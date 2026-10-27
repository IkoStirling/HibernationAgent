#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <set>
namespace hibernation {
inline bool revisitableObservationTool(const std::string& tool){
    static const std::set<std::string> concrete={"workspace.list","workspace.read","workspace.view_image","computer.screenshot","computer.list_directory","television.observe","television.observe_sequence","television.search","reading.search","reading.open","atelier.read","napcat.view_image"};return concrete.contains(tool);
}
inline bool continuityObservationTool(const std::string& tool){
    static const std::set<std::string> effects={"workspace.write","workspace.edit","computer.open_application","television.open","television.open_page","television.play","television.pause","television.stop","atelier.write","atelier.append","habitat.create","habitat.act","habitat.arrange","pocket.start","pocket.press","pocket.leave","rhythm.rest","rhythm.cancel"};
    return revisitableObservationTool(tool)||effects.contains(tool);
}
inline std::string observedImageDelivery(const nlohmann::json& trace,const std::string& sourceId){
    std::string state="no_saved_receipt";
    for(const auto& visual:trace.value("visual_inputs",nlohmann::json::array()))if(visual.value("source_id",std::string{})==sourceId){
        if(!visual.value("attached",false)){if(state=="no_saved_receipt")state="not_attached";continue;}
        const auto delivery=visual.value("delivery_state",std::string{});
        if(delivery=="acknowledged")return delivery;
        if(delivery=="prepared")state="prepared_only";
        else if(!delivery.empty())state=delivery=="attempted"?"unknown":delivery;
        else if(trace.value("http_status",0)>=200&&trace.value("http_status",0)<300)return "acknowledged";
        else state="legacy_attachment_only";
    }
    return state;
}
inline nlohmann::json referencedObservationContext(nlohmann::json record){
    using Json=nlohmann::json;const auto kind=record.value("kind",std::string{});
    if((kind!="tool_observation"&&kind!="tool_intent")||!record.contains("result")||!record.at("result").is_object())return record;
    bool removed=false;auto& result=record["result"];
    const auto strip=[&](Json& holder){if(!holder.is_object()||!holder.contains("image"))return;holder.erase("image");holder["image_available_in_source"]=true;holder["image_content_in_context"]=false;removed=true;};
    strip(result);if(result.contains("frames")&&result.at("frames").is_array())for(auto& frame:result["frames"])strip(frame);
    if(removed)record["context_representation"]="observation_metadata_without_pixels";
    return record;
}
// A previous receipt proves what happened, without replaying the entire tool
// transcript, granting old permissions, or carrying pixels into every turn.
inline nlohmann::json compactObservation(const nlohmann::json& record,bool imageDelivered,const std::string& deliveryState={}){
    using Json=nlohmann::json;Json result={{"kind",record.at("kind")},{"tool",record.at("tool")},{"created_at",record.value("created_at",std::int64_t(0))},{"context_role","previous_tool_observation"}};
    const auto& data=record.at("result");Json summary=Json::object();
    if(record.at("tool")=="mentor.execute"){
        for(const auto* field:{"delegated","operations_executed","operations_dispatched","operations_may_have_executed","steps_attempted","steps_succeeded","all_steps_succeeded","has_pending_effects","cancelled","task_completion"})if(data.contains(field)&&data.at(field).is_primitive())summary[field]=data.at(field);
        summary["receipts"]=Json::array();for(const auto& step:data.value("receipts",Json::array())){if(summary.at("receipts").size()>=4)break;const auto& receipt=step.at("receipt");auto meta=Json{{"tool",step.at("tool")},{"ok",receipt.value("ok",false)},{"source_id",receipt.value("source_id",std::string{})}};if(receipt.contains("error"))meta["error"]=receipt.at("error");if(receipt.contains("result"))meta["result"]=compactObservation(Json{{"kind","tool_observation"},{"tool",step.at("tool")},{"result",receipt.at("result")}},false).at("result");summary["receipts"].push_back(std::move(meta));}
    }
    for(const auto* field:{"source_mount","source_path","width","height","original_width","original_height","frame_index","source_frame_count","written","completed","accepted","queued","opened","state","mode","frame_time_seconds","position_seconds","title","path","id","phase","changed","scope","source_url","article_url","provider","revision","retrieved_ms","full_article_read","text_truncated","cache_hit","language"})if(data.contains(field)&&data.at(field).is_primitive()){
        const auto& value=data.at(field);if(value.is_string()&&value.get_ref<const std::string&>().size()>512)continue;summary[field]=value;
    }
    if(data.contains("piece")&&data.at("piece").is_object()){summary["piece"]=Json::object();for(const auto* field:{"id","title","revision","kind","stage"})if(data.at("piece").contains(field))summary["piece"][field]=data.at("piece").at(field);}
    for(const auto* field:{"sampled_frames","span_seconds","stop_reason","full_video_watched","audio_delivered"})if(data.contains(field)&&data.at(field).is_primitive())summary[field]=data.at(field);
    if(data.contains("frames")&&data.at("frames").is_array()){
        summary["frames"]=Json::array();for(const auto& frame:data.at("frames")){if(summary.at("frames").size()>=3)break;Json meta=Json::object();for(const auto* field:{"index","frame_time_seconds","observed_at_ms"})if(frame.contains(field)&&frame.at(field).is_number())meta[field]=frame.at(field);summary["frames"].push_back(std::move(meta));}
        summary["images_delivered_to_model"]=imageDelivered;
    }
    if(data.contains("image"))summary["image_delivered_to_model"]=imageDelivered;
    if(!deliveryState.empty()&&(summary.contains("image_delivered_to_model")||summary.contains("images_delivered_to_model"))){
        summary["image_delivery_status"]=deliveryState;
        if(deliveryState=="unknown"||deliveryState=="no_saved_receipt"||deliveryState=="legacy_attachment_only"){summary.erase("image_delivered_to_model");summary.erase("images_delivered_to_model");}
    }
    result["result"]=std::move(summary);return result;
}
}
