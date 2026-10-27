#pragma once
#include <Hibernation/WidgetAppearance.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <Hibernation/CharacterAnimation.h>
#include <functional>

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
        skins.push_back({{"id",id},{"label",std::string(id)=="dinosaur"?"小恐龙":std::string(id)=="moonbird"?"月羽鸟":"游光"},{"poses",std::move(poses)}});
    }
    for(const auto* id:{"forest","mist","amber"}){
        Json colors=Json::array();for(auto value:palette(id).rgb){char hex[8]{};std::snprintf(hex,sizeof(hex),"#%06x",static_cast<unsigned>(value));colors.push_back(hex);}
        palettes.push_back({{"id",id},{"colors",std::move(colors)}});
    }
    return {{"version",1},{"extent",Frame::extent},{"skins",std::move(skins)},{"palettes",std::move(palettes)}};
}
inline nlohmann::json catalog(const std::filesystem::path& pluginRoot,const nlohmann::json& widget,
    const std::function<std::string(const std::vector<std::uint8_t>&)>& encode){
    using Json=nlohmann::json;auto result=catalog();result["version"]=2;
    const auto folder=pluginRoot/"widget/characters/asterlan";
    const auto override=widget.value("animationPack",std::string{});result["configured_override"]=override;result["override_preset"]="custom";
    if(!override.empty()){std::error_code error;const auto selected=std::filesystem::weakly_canonical(std::filesystem::u8path(override),error);if(!error&&selected==std::filesystem::weakly_canonical(folder,error)&&!error)result["override_preset"]="asterlan";}
    try{
        const auto pack=character::loadPack(folder);Json poses=Json::array();
        for(const auto* state:{"idle","walk","resting","speaking"}){
            const auto& clip=pack->states.at(pack->resolve(state))[0].loop;const auto& ref=clip.frames[0];
            const auto path=pack->files.at(ref.image);const auto bytes=std::filesystem::file_size(path);if(bytes>512*1024)throw std::runtime_error("preview_size");
            std::ifstream input(path,std::ios::binary);std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});const auto encoded=encode(data);if(encoded.empty())throw std::runtime_error("preview_encoding");
            poses.push_back({{"state",state},{"image","data:image/png;base64,"+encoded},{"canvas",Json::array({pack->width,pack->height})},{"rect",Json::array({ref.x,ref.y,ref.w,ref.h})}});
        }
        result["skins"].push_back({{"id","asterlan"},{"label","爱丝特兰（小爱）"},{"original_palette",true},{"renderer","png_sequence"},{"poses",std::move(poses)}});
    }catch(...){result["sequence_preview_issue"]="asterlan_preview_unavailable";}
    return result;
}
}
