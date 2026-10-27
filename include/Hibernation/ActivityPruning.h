#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
namespace hibernation {
struct ActivityPruning {
    bool stopActive=false;
    nlohmann::json pending=nlohmann::json::array(),removed=nlohmann::json::array();
};
inline ActivityPruning pruneActivityIntentions(const nlohmann::json& plan,std::int64_t now,const std::function<bool(const nlohmann::json&)>& current){
    using Json=nlohmann::json;ActivityPruning result;
    const auto active=plan.value("active",Json(nullptr));result.stopActive=active.is_object()&&active.contains("intention")&&!current(active.at("intention"));
    for(const auto& entry:plan.value("entries",Json::array())){
        const bool stale=entry.contains("intention")&&!current(entry.at("intention"));const bool expired=entry.value("expires_ms",now+1)<=now;
        if(!stale&&!expired){result.pending.push_back(entry);continue;}
        auto removed=entry;removed["status"]=stale?"not_started_intention_changed":"not_started_expired";removed["ended_ms"]=now;result.removed.push_back(std::move(removed));
    }return result;
}
}
