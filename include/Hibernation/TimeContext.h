#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <ctime>
#include <limits>
namespace hibernation {
// Human-readable model context derived from the same authoritative timestamp.
// Local time describes the host OS zone, never a guessed user location/weather.
inline nlohmann::json presentationTimeContext(std::int64_t milliseconds){
    using Json=nlohmann::json;Json result={{"clock_source","host_os"},{"formatted_time_available",false}};
    if(milliseconds<0||milliseconds/1000>std::numeric_limits<std::time_t>::max())return result;
    const auto seconds=static_cast<std::time_t>(milliseconds/1000);std::tm utc{},local{};
#ifdef _WIN32
    if(gmtime_s(&utc,&seconds)||localtime_s(&local,&seconds))return result;
#else
    if(!gmtime_r(&seconds,&utc)||!localtime_r(&seconds,&local))return result;
#endif
    char utcText[32]{},localText[64]{};
    if(!std::strftime(utcText,sizeof utcText,"%Y-%m-%dT%H:%M:%SZ",&utc)||!std::strftime(localText,sizeof localText,"%Y-%m-%dT%H:%M:%S%z",&local))return result;
    result.update({{"utc_time",utcText},{"host_local_time",localText},{"host_local_hour",local.tm_hour},{"formatted_time_available",true}});return result;
}
}
