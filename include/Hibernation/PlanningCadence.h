#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
namespace hibernation {
using CadenceJson=nlohmann::json;
inline CadenceJson emptyPlanningCadence(){return {{"version",1},{"quiet",0},{"failures",0},{"last_ms",0},{"next_ms",0}};}
inline bool validPlanningCadence(const CadenceJson& value){
    if(!value.is_object()||!value.contains("version")||!value.at("version").is_number_integer()||value.at("version")!=1)return false;
    for(const auto* field:{"quiet","failures","last_ms","next_ms"})if(!value.contains(field)||!value.at(field).is_number_integer())return false;
    const auto last=value.at("last_ms").get<std::int64_t>(),next=value.at("next_ms").get<std::int64_t>();
    return value.at("quiet").get<std::int64_t>()>=0&&value.at("quiet").get<std::int64_t>()<=5&&value.at("failures").get<std::int64_t>()>=0&&value.at("failures").get<std::int64_t>()<=5&&last>=0&&next>=last&&next-last<=3600000;
}
inline std::int64_t planningBase(std::int64_t base){return std::clamp<std::int64_t>(base,30000,3600000);}
inline CadenceJson planningOutcome(const CadenceJson& saved,bool success,bool empty,std::int64_t now,std::int64_t base){
    if(!validPlanningCadence(saved)||now<0||now>std::numeric_limits<std::int64_t>::max()-3600000)return saved;
    auto value=saved;value["quiet"]=success&&empty?std::min(5,saved.at("quiet").get<int>()+1):0;value["failures"]=!success?std::min(5,saved.at("failures").get<int>()+1):0;
    const auto streak=std::max(value.at("quiet").get<int>(),value.at("failures").get<int>());base=planningBase(base);const auto delay=std::min(std::max<std::int64_t>(base,900000),base*(std::int64_t(1)<<streak));
    value["last_ms"]=now;value["next_ms"]=now+delay;return value;
}
inline std::int64_t restoredPlanningDue(const CadenceJson& saved,std::int64_t now,std::int64_t base){
    if(now<0||now>std::numeric_limits<std::int64_t>::max()-3600000)return now;
    if(!validPlanningCadence(saved))return now+planningBase(base);
    const auto maximum=std::max<std::int64_t>(planningBase(base),900000);if(now<saved.at("last_ms").get<std::int64_t>())return now+planningBase(base);
    return std::clamp(saved.at("next_ms").get<std::int64_t>(),now,now+maximum);
}
}
