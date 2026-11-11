#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
namespace hibernation::idle_economy {
inline int band(std::int64_t seconds){return seconds>=1800?2:seconds>=300?1:0;}
inline std::int64_t interval(int mode){return mode>=2?1800000:mode==1?600000:0;}
inline bool due(int mode,std::int64_t last,std::int64_t now){return mode==0||last==0||now>=last+interval(mode);}
inline std::size_t historyBytes(std::size_t configured,std::size_t required,bool followup,bool idleSolitude){return std::max(required,followup?std::min<std::size_t>(configured,16000):idleSolitude?std::min<std::size_t>(configured,12000):configured);}
inline bool reserveAvailable(const nlohmann::json& budget,int mode=2){
    if(!budget.value("enabled",true))return true;
    const auto cap=budget.at("max_input_tokens").get<std::uint64_t>();const auto calls=budget.at("max_calls").get<std::uint64_t>();
    // Keep a return reserve plus admission headroom for a complete actor/review
    // pair. Reported usage is observed after calls, so this is not a hard hold.
    return !budget.at("blocked").get<bool>()&&budget.at("input_tokens").get<std::uint64_t>()<(mode==0?cap-cap/10:cap-cap/5-cap/5)&&budget.at("remaining_calls").get<std::uint64_t>()>std::min<std::uint64_t>(mode==0?2:8,calls/(mode==0?10:5));
}
inline nlohmann::json commands(const nlohmann::json& available,int mode){
    if(mode==0)return available;nlohmann::json out=nlohmann::json::array();
    for(const auto& kind:available)if(kind=="wait"||kind=="pose"||kind=="rest"||kind=="move_to"||kind=="look_nearest"||kind=="eat_nearest"||kind=="gather"||kind=="gather_for_recipe"||kind=="pursue_intention")out.push_back(kind);return out;
}
}
