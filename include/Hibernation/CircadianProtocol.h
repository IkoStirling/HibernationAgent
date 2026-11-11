#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
namespace hibernation {
// Cache freshness is deliberately coarse: never key on a continuously changing
// clock or exact inertia value. One waking -> settled transition is sufficient.
inline const char* circadianReactionBand(const nlohmann::json& state){
 if(!state.is_object())return "settled";
 if(state.value("phase",std::string{})=="sleeping")return "sleeping";
 const auto value=state.find("sleep_inertia");
 if(value!=state.end()&&value->is_number()){const double inertia=value->get<double>();if(std::isfinite(inertia)&&inertia>=.12)return "waking";}
 return "settled";
}
inline nlohmann::json circadianContext(const nlohmann::json& state){using J=nlohmann::json;J view=J::object();if(!state.is_object())return view;for(const auto* field:{"configured","preferences","preferences_revision","phase","remaining_seconds","elapsed_seconds","active_rest_seconds","sleep_inertia","guide"})if(state.contains(field))view[field]=state.at(field);return view;}
inline nlohmann::json circadianPreferencesSchema(){
 using J=nlohmann::json;return {{"type","object"},{"additionalProperties",false},{"required",J::array({"enabled","start_minute","end_minute"})},{"properties",{{"enabled",{{"type","boolean"}}},{"start_minute",{{"type","integer"},{"minimum",0},{"maximum",1439}}},{"end_minute",{{"type","integer"},{"minimum",0},{"maximum",1439}}},{"drag_wake_probability",{{"type","number"},{"minimum",0},{"maximum",1}}},{"message_wake_probability",{{"type","number"},{"minimum",0},{"maximum",1}}},{"inertia_seconds",{{"type","number"},{"minimum",30},{"maximum",3600}}}}}};
}
inline nlohmann::json circadianProposalSchema(){using J=nlohmann::json;return {{"type","object"},{"additionalProperties",false},{"required",J::array({"expected_revision","preferences"})},{"properties",{{"expected_revision",{{"type","integer"},{"minimum",0},{"maximum",1000000}}},{"preferences",circadianPreferencesSchema()}}}};}
}
