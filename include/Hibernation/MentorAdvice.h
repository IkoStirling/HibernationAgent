#pragma once
#include <AYSerializer/JsonSchema.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <string>
namespace hibernation::mentor {
using Json=nlohmann::json;
inline bool operational(const std::string& name){
    for(const auto* prefix:{"widget.","habitat.","television.","workspace.","computer.","reading.","atelier.","pocket.","activity.","body.","rhythm."})if(name.starts_with(prefix))return true;
    return false;
}
inline bool pendingEffect(const Json& result){
    if(!result.is_object())return false;
    for(const auto* field:{"queued","pending","scheduled"})if(result.contains(field)){const auto& value=result.at(field);if((value.is_boolean()&&value.get<bool>())||(value.is_number()&&value.get<double>()>0)||((value.is_array()||value.is_object())&&!value.empty()))return true;}
    if(result.contains("state")&&result.at("state").is_string()&&result.at("state")=="pending")return true;
    return result.contains("completed")&&result.at("completed").is_boolean()&&!result.at("completed").get<bool>();
}
inline Json schema(){
    const Json text={{"type","string"}};
    return {{"type","object"},{"additionalProperties",false},{"required",Json::array({"status","summary","steps","missing_capability"})},{"properties",{
        {"status",{{"type","string"},{"enum",Json::array({"advice","blocked"})}}},{"summary",text},{"missing_capability",text},
        {"steps",{{"type","array"},{"items",{{"type","object"},{"additionalProperties",false},{"required",Json::array({"instruction","tool","arguments_json"})},{"properties",{{"instruction",text},{"tool",text},{"arguments_json",text}}}}}}}
    }}};
}
// No substring extraction, unbounded repair, tool execution, or authority upgrade.
inline std::optional<Json> validate(const std::string& text,const Json& available){
    if(text.size()>16000||!ayt::serializer::validateJsonDocument(text,schema().dump()))return {};
    auto value=Json::parse(text);if(value.at("summary").get_ref<const std::string&>().size()>2400||value.at("missing_capability").get_ref<const std::string&>().size()>800||value.at("steps").size()>4)return {};
    if(value.at("status")=="blocked"&&!value.at("steps").empty())return {};
    Json proposals=Json::array();for(auto& step:value["steps"]){
        const auto name=step.at("tool").get<std::string>(),argsText=step.at("arguments_json").get<std::string>();
        if(step.at("instruction").get_ref<const std::string&>().size()>1200||argsText.size()>4096)return {};
        if(name.empty()){if(argsText!="{}")return {};continue;}
        if(!operational(name))return {};const auto tool=std::find_if(available.begin(),available.end(),[&](const auto& t){return t.value("name",std::string{})==name;});
        if(tool==available.end()||!ayt::serializer::validateJsonDocument(argsText,tool->at("parameters").dump()))return {};
        proposals.push_back({{"tool",name},{"args",Json::parse(argsText)},{"instruction",step.at("instruction")}});
    }
    value["proposals"]=std::move(proposals);value["advisory_only"]=true;value["operations_executed"]=false;
    value["guide"]="这是导师的建议，不是已经完成的操作、记忆事实或新的权限。由你选择后通过现有工具执行，按真实回执判断结果。";return value;
}
inline std::optional<std::string> finalMessage(const std::string& events){
    std::optional<std::string> answer;bool completed=false,failed=false;std::size_t start=0;
    while(start<events.size()){
        auto end=events.find('\n',start);if(end==std::string::npos)end=events.size();const auto line=events.substr(start,end-start);start=end+1;
        auto row=Json::parse(line,nullptr,false);if(!row.is_object())continue;const auto type=row.value("type",std::string{});
        if(type=="turn.failed"||type=="error")failed=true;if(type=="turn.completed")completed=true;
        if(type=="item.completed"&&row.contains("item")&&row.at("item").is_object()){
            const auto& item=row.at("item");if(item.value("type",std::string{})=="agent_message"&&item.contains("text")&&item.at("text").is_string())answer=item.at("text").get<std::string>();
            // The mentor is advice-only. Treat unexpected local execution as a failed bridge.
            if(item.value("type",std::string{})=="command_execution"||item.value("type",std::string{})=="file_change"||item.value("type",std::string{})=="mcp_tool_call")failed=true;
        }
    }return completed&&!failed?answer:std::nullopt;
}
}
