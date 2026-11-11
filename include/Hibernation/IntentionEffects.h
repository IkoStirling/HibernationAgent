#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace hibernation::intention_life {
using Json=nlohmann::json;
inline std::string effectBatch(const Json& event){
    const auto plugin=event.value("plugin",std::string{}),kind=event.value("kind",std::string{});
    if(plugin=="canteen"){
        if(kind=="canteen.ready"||kind=="canteen.taken"||kind=="canteen.expired"||kind=="canteen.discarded")return event.value("batch",Json::object()).value("id",std::string{});
        if(kind=="canteen.cancelled"||kind=="canteen.chain_interrupted")return event.value("job",Json::object()).value("batch_id",std::string{});
    }
    if(plugin=="habitat"&&kind=="habitat.consumed")return event.value("item",Json::object()).value("provenance",Json::object()).value("batch_id",std::string{});
    return {};
}
inline std::string effectPhase(const Json& event){
    const auto kind=event.value("kind",std::string{});
    if(kind=="canteen.ready")return "ready";if(kind=="canteen.taken")return "on_desktop";if(kind=="habitat.consumed")return "consumed";
    if(kind=="canteen.expired")return "expired";if(kind=="canteen.discarded")return "discarded";
    if(kind=="canteen.cancelled")return "cancelled";if(kind=="canteen.chain_interrupted")return "interrupted";return {};
}
inline bool advanceEffect(const std::string& before,const std::string& next){
    if(before==next||before=="consumed"||before=="expired"||before=="discarded"||before=="cancelled"||before=="interrupted")return false;
    if(next=="ready")return before=="cooking";if(next=="on_desktop")return before=="cooking"||before=="ready";
    return next=="consumed"||next=="expired"||next=="discarded"||next=="cancelled"||next=="interrupted";
}
}
