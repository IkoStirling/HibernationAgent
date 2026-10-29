#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <string>
#include "Hibernation/ObservationContext.h"

namespace hibernation {
// Read-only semantic views of known world receipts. Internal diagnostics and
// encoded pixels are not library materials. Original records remain intact.
inline std::string experienceExcerpt(const std::string& text,std::size_t limit){
    auto end=std::min(text.size(),limit);while(end>0&&end<text.size()&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;return text.substr(0,end);
}
inline nlohmann::json archiveWorldExperience(const nlohmann::json& record){
    using Json=nlohmann::json;
    if(!record.contains("result")||!record.at("result").is_object())return nullptr;
    const auto tool=record.value("tool",std::string{});
    const auto& data=record.at("result");const auto kind=record.value("kind",std::string{});
    const bool legacyApplied=legacyAppliedPuzzleRecord(record);
    if(kind!="tool_observation"&&!legacyApplied)return nullptr;
    static const std::set<std::string> concrete={"pocket.start","pocket.press","pocket.leave","pocket.observe","atelier.write","atelier.append","atelier.read","reading.open","workspace.read","workspace.write","workspace.edit","habitat.create","habitat.act","habitat.arrange","habitat.observe","canteen.observe","workspace.view_image","computer.screenshot","television.observe","television.observe_sequence"};
    if(!concrete.contains(tool)&&!pixelMutationTool(tool)&&!pixelObservationTool(tool))return nullptr;
    Json facts=Json::object();
    if(tool=="canteen.observe"){for(const auto* field:{"job","shelf","recent","ready_count","delivery_pending"})if(data.contains(field))facts[field]=data.at(field);}
    if(data.contains("document")&&data.at("document").is_object())facts["document"]=pixelDocumentContext(data.at("document"));
    const auto copy=[&](Json& into,const Json& from,std::initializer_list<const char*> fields){for(const auto* field:fields)if(from.contains(field)&&from.at(field).is_primitive()){
        const auto& value=from.at(field);into[field]=value.is_string()?Json(experienceExcerpt(value.get_ref<const std::string&>(),512)):value;
    }};
    copy(facts,data,{"written","applied","exported","completed","accepted","queued","opened","scope","source_mount","source_path","path","title","source_url","article_url","revision","retrieved_ms","full_article_read","text_truncated","content_kind","detail","content_model","source_total_bytes","source_truncated","reading_identity","offset","next_offset","total_bytes","eof","content_sha256","sampled_frames","span_seconds","full_video_watched","audio_delivered","frame_index","source_frame_count","position_seconds","frame_time_seconds"});
    for(const auto* field:{"piece","game","object"})if(data.contains(field)&&data.at(field).is_object()){
        Json value=Json::object();copy(value,data.at(field),{"id","title","revision","kind","stage","status","lit_count","moves","moves_remaining","limit","name","phase","action","x","y"});
        if(std::string_view(field)=="game"&&data.at(field).contains("rows")){
            const auto& rows=data.at(field).at("rows");bool valid=rows.is_array()&&rows.size()==3;
            if(valid)for(const auto& row:rows){if(!row.is_array()||row.size()!=3){valid=false;break;}for(const auto& cell:row)if(!cell.is_boolean()){valid=false;break;}}
            if(valid)value["rows"]=rows;
        }
        facts[field]=std::move(value);
    }
    if(tool=="habitat.observe"&&data.contains("objects")&&data.at("objects").is_array()){
        facts["objects"]=Json::array();for(const auto& object:data.at("objects")){if(facts["objects"].size()>=24)break;if(!object.is_object())continue;Json value=Json::object();copy(value,object,{"id","kind","name","phase","x","y"});facts["objects"].push_back(std::move(value));}
    }
    if(data.contains("image")||data.contains("frames")){facts["image_content_in_context"]=false;facts["visual_revisit_required"]=true;}
    // Exact text is separate from bounded metadata. A historical image receipt
    // is not another image delivery. Failed writes never enter this branch.
    std::string text,scope;
    if(tool=="reading.open"){text=data.value("text",std::string{});scope=data.value("scope",std::string{})=="article_source_chunk"?"observed_article_source_chunk":"observed_article_excerpt";}
    else if(tool=="workspace.read"){text=data.value("content",std::string{});scope="observed_file_chunk";}
    else if(tool=="atelier.read"){text=data.value("piece",Json::object()).value("content",std::string{});scope="observed_authored_piece";}
    else if((tool=="atelier.write"||tool=="atelier.append")&&data.value("written",false)&&record.contains("written_content")&&record.at("written_content").is_string()){
        text=record.at("written_content").get<std::string>();scope=tool=="atelier.append"?"applied_append_fragment":"applied_whole_piece";
    }
    Json view={{"kind",kind},{"tool",tool},{"created_at",record.value("created_at",std::int64_t(0))},{"context_role","historical_world_receipt"},{"result",facts},{"text",tool+"\n"+facts.dump()}};
    if(legacyApplied){view["recorded_kind"]="tool_intent";view["legacy_applied_puzzle"]=true;view["text"]=view.at("text").get<std::string>()+"\nLegacy classification: applied=true confirms the individual operation happened; completed indicates whether the whole puzzle was solved. The original record is unchanged.";}
    if(!scope.empty()){view["observed_text_scope"]=scope;view["observed_text_bytes"]=text.size();view["observed_text_clipped"]=text.size()>60000;view["text"]=view.at("text").get<std::string>()+"\n"+experienceExcerpt(text,60000);}
    view["historical_only"]=true;return view;
}
}
