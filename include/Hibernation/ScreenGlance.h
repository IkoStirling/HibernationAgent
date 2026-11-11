#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
namespace hibernation::screen_glance {
using Json=nlohmann::json;
inline bool enabled(const Json& computer,bool desktop,bool channel,bool vision){
    return desktop&&!channel&&vision&&computer.value("enabled",true)&&computer.value("allowScreenCapture",false)&&computer.value("autonomousObservationEnabled",false);
}
inline bool due(const Json& state,std::int64_t now){return now>=state.value("next_ms",std::int64_t(0));}
inline Json admitted(const Json& computer,const std::string& id,std::int64_t now){
    return {{"admission_id",id},{"admitted_ms",now},{"expires_ms",now+180000},{"next_ms",now+1000LL*computer.value("autonomousObservationIntervalSeconds",300)},{"capture_claimed",false}};
}
inline bool valid(const Json& state,const std::string& id,std::int64_t now){return !id.empty()&&id==state.value("admission_id",std::string{})&&now<=state.value("expires_ms",std::int64_t(0));}
}
