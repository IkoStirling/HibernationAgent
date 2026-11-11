#pragma once
#include <nlohmann/json.hpp>
namespace hibernation {
// Current proprioception only. Past gathered items and archived outcomes stay
// in their source-aware history; this view grants no memory-write authority.
inline nlohmann::json currentWorldActions(const nlohmann::json& snapshots){
    using Json=nlohmann::json;Json result={{"gathering",nullptr},{"cooking",nullptr}};
    const auto habitat=snapshots.value("habitat",Json::object());const auto gather=habitat.is_object()?habitat.value("gather",Json(nullptr)):Json(nullptr);
    if(gather.is_object()){
        Json brief=Json::object();for(const auto* field:{"id","phase","requested","collected","needs","region","destination","target"})if(gather.contains(field))brief[field]=gather.at(field);result["gathering"]=std::move(brief);
    }
    const auto canteen=snapshots.value("canteen",Json::object());const auto kitchen=canteen.is_object()?canteen.value("current",Json::object()):Json(nullptr);const auto job=kitchen.is_object()?kitchen.value("job",Json(nullptr)):Json(nullptr);
    if(job.is_object()){
        Json brief=Json::object();for(const auto* field:{"id","batch_id","recipe_id","phase","stage_name"})if(job.contains(field))brief[field]=job.at(field);result["cooking"]=std::move(brief);
    }return result;
}
}
