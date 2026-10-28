#pragma once
#include <Hibernation/ContextControl.h>
#include <map>
namespace hibernation {
// Keep the legacy event envelope and its evidence identities intact. Only its
// wire ordering changes: already-authorized history precedes volatile snapshots.
inline std::string cacheOrderedEvent(const nlohmann::json& event){
    nlohmann::ordered_json wire=nlohmann::ordered_json::object();
    for(const auto* key:{"event_kind","conversation_delivery","authorized_context"})if(event.contains(key))wire[key]=event.at(key);
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
inline nlohmann::json addCacheUsage(nlohmann::json total,const nlohmann::json& usage){
    const auto count=[](const nlohmann::json& v,const char* key)->std::optional<std::uint64_t>{const auto at=v.find(key);return at!=v.end()&&at->is_number_unsigned()?std::optional(at->get<std::uint64_t>()):std::nullopt;};
    for(const auto* key:{"calls","usage_reported_calls","cache_reported_calls","input_tokens","output_tokens","cached_input_tokens","uncached_input_tokens"})if(!total.contains(key))total[key]=std::uint64_t(0);
    total["calls"]=total.at("calls").get<std::uint64_t>()+1;
    const auto input=count(usage,"input_tokens"),output=count(usage,"output_tokens"),hit=count(usage,"cached_input_tokens"),miss=count(usage,"uncached_input_tokens");
    if(input||output)total["usage_reported_calls"]=total.at("usage_reported_calls").get<std::uint64_t>()+1;
    if(input)total["input_tokens"]=total.at("input_tokens").get<std::uint64_t>()+*input;if(output)total["output_tokens"]=total.at("output_tokens").get<std::uint64_t>()+*output;
    if(hit&&miss){total["cache_reported_calls"]=total.at("cache_reported_calls").get<std::uint64_t>()+1;total["cached_input_tokens"]=total.at("cached_input_tokens").get<std::uint64_t>()+*hit;total["uncached_input_tokens"]=total.at("uncached_input_tokens").get<std::uint64_t>()+*miss;}
    const double hits=total.at("cached_input_tokens").get<double>(),misses=total.at("uncached_input_tokens").get<double>();total["cache_hit_ratio"]=(hits+misses)>0?nlohmann::json(hits/(hits+misses)):nlohmann::json(nullptr);return total;
}
}
