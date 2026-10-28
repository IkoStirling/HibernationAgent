#pragma once
#include <AYInference/Inference.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
namespace hibernation {
// Application policy, independent of providers/prices. A future pricing plugin
// can choose a preset without replacing the actor/reviewer/tool pipeline.
struct ContextControl {
    bool enabled=true,reviewReadOnlyTools=false,reviewSilence=false,imageOnce=true,sessionProjection=true;
    std::string preset="balanced";
    std::size_t historyBytes=12000,reviewHistoryBytes=6000,oldReceiptBytes=4096;
    std::size_t protocolRepairs=1,reviewAttempts=2;
    std::size_t sessionBytes=131072,sessionMaxTurns=16;
    static ContextControl read(const nlohmann::json& value){
        using Json=nlohmann::json;ContextControl out;
        if(!value.is_object())throw std::runtime_error("context_control_object");
        const std::set<std::string> fields={"enabled","preset","historyBytes","reviewHistoryBytes","oldReceiptBytes","reviewReadOnlyTools","reviewSilence","imageOnce","protocolRepairs","reviewAttempts","sessionProjection","sessionBytes","sessionMaxTurns"};
        for(auto it=value.begin();it!=value.end();++it)if(!fields.contains(it.key()))throw std::runtime_error("context_control_field");
        auto boolean=[&](const char* key,bool fallback){if(!value.contains(key))return fallback;if(!value.at(key).is_boolean())throw std::runtime_error("context_control_boolean");return value.at(key).get<bool>();};
        if(value.contains("preset")){if(!value.at("preset").is_string())throw std::runtime_error("context_control_preset");out.preset=value.at("preset");}
        if(out.preset=="economy"){out.historyBytes=7000;out.reviewHistoryBytes=3000;out.oldReceiptBytes=2048;out.reviewAttempts=1;}
        else if(out.preset!="balanced"&&out.preset!="full")throw std::runtime_error("context_control_preset");
        auto number=[&](const char* key,std::size_t fallback,std::size_t min,std::size_t max){if(!value.contains(key))return fallback;const auto& x=value.at(key);if(!x.is_number_integer()||x.get<double>()<static_cast<double>(min)||x.get<double>()>static_cast<double>(max))throw std::runtime_error("context_control_range");return x.get<std::size_t>();};
        out.enabled=boolean("enabled",true)&&out.preset!="full";
        out.historyBytes=number("historyBytes",out.historyBytes,2048,262144);
        out.reviewHistoryBytes=number("reviewHistoryBytes",out.reviewHistoryBytes,1024,262144);
        out.oldReceiptBytes=number("oldReceiptBytes",out.oldReceiptBytes,512,65536);
        out.sessionProjection=boolean("sessionProjection",true)&&out.enabled;
        out.sessionBytes=number("sessionBytes",out.preset=="economy"?65536:131072,16384,524288);
        out.sessionMaxTurns=number("sessionMaxTurns",16,2,64);
        out.reviewReadOnlyTools=boolean("reviewReadOnlyTools",false);out.reviewSilence=boolean("reviewSilence",false);out.imageOnce=boolean("imageOnce",true);
        out.protocolRepairs=number("protocolRepairs",out.protocolRepairs,0,2);out.reviewAttempts=number("reviewAttempts",out.reviewAttempts,1,2);
        if(!out.enabled){out.reviewReadOnlyTools=true;out.reviewSilence=true;out.imageOnce=false;out.protocolRepairs=2;out.reviewAttempts=2;}
        return out;
    }
    nlohmann::json describe()const{return {{"sessionProjection",sessionProjection},{"sessionBytes",sessionBytes},{"sessionMaxTurns",sessionMaxTurns},{"enabled",enabled},{"preset",preset},{"historyBytes",historyBytes},{"reviewHistoryBytes",reviewHistoryBytes},{"oldReceiptBytes",oldReceiptBytes},{"reviewReadOnlyTools",reviewReadOnlyTools},{"reviewSilence",reviewSilence},{"imageOnce",imageOnce},{"protocolRepairs",protocolRepairs},{"reviewAttempts",reviewAttempts}};}
    std::size_t historyLimit(std::size_t legacy,std::size_t required)const{return enabled?std::min(legacy,std::max(historyBytes,required)):legacy;}
};
inline bool readOnlyModelTool(const std::string& name){
    // Explicit allowlist. Unknown and new plugin tools retain semantic review.
    static const std::set<std::string> names={"capabilities.list","workspace.list","workspace.read","workspace.view_image","computer.list_directory","computer.screenshot","observation.revisit","memory.search","memory.read","memory.archive_search","memory.archive_read","state.observe","stage.observe","body.observe","schedule.list","activity.status","widget.status","television.status","television.observe","television.observe_sequence","television.search","napcat.view_image"};
    return name=="self.observe"||name=="habitat.observe"||name=="atelier.list"||name=="atelier.read"||name=="pocket.observe"||names.contains(name);
}
inline bool readOnlyModelBatch(const nlohmann::json& candidate){
    if(candidate.value("kind",std::string{})!="call"||!candidate.contains("calls")||candidate.at("calls").empty())return false;
    for(const auto& call:candidate.at("calls"))if(!readOnlyModelTool(call.value("tool",std::string{})))return false;return true;
}
inline std::string contextExcerpt(const std::string& text,std::size_t limit){
    if(text.size()<=limit)return text;auto end=limit;while(end&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;return text.substr(0,end);
}
inline nlohmann::json priorReceiptContext(const nlohmann::json& value,std::size_t limit){
    using Json=nlohmann::json;if(value.dump().size()<=limit)return value;
    Json out=Json::object();for(const auto* key:{"ok","call_id","source_id","error","reused_after_review","deduplicated","guidance"})if(value.contains(key))out[key]=value.at(key);
    out["context_compacted"]=true;out["original_bytes"]=value.dump().size();
    if(value.contains("result")&&value.at("result").is_object()){
        Json facts=Json::object();for(const auto* key:{"id","revision","sent","written","completed","accepted","queued","opened","changed","state","target","width","height","capture_rectangle","region","recipient","type","image_attached","vision_available","image_available_in_source","image_delivery_status","delivery_status","receipt"})if(value.at("result").contains(key))facts[key]=value.at("result").at(key);
        out["result"]=facts;
    }
    // A marked excerpt is data, never a successful observation or a new source.
    const auto bytes=out.dump().size();if(bytes+128<limit){
        out["data_excerpt"]=contextExcerpt(value.dump(),limit-bytes-128);
        while(out.dump().size()>limit&&!out.at("data_excerpt").get_ref<const std::string&>().empty())out["data_excerpt"]=contextExcerpt(out.at("data_excerpt"),out.at("data_excerpt").get_ref<const std::string&>().size()/2);
    }
    return out;
}
inline nlohmann::json reviewEventContext(const std::string& text,const nlohmann::json& candidate,std::size_t budget){
    using Json=nlohmann::json;auto event=Json::parse(text,nullptr,false);if(!event.is_object())return Json{{"context_unavailable",true}};
    if(!event.contains("authorized_context")||!event.at("authorized_context").is_array())return event;
    const auto all=event.at("authorized_context");Json selected=Json::array(),omitted=Json::array();std::size_t used=0;std::set<std::size_t> admitted;
    const auto mentions=[&](const Json& node,const std::string& id,auto&& self)->bool{if(node.is_string())return node.get_ref<const std::string&>()==id;if(node.is_structured())for(const auto& child:node)if(self(child,id,self))return true;return false;};
    for(std::size_t i=0;i<all.size();++i){const auto& row=all.at(i);const auto id=row.value("id",std::string{});if(id==event.value("event_id",std::string{})||mentions(candidate,id,mentions)){admitted.insert(i);used+=row.dump().size();}}
    for(std::size_t i=all.size();i-->0;){if(admitted.contains(i))continue;const auto size=all.at(i).dump().size();if(used+size<=budget){admitted.insert(i);used+=size;}}
    // Select newest background evidence first, but present it in the actor's
    // original order. Reversing dialogue places corrections before the mistake.
    for(std::size_t i=0;i<all.size();++i)if(admitted.contains(i))selected.push_back(all.at(i));else omitted.push_back(all.at(i).value("id",std::string{}));
    event["authorized_context"]=selected;event["review_context_omitted_ids"]=omitted;return event;
}
inline void compactPriorToolMessages(std::vector<ayt::inference::Message>& messages,std::size_t limit){
    using Json=nlohmann::json;for(auto& message:messages){
        if(message.role!="tool"&&message.role!="user")continue;
        auto value=Json::parse(message.text,nullptr,false);if(!value.is_object())continue;
        if(message.role=="tool")message.text=priorReceiptContext(value,limit).dump();
        else if(value.contains("tool_results")&&value.at("tool_results").is_array()){
            for(auto& receipt:value["tool_results"])receipt=priorReceiptContext(receipt,limit);message.text=value.dump();
        }
    }
}
inline nlohmann::json modelRequestFootprint(const ayt::inference::Request& request){
    std::size_t text=0,images=0,imageBytes=0,tools=0;for(const auto& message:request.messages){text+=message.text.size();for(const auto& call:message.toolCalls)text+=call.name.size()+call.arguments.size();for(const auto& image:message.images){++images;imageBytes+=image.dataUrl.size();}}
    for(const auto& tool:request.tools)tools+=tool.name.size()+tool.description.size()+tool.parameters.size();
    return {{"text_bytes",text},{"tool_schema_bytes",tools},{"output_schema_bytes",request.outputSchema.size()},{"image_count",images},{"image_uri_bytes",imageBytes},{"token_estimate",nullptr}};
}
}
