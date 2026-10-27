#pragma once
#include <nlohmann/json.hpp>
namespace hibernation {
inline nlohmann::json imageDeliveryContext(bool available,std::size_t imageCount){
    return {{"image_input_available",available},{"delivery_mode","on_demand_images"},{"images_in_current_request",imageCount},{"continuous_screen_feed",false},{"microphone_feed",false},{"virtual_environment_source","body_and_enabled_plugin_state"}};
}
// Model-facing physical state. Admin diagnostics and source filenames stay in
// plugin snapshots; this projection contains only available actions and live pose.
inline nlohmann::json widgetBodyContext(const nlohmann::json& value){
    using Json=nlohmann::json;Json result=Json::object();
    for(const auto* key:{"state","visible","desktop","x","y","target_x","target_y","facing","moving","dialogue_phase","autonomous_mode","roaming","dragging","dropping","held_pose","pending_pose","animation_catalog","screen_geometry"})if(value.contains(key))result[key]=value.at(key);
    const auto animation=value.value("animation",Json::object());result["animation"]=Json::object();
    for(const auto* key:{"requested","resolved","phase","variant","fallback"})if(animation.contains(key))result["animation"][key]=animation.at(key);
    return result;
}
inline nlohmann::json activityBodyContext(const nlohmann::json& plan){
    using Json=nlohmann::json;auto command=[](const Json& value){if(!value.is_object())return Json(nullptr);Json result=Json::object();for(const auto* key:{"kind","seconds","purpose","x","y","state","status","template","name","title","piece_kind","piece_id","stage","query","key","language","object_id","action","game_id","expected_revision","cell","effect_outcome"})if(value.contains(key))result[key]=value.at(key);return result;};
    const auto linked=[&](const Json& item){auto value=command(item);if(value.is_object())for(const auto* key:{"intention","attempt_source_id"})if(item.contains(key))value[key]=item.at(key);return value;};
    Json next=Json::array();for(const auto& item:plan.value("entries",Json::array()))next.push_back(linked(item));
    Json recent=Json::array();const auto history=plan.value("recent",Json::array());const auto begin=history.size()>3?history.size()-3:0;for(std::size_t i=begin;i<history.size();++i)recent.push_back(linked(history[i]));
    return {{"active",linked(plan.value("active",Json(nullptr)))},{"next",next},{"recent",recent},{"purpose_meaning","purpose只是当时想做这件事的目的；status和effect_outcome才说明实际发生什么。intention可连接self里的未完打算；一次动作完成不等于更大的打算已达成，not_started表示还没开始就取消了。"}};
}
}
