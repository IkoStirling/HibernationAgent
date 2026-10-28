#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace hibernation {
inline bool pixelMutationTool(const std::string& name){
    return name=="pixel.create"||name=="pixel.edit"||name=="pixel.palette"||name=="pixel.frame"||name=="pixel.undo"||name=="pixel.redo"||name=="pixel.export"||name=="pixel.show";
}
inline bool pixelObservationTool(const std::string& name){return name=="pixel.read"||name=="pixel.preview"||name=="pixel.list";}
inline bool pixelPresentationCheckpoint(const nlohmann::json& event){
    if(!event.is_object()||event.value("plugin",std::string{})!="pixel-canvas")return false;
    const auto kind=event.value("kind",std::string{});
    return kind=="pixel.displayed"||kind=="pixel.hidden"||kind=="pixel.moved"||kind=="pixel.display_failed"||kind=="pixel.display_cancelled";
}
inline nlohmann::json pixelDocumentContext(const nlohmann::json& document,bool includePalette=false){
    using Json=nlohmann::json;Json value=Json::object();if(!document.is_object())return value;
    for(const auto* key:{"id","title","width","height","revision","created_ms","updated_ms","content_kind"})if(document.contains(key)&&document.at(key).is_primitive())value[key]=document.at(key);
    if(includePalette&&document.contains("palette")&&document.at("palette").is_array()&&document.at("palette").size()<=32)value["palette"]=document.at("palette");
    if(document.contains("frames")&&document.at("frames").is_array()){
        value["frame_count"]=document.at("frames").size();value["frames"]=Json::array();
        for(const auto& frame:document.at("frames")){if(value.at("frames").size()>=8)break;Json entry={{"index",value.at("frames").size()}};if(frame.is_object()&&frame.contains("duration_ms")&&frame.at("duration_ms").is_number_integer())entry["duration_ms"]=frame.at("duration_ms");value["frames"].push_back(std::move(entry));}
    }
    value["content_kind"]="authored_pixel_art";return value;
}
// Operator tools retain the complete editable document. The character receives
// bounded structure and can request an actual preview image; raw grids are not
// copied into every model turn or silently interpreted as real-world events.
inline nlohmann::json pixelCharacterReceipt(nlohmann::json receipt){
    if(receipt.is_object()&&receipt.contains("result")&&receipt.at("result").is_object()){
        auto& value=receipt["result"];
        if(value.contains("document"))value["document"]=pixelDocumentContext(value.at("document"),true);
    }
    return receipt;
}
}
