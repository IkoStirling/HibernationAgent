#pragma once
#include <Hibernation/WidgetAppearance.h>
#include <nlohmann/json.hpp>
#include <cstdio>

namespace hibernation::appearance {
// Dashboard previews are sampled from the same original frames as the native
// widget. This catalog contains no character state, records or plugin code.
inline nlohmann::json catalog(){
    using Json=nlohmann::json;Json skins=Json::array(),palettes=Json::array();
    for(const auto* id:{"dinosaur","moonbird","lumen"}){
        Json poses=Json::array();
        for(const auto* state:{"idle","walk","resting","speaking"}){
            const auto frame=make(id,state,.15,false,1,true);std::string cells;cells.reserve(Frame::extent*Frame::extent);
            for(auto cell:frame.cells)cells.push_back(static_cast<char>('0'+cell));
            poses.push_back({{"state",state},{"cells",cells},{"scale_x",frame.scaleX},{"scale_y",frame.scaleY},{"offset_y",frame.offsetY}});
        }
        skins.push_back({{"id",id},{"poses",std::move(poses)}});
    }
    for(const auto* id:{"forest","mist","amber"}){
        Json colors=Json::array();for(auto value:palette(id).rgb){char hex[8]{};std::snprintf(hex,sizeof(hex),"#%06x",static_cast<unsigned>(value));colors.push_back(hex);}
        palettes.push_back({{"id",id},{"colors",std::move(colors)}});
    }
    return {{"version",1},{"extent",Frame::extent},{"skins",std::move(skins)},{"palettes",std::move(palettes)}};
}
}
