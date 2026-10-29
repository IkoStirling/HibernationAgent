#pragma once
#include <Hibernation/ModelEmbodiment.h>
#include <Hibernation/PixelContext.h>
#include <string_view>
namespace hibernation {
// Model context describes the character's usable world. Rendering telemetry and
// cache counters remain in the unchanged operator diagnostics, not its attention.
inline nlohmann::json pluginCharacterContext(std::string_view id,const nlohmann::json& saved){
    using Json=nlohmann::json;if(!saved.is_object())return saved;
    if(id=="style-lora")return Json::object();
    if(id=="widget")return widgetBodyContext(saved);
    Json view=Json::object();const auto take=[&](std::initializer_list<const char*> fields){for(const auto* field:fields)if(saved.contains(field))view[field]=saved.at(field);};
    if(id=="pixel-canvas"){
        take({"document_count","maximum","can_create","content_kind","guide"});
        const auto surface=saved.value("surface",Json::object());view["surface"]=Json::object();
        for(const auto* field:{"desktop","visible","pending","document_id","revision","frame","scale","paused","reduced_motion"})if(surface.contains(field))view["surface"][field]=surface.at(field);
        view["documents"]=Json::array();for(const auto& document:saved.value("documents",Json::array())){if(view["documents"].size()>=6)break;view["documents"].push_back(document);}
        view["authorship_note"]="目录中的作品也可能由用户编辑；自己实际调用工具后的成功写入回执才证明自己完成了相应版本。";return view;
    }
    if(id=="canteen")return saved.value("current",Json::object());
    if(id=="habitat")take({"objects","pending","appetite","visible","desktop","creation_policy"});
    else if(id=="television")take({"mode","title","source","state","position_seconds","duration_seconds","visible","audio_available","page_platform","page_url","observation","error"});
    else if(id=="tts")take({"active","synthesizing","queued_replies"});
    else if(id=="rhythm")take({"phase","duration","elapsed","origin","local_hour"});
    else if(id=="reading")take({"recent","last_search","last_read","autonomous_allowed","language","requests_used","request_limit","network_scope","automatic_network"});
    else if(id=="mentor")take({"backend","executable_available","authorization","advisory_only","execution_enabled","execution_scope","automatic_consultation","last_issue"});
    else return saved; // Unknown plugins retain their published contract.
    return view;
}
// Ordinary conversation needs a background index, not a repeated invitation to
// narrate every unchanged object, board and draft. Explicit observation and
// private planning continue to receive the complete published character view.
inline nlohmann::json pluginDialogueContext(std::string_view id,const nlohmann::json& current){
    using Json=nlohmann::json;if(!current.is_object())return current;
    Json view=Json::object();const auto take=[&](std::initializer_list<const char*> fields){for(const auto* field:fields)if(current.contains(field))view[field]=current.at(field);};
    if(id=="canteen"){take({"job","ready_count","shelf_capacity","can_cook","delivery_pending","automatic"});view["details_tool"]="canteen.observe";
    }else if(id=="habitat"){
        take({"pending","appetite","visible","desktop","creation_policy"});
        view["object_count"]=current.value("objects",Json::array()).size();view["details_tool"]="habitat.observe";
    }else if(id=="pixel-canvas"){
        take({"document_count","maximum","can_create","surface"});view["details_tool"]="pixel.list";view["preview_tool"]="pixel.preview";
    }else if(id=="atelier"){
        take({"piece_count","can_create","maximum","content_kind"});view["details_tool"]="atelier.list";view["read_tool"]="atelier.read";
    }else if(id=="pocket"){
        const auto state=current.value("current",current);const auto game=state.is_object()?state.value("game",Json(nullptr)):Json(nullptr);
        view["game"]=Json(nullptr);if(game.is_object()){view["game"]=Json::object();for(const auto* field:{"id","revision","status","moves","limit","lit_count","moves_remaining"})if(game.contains(field))view["game"][field]=game.at(field);}
        view["details_tool"]="pocket.observe";
    }else return current;
    view["context_detail"]="ambient_index";return view;
}
}
