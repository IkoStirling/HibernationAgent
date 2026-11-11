#pragma once
#include <Hibernation/ContextControl.h>
#include <map>
#include <limits>
#include <optional>
namespace hibernation {
// Keep the legacy event envelope and its evidence identities intact. Only its
// wire ordering changes: already-authorized history precedes volatile snapshots.
inline std::string cacheOrderedEvent(const nlohmann::json& event){
    nlohmann::ordered_json wire=nlohmann::ordered_json::object();
    for(const auto* key:{"conversation_delivery","authorized_context","event_kind"})if(event.contains(key))wire[key]=event.at(key);
    for(auto it=event.begin();it!=event.end();++it)if(!wire.contains(it.key()))wire[it.key()]=it.value();
    return wire.dump();
}
inline nlohmann::ordered_json cacheBackgroundWire(const nlohmann::json& event){
    nlohmann::ordered_json wire=nlohmann::ordered_json::object();
    for(const auto* key:{"conversation_delivery","memories","self","evidence","excluded","recent_activities"})if(event.contains(key))wire[key]=event.at(key);
    for(auto it=event.begin();it!=event.end();++it)if(!wire.contains(it.key()))wire[it.key()]=it.value();return wire;
}
inline std::string cacheOrderedReview(const nlohmann::json& event){
    nlohmann::ordered_json wire=nlohmann::ordered_json::object();
    for(const auto* key:{"conversation_delivery","event_context","context","current_user_message","conversation_focus","authoritative_state","open_questions","state","excluded"})if(event.contains(key))wire[key]=event.at(key);
    for(auto it=event.begin();it!=event.end();++it)if(!wire.contains(it.key()))wire[it.key()]=it.value();
    // Ordering only the wrapper is insufficient: json assignment otherwise
    // restores alphabetical body/state ordering inside the review evidence.
    if(event.contains("context")&&event.at("context").is_object())wire["context"]=cacheBackgroundWire(event.at("context"));
    if(event.contains("event_context")&&event.at("event_context").is_string()){
        const auto nested=nlohmann::json::parse(event.at("event_context").get_ref<const std::string&>(),nullptr,false);
        if(nested.is_object())wire["event_context"]=cacheOrderedEvent(nested);
    }
    return wire.dump();
}
inline std::string cacheOrderedBackground(const nlohmann::json& event){
    return cacheBackgroundWire(event).dump();
}
// Keep an already-sent tool receipt immutable while the turn remains within a
// bounded receipt window. Rewriting it on every step destroys append-only reuse.
// A real overflow starts a compacted prefix; authority/source checks are unchanged.
inline bool compactCachedToolMessages(std::vector<ayt::inference::Message>& messages,std::size_t limit,std::size_t first=0){
    std::size_t bytes=0;
    for(std::size_t i=first;i<messages.size();++i){const auto& message=messages[i];
        if(message.role=="tool")bytes+=message.text.size();
        else if(message.role=="user"){
            const auto value=nlohmann::json::parse(message.text,nullptr,false);
            if(value.is_object()&&value.contains("tool_results"))bytes+=message.text.size();
        }
    }
    if(bytes<=std::max<std::size_t>(16384,limit*4))return false;
    bool changed=false;
    for(std::size_t i=first;i<messages.size();++i){auto& message=messages[i];
        if(message.role!="tool"&&message.role!="user")continue;
        auto value=nlohmann::json::parse(message.text,nullptr,false);if(!value.is_object())continue;
        if(message.role=="tool")value=priorReceiptContext(value,limit);
        else if(value.contains("tool_results")&&value.at("tool_results").is_array())for(auto& receipt:value["tool_results"])receipt=priorReceiptContext(receipt,limit);
        else continue;
        auto text=value.dump();if(text!=message.text){message.text=std::move(text);changed=true;}
    }
    return changed;
}
inline nlohmann::json cacheUsageJson(const ayt::inference::Usage& usage){
    using Json=nlohmann::json;Json out={{"input_tokens",nullptr},{"output_tokens",nullptr},{"cached_input_tokens",nullptr},{"uncached_input_tokens",nullptr},{"cache_hit_ratio",nullptr}};
    if(usage.inputTokens)out["input_tokens"]=*usage.inputTokens;if(usage.outputTokens)out["output_tokens"]=*usage.outputTokens;
    if(usage.cachedInputTokens)out["cached_input_tokens"]=*usage.cachedInputTokens;if(usage.uncachedInputTokens)out["uncached_input_tokens"]=*usage.uncachedInputTokens;
    if(usage.cachedInputTokens&&usage.uncachedInputTokens&&(*usage.cachedInputTokens||*usage.uncachedInputTokens))out["cache_hit_ratio"]=double(*usage.cachedInputTokens)/(double(*usage.cachedInputTokens)+double(*usage.uncachedInputTokens));
    return out;
}
inline void batchCacheUsage(nlohmann::json& batch,const std::string& role,const std::string& profile,const ayt::inference::Usage& usage){batch[role+"_profile_id"]=profile;batch[role+"_usage"]=cacheUsageJson(usage);}
inline std::optional<std::uint64_t> reportedUsageCount(const nlohmann::json& value,const char* key){
    const auto at=value.find(key);if(at==value.end()||!at->is_number_integer())return std::nullopt;
    if(at->is_number_unsigned())return at->get<std::uint64_t>();
    const auto count=at->get<std::int64_t>();return count>=0?std::optional<std::uint64_t>(static_cast<std::uint64_t>(count)):std::nullopt;
}
inline nlohmann::json addCacheUsage(nlohmann::json total,const nlohmann::json& usage){
    using Json=nlohmann::json;if(!total.is_object())total=Json::object();
    const bool legacy=reportedUsageCount(total,"calls").value_or(0)>0&&!total.contains("input_reported_calls");
    for(const auto* key:{"calls","usage_reported_calls","cache_reported_calls","input_tokens","output_tokens","cached_input_tokens","uncached_input_tokens"})if(!total.contains(key))total[key]=std::uint64_t(0);
    for(const auto* key:{"input_reported_calls","output_reported_calls","complete_usage_reported_calls"})if(!total.contains(key))total[key]=legacy?Json(nullptr):Json(std::uint64_t(0));
    // A historical positive sum proves at least one observation, but an old zero
    // does not prove the provider reported zero. Exact legacy report counts stay unknown.
    if(!total.contains("input_tokens_known"))total["input_tokens_known"]=reportedUsageCount(total,"input_tokens").value_or(0)>0;
    if(!total.contains("output_tokens_known"))total["output_tokens_known"]=reportedUsageCount(total,"output_tokens").value_or(0)>0;
    const auto add=[&](const char* key,std::uint64_t amount){const auto previous=reportedUsageCount(total,key).value_or(0);if(amount>std::numeric_limits<std::uint64_t>::max()-previous){total[key]=std::numeric_limits<std::uint64_t>::max();total["counter_saturated"]=true;if(std::string_view(key)=="cached_input_tokens"||std::string_view(key)=="uncached_input_tokens")total["cache_counter_saturated"]=true;}else total[key]=previous+amount;};
    const auto observed=[&](const char* key){if(!total.at(key).is_null())add(key,1);};
    const auto input=reportedUsageCount(usage,"input_tokens"),output=reportedUsageCount(usage,"output_tokens"),hit=reportedUsageCount(usage,"cached_input_tokens"),miss=reportedUsageCount(usage,"uncached_input_tokens");
    add("calls",1);if(input||output)add("usage_reported_calls",1);
    if(input){add("input_tokens",*input);observed("input_reported_calls");total["input_tokens_known"]=true;}
    if(output){add("output_tokens",*output);observed("output_reported_calls");total["output_tokens_known"]=true;}
    if(input&&output)observed("complete_usage_reported_calls");
    // Cache ratios use only paired provider cache counters, never a missing side as zero.
    if(hit&&miss){add("cache_reported_calls",1);add("cached_input_tokens",*hit);add("uncached_input_tokens",*miss);}
    const double hits=total.at("cached_input_tokens").get<double>(),misses=total.at("uncached_input_tokens").get<double>();total["cache_hit_ratio"]=!total.value("cache_counter_saturated",false)&&(hits+misses)>0?Json(hits/(hits+misses)):Json(nullptr);return total;
}
// Normal lifetime buckets stay bounded; a fixed extra bucket retains every later
// reported call without accumulating an unbounded list of profile or purpose IDs.
inline nlohmann::json addModelCacheUsage(nlohmann::json state,const std::string& profile,const std::string& purpose,const nlohmann::json& usage){
    using Json=nlohmann::json;auto& connections=state["connections"];if(!connections.is_object())connections=Json::object();
    constexpr const char* otherProfiles="$other_profiles";constexpr const char* otherPurposes="$other_purposes";
    const auto profiles=connections.size()-(connections.contains(otherProfiles)?1:0);
    const auto& profileKey=connections.contains(profile)||profiles<32?profile:std::string(otherProfiles);
    auto& connection=connections[profileKey];if(!connection.is_object())connection=Json::object();
    if(profileKey==otherProfiles)connection["aggregation"]="additional_profiles";
    auto& groups=connection["purposes"];if(!groups.is_object())groups=Json::object();
    const auto purposes=groups.size()-(groups.contains(otherPurposes)?1:0);
    const auto& purposeKey=groups.contains(purpose)||purposes<24?purpose:std::string(otherPurposes);
    groups[purposeKey]=addCacheUsage(groups.value(purposeKey,Json::object()),usage);
    connection["total"]=addCacheUsage(connection.value("total",Json::object()),usage);return state;
}
}
