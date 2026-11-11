#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace hibernation::background {
using Json=nlohmann::json;
constexpr std::int64_t dayMs=86400000,offsetMs=8*3600000;
struct Policy {
    bool enabled=true,autoEconomy=false;std::uint64_t calls=120,inputTokens=500000;
    static Policy read(const Json& value){
        if(!value.is_object())throw std::runtime_error("background_budget_object");
        const std::set<std::string> allowed={"enabled","maxCallsPerDay","maxInputTokensPerDay","autoEconomy"};
        for(auto it=value.begin();it!=value.end();++it)if(!allowed.contains(it.key()))throw std::runtime_error("background_budget_field");
        Policy policy;if(value.contains("enabled")){if(!value.at("enabled").is_boolean())throw std::runtime_error("background_budget_enabled");policy.enabled=value.at("enabled");}if(value.contains("autoEconomy")){if(!value.at("autoEconomy").is_boolean())throw std::runtime_error("background_budget_auto_economy");policy.autoEconomy=value.at("autoEconomy");}
        const auto integer=[&](const char* key,std::uint64_t fallback,std::uint64_t minimum,std::uint64_t maximum){if(!value.contains(key))return fallback;const auto& v=value.at(key);if(!v.is_number_integer()||v.get<double>()<double(minimum)||v.get<double>()>double(maximum))throw std::runtime_error("background_budget_range");return v.get<std::uint64_t>();};
        policy.calls=integer("maxCallsPerDay",120,1,10000);policy.inputTokens=integer("maxInputTokensPerDay",500000,1000,100000000);return policy;
    }
    Json json()const{return {{"enabled",enabled},{"maxCallsPerDay",calls},{"maxInputTokensPerDay",inputTokens},{"autoEconomy",autoEconomy}};}
};
inline std::uint64_t sum(std::uint64_t a,std::uint64_t b){return b>std::numeric_limits<std::uint64_t>::max()-a?std::numeric_limits<std::uint64_t>::max():a+b;}
inline std::int64_t dayIndex(std::int64_t now){return now< -offsetMs?0:(now/dayMs+(now%dayMs+offsetMs)/dayMs);}
inline Json fresh(std::int64_t now){return {{"version",2},{"day_index",dayIndex(now)},{"calls",std::uint64_t(0)},{"input_tokens",std::uint64_t(0)},{"reported_calls",std::uint64_t(0)},{"total_input_tokens",std::uint64_t(0)},{"cached_input_tokens",std::uint64_t(0)},{"confirmed_uncached_input_tokens",std::uint64_t(0)},{"unknown_cache_input_tokens",std::uint64_t(0)},{"cache_reported_calls",std::uint64_t(0)},{"unknown_total_calls",std::uint64_t(0)},{"last_seen_ms",now}};}
inline Json normalize(const Json& saved,std::int64_t now){
    if(!saved.is_object()||(saved.value("version",0)!=1&&saved.value("version",0)!=2))throw std::runtime_error("background_budget_version");
    if(dayIndex(now)>saved.at("day_index").get<std::int64_t>()&&now>=saved.at("last_seen_ms").get<std::int64_t>())return fresh(now);
    auto value=saved;if(value.at("version")==1){value["version"]=2;value["total_input_tokens"]=value.at("input_tokens");value["cached_input_tokens"]=std::uint64_t(0);value["confirmed_uncached_input_tokens"]=std::uint64_t(0);value["unknown_cache_input_tokens"]=value.at("input_tokens");value["cache_reported_calls"]=std::uint64_t(0);value["unknown_total_calls"]=std::uint64_t(0);value["legacy_cache_split_unavailable"]=true;}
    value["last_seen_ms"]=std::max(now,saved.at("last_seen_ms").get<std::int64_t>());return value;
}
inline std::string reason(const Policy& policy,const Json& state){
    if(!policy.enabled)return {};
    if(state.at("calls").get<std::uint64_t>()>=policy.calls)return "daily_calls";
    if(state.at("input_tokens").get<std::uint64_t>()>=policy.inputTokens)return "daily_input_tokens";return {};
}
inline bool canReserve(const Policy& policy,const Json& state,std::uint64_t amount){return amount>0&&(!policy.enabled||(reason(policy,state).empty()&&amount<=policy.calls-state.at("calls").get<std::uint64_t>()));}
inline Json reserve(Json state,std::uint64_t amount){state["calls"]=sum(state.at("calls").get<std::uint64_t>(),amount);return state;}
inline Json observe(Json state,const Json& usage){
    const auto count=[&](const char* key)->std::optional<std::uint64_t>{const auto it=usage.find(key);if(it==usage.end()||!it->is_number_integer()||it->get<double>()<0)return {};return it->get<std::uint64_t>();};
    auto input=count("input_tokens"),hit=count("cached_input_tokens"),miss=count("uncached_input_tokens");
    if(!input&&hit&&miss&&*hit<=std::numeric_limits<std::uint64_t>::max()-*miss)input=*hit+*miss;
    if(!input&&!miss)return state;
    const auto add=[&](const char* key,std::uint64_t n){state[key]=sum(state.value(key,std::uint64_t(0)),n);};
    add("reported_calls",1);add("total_input_tokens",input?*input:*miss);if(!input)add("unknown_total_calls",1);
    bool split=false;std::uint64_t charged=0,cached=0;
    if(input&&hit&&miss&&*hit<=*input&&*miss==*input-*hit){charged=*miss;cached=*hit;split=true;}
    else if(input&&hit&&!miss&&*hit<=*input){charged=*input-*hit;cached=*hit;split=true;}
    else if(input&&miss&&!hit&&*miss<=*input){charged=*miss;cached=*input-*miss;split=true;}
    else if(!input&&miss){charged=*miss;split=true;}
    if(split){add("confirmed_uncached_input_tokens",charged);add("cached_input_tokens",cached);add("cache_reported_calls",1);}
    else{charged=input.value_or(0);add("unknown_cache_input_tokens",charged);}
    add("input_tokens",charged);return state;
}
inline bool purpose(const std::string& value){static const std::set<std::string> names={"solitude","outer_event","task_complete","story_end","activity","activity_review","reaction","reaction_review","background_review"};return names.contains(value);}
inline bool recoverLegacyCacheSplit(Json& state,const Json& baseline,const Json& current){
    if(!state.value("legacy_cache_split_unavailable",false))return false;
    const auto totals=[](const Json& snapshot)->std::optional<std::array<std::uint64_t,3>>{
        std::array<std::uint64_t,3> out{};const auto connections=snapshot.value("connections",Json::object());
        for(const auto& connection:connections){const auto groups=connection.value("purposes",Json::object());for(auto it=groups.begin();it!=groups.end();++it){if(!purpose(it.key()))continue;const auto& value=it.value();if(value.value("counter_saturated",false)||value.value("cache_counter_saturated",false))return {};int i=0;for(const auto* field:{"input_tokens","cached_input_tokens","uncached_input_tokens"}){const auto n=value.value(field,std::uint64_t(0));if(n>std::numeric_limits<std::uint64_t>::max()-out[i])return {};out[i++]+=n;}}}return out;
    };
    const auto before=totals(baseline),after=totals(current);if(!before||!after)return false;std::array<std::uint64_t,3> delta{};
    for(int i=0;i<3;++i){if((*after)[i]<(*before)[i])return false;delta[i]=(*after)[i]-(*before)[i];}
    if(delta[0]!=state.at("total_input_tokens")||delta[1]>delta[0]||delta[2]>delta[0]-delta[1])return false;
    state["cached_input_tokens"]=delta[1];state["confirmed_uncached_input_tokens"]=delta[2];state["unknown_cache_input_tokens"]=delta[0]-delta[1]-delta[2];state["input_tokens"]=delta[0]-delta[1];state["legacy_cache_split_unavailable"]=false;state["cache_split_recovered"]=true;return true;
}
inline Json status(const Policy& policy,const Json& state,std::int64_t now){
    const auto day=state.at("day_index").get<std::int64_t>();const std::chrono::year_month_day date{std::chrono::sys_days{std::chrono::days{day}}};
    const auto pad=[](unsigned n){return n<10?"0"+std::to_string(n):std::to_string(n);};const auto label=std::to_string(int(date.year()))+"-"+pad(unsigned(date.month()))+"-"+pad(unsigned(date.day()));
    const auto used=state.at("calls").get<std::uint64_t>(),reported=state.at("reported_calls").get<std::uint64_t>();const auto blocked=reason(policy,state);
    return {{"enabled",policy.enabled},{"auto_economy",policy.autoEconomy},{"accounting_basis","uncached_input_tokens"},{"accounting_migration",state.value("accounting_migration",Json(nullptr))},{"day",label},{"timezone","Asia/Shanghai"},{"used_calls",used},{"remaining_calls",used>=policy.calls?0:policy.calls-used},{"max_calls",policy.calls},{"input_tokens",state.at("input_tokens")},{"total_input_tokens",state.at("total_input_tokens")},{"cached_input_tokens",state.at("cached_input_tokens")},{"confirmed_uncached_input_tokens",state.at("confirmed_uncached_input_tokens")},{"unknown_cache_input_tokens",state.at("unknown_cache_input_tokens")},{"legacy_cache_split_unavailable",state.value("legacy_cache_split_unavailable",false)},{"reported_calls",reported},{"unknown_usage_calls",used>reported?used-reported:0},{"unknown_total_calls",state.value("unknown_total_calls",0)},{"max_input_tokens",policy.inputTokens},{"blocked",!blocked.empty()},{"blocked_reason",blocked},{"resets_at_ms",(day+1)*dayMs-offsetMs},{"clock_reversed",now<state.at("last_seen_ms").get<std::int64_t>()},{"token_limit_is_observed",true}};
}
}
