#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <string>
namespace hibernation {
inline bool creativeSlotAvailable(const nlohmann::json& state){
    if(!state.is_object())return false;
    if(state.contains("can_create")&&state.at("can_create").is_boolean())return state.at("can_create").get<bool>();
    if(!state.contains("piece_count")||!state.at("piece_count").is_number_integer()||!state.contains("maximum")||!state.at("maximum").is_number_integer())return false;
    return state.at("piece_count").get<std::int64_t>()<state.at("maximum").get<std::int64_t>();
}
inline nlohmann::json visibleContinuationDrafts(const nlohmann::json& state){
    using Json=nlohmann::json;Json drafts=Json::array();std::set<std::string> seen;
    if(!state.is_object())return drafts;
    for(const auto* field:{"pieces","continuations"}){
        if(!state.contains(field)||!state.at(field).is_array())continue;
        std::size_t visited=0;for(const auto& item:state.at(field)){
            if(++visited>8)break;
            if(!item.is_object()||!item.contains("id")||!item.at("id").is_string()||!item.contains("stage")||item.at("stage")!="draft"||!item.contains("revision")||!item.at("revision").is_number_integer())continue;
            const auto id=item.at("id").get<std::string>();if(id.empty()||id.size()>48||!seen.insert(id).second)continue;
            drafts.push_back({{"id",id},{"revision",item.at("revision")},{"stage","draft"}});
        }
    }
    return drafts;
}
}
