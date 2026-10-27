#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace hibernation::personal {
using EvidenceJson=nlohmann::json;
// Identity/weight describe provenance, not whether the model's interpretation
// is correct. They are program-owned and cannot be supplied in a tool proposal.
inline EvidenceJson anchor(std::string type,std::string identity,double weight){
    if(identity.empty()||identity.size()>2048)return nullptr;
    return {{"key",type+":"+identity},{"type",std::move(type)},{"weight",weight}};
}
inline EvidenceJson terminalAnchor(const EvidenceJson& record,const EvidenceJson& source){
    try{
    const auto kind=record.value("kind",std::string{}),tool=record.value("tool",std::string{});
    const auto at=record.value("created_at",std::int64_t(0));
    if(kind=="user_message"&&at>0){auto value=anchor("conversation",std::to_string(at),.5);value["observed_ms"]=at;return value;}
    if(kind=="story_end")return anchor("story",source.at("id"),.75);
    const auto result=record.value("result",EvidenceJson::object());
    if(kind=="tool_attempt"&&result.is_object()&&result.value("attempted",false)&&!result.value("succeeded",true)){
        const auto type=result.value("target_kind",std::string{});if(type!="authored_piece"&&type!="puzzle")return nullptr;
        return anchor(type,result.value("target",EvidenceJson::object()).value("id",std::string{}),.25);
    }
    if((kind=="tool_observation"||kind=="tool_intent")&&result.is_object()){
        const auto game=result.value("game",EvidenceJson::object());
        if(tool.starts_with("pocket.")&&game.is_object()&&game.contains("id"))return anchor("puzzle",game.at("id"),result.value("applied",false)?1:.5);
        // accepted/queued plans are not another completed experience.
        if(kind=="tool_intent")return nullptr;
        const auto object=result.value("object",EvidenceJson::object());
        if(tool=="habitat.create"&&object.is_object()&&object.contains("id"))return anchor("desk_object",object.at("id"),1);
        const auto piece=result.value("piece",EvidenceJson::object());
        if((tool=="atelier.write"||tool=="atelier.append")&&result.value("written",false)&&piece.is_object()&&piece.contains("id"))return anchor("authored_piece",piece.at("id"),1);
        if(tool=="reading.open"&&(result.value("scope",std::string{})=="article_lead_summary"||result.value("scope",std::string{})=="article_source_chunk")&&!result.value("text",std::string{}).empty())return anchor("reading",result.value("reading_identity",result.value("source_url",std::string{})),1);
        if((tool=="workspace.write"||tool=="workspace.edit")&&result.value("written",false))return anchor("draft",result.value("path",std::string{}),1);
        return nullptr; // Queries/internal state do not manufacture experiences.
    }
    if(kind=="outer_event"){
        const auto event=record.value("observation",EvidenceJson::object());
        const auto name=event.value("kind",std::string{});
        const auto game=event.value("game",EvidenceJson::object());
        if(name.starts_with("pocket.")&&game.is_object()&&game.contains("id"))return anchor("puzzle",game.at("id"),1);
        const auto object=event.value("item",EvidenceJson::object());
        if(name.starts_with("habitat.")&&object.is_object()&&object.contains("id"))return anchor("desk_object",object.at("id"),1);
        const auto piece=event.value("piece",EvidenceJson::object());
        if(name.starts_with("atelier.")&&piece.is_object()&&piece.contains("id"))return anchor("authored_piece",piece.at("id"),1);
        if(name=="reading.observed"){const auto reference=event.value("reference",EvidenceJson::object());return anchor("reading",reference.value("reading_identity",reference.value("source_url",std::string{})),1);}
        // Anonymous motion/pose and service messages do not consolidate taste.
        return nullptr;
    }
    if(kind=="task_complete")return anchor("task",source.at("id"),.75);
    return nullptr;
    }catch(...){return nullptr;} // Malformed metadata earns no confidence.
}
}
