#pragma once
#include <Hibernation/TurnProtocol.h>
#include <AYInference/Inference.h>
#include <AYSerializer/JsonSchema.h>
#include <map>
#include <algorithm>
namespace hibernation {
inline std::string nativeToolName(std::string_view value){
    std::string result="ha_";for(const unsigned char c:value){if(c=='.')result+="_DOT_";else if(c=='_')result+="_US_";else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')result+=static_cast<char>(c);else return {};}
    return result.size()<=64?result:std::string{};
}
// Null for a known optional field means omission in the existing application
// protocol. Declare that wire compatibility explicitly, then normalize against
// the original schema before review; unknown and required fields stay strict.
inline nlohmann::json nativeArgumentSchema(nlohmann::json schema){
    using Json=nlohmann::json;
    if(schema.contains("anyOf"))for(auto& branch:schema["anyOf"])branch=nativeArgumentSchema(std::move(branch));
    if(schema.contains("properties")){
        const auto required=schema.value("required",Json::array());
        for(auto property=schema["properties"].begin();property!=schema["properties"].end();++property){
            auto child=nativeArgumentSchema(property.value());
            if(std::find(required.begin(),required.end(),property.key())==required.end()&&!ayt::serializer::validateJsonDocument("null",child.dump()))child={{"anyOf",Json::array({child,{{"type","null"}}})}};
            property.value()=std::move(child);
        }
    }
    if(schema.contains("items"))schema["items"]=nativeArgumentSchema(schema.at("items"));return schema;
}
struct NativeToolBridge {
    static nlohmann::json finalReplyParameters(){
        const auto turn=nlohmann::json::parse(turnSchema());
        // The cross-field text/messages requirement is checked by validateTurn.
        // Keep a flat schema so optional-null normalization applies to all fields.
        return {{"type","object"},{"additionalProperties",false},{"properties",{{"version",{{"type","integer"},{"enum",nlohmann::json::array({1})}}},{"kind",{{"enum",nlohmann::json::array({"reply"})}}},{"calls",{{"type","array"},{"maxItems",0}}},{"text",turn.at("properties").at("text")},{"messages",replyMessagesSchema()},{"presentation",turn.at("properties").at("presentation")},{"carry_forward",personalCarrySchema()},{"appraisal",appraisalParameters()},{"feeling",feelingProposalParameters()}}}};
    }
    static nlohmann::json finalSilenceParameters(){return {{"type","object"},{"properties",{{"version",{{"type","integer"},{"enum",nlohmann::json::array({1})}}},{"kind",{{"enum",nlohmann::json::array({"silence"})}}},{"calls",{{"type","array"},{"maxItems",0}}},{"text",{{"type","null"}}},{"carry_forward",personalCarrySchema()},{"appraisal",appraisalParameters()},{"feeling",feelingProposalParameters()}}},{"additionalProperties",false}};}
    static std::vector<ayt::inference::ToolDefinition> finalDefinitions(){
        return {{"ha_turn_reply","提交本轮最终对白。可自行选择text一条完整回复或messages的1至6条消息；使用messages时text可为null，保留自己选择的消息边界，不为凑条数扩写。直接写对对方说的台词，普通交谈不加舞台旁白或整段扮演引号；明确请求作品时按所需文体。可选carry_forward私下留有来源的持续关注，appraisal表达对实际经历的当下感受；均可省略。整组校验和审核后才显示，不与操作提案同批。",nativeArgumentSchema(finalReplyParameters()).dump(),false},
                {"ha_turn_silence","选择本轮不发言；不代表发生了任何动作。可选carry_forward仅留下一点有来源的持续关注，appraisal表达对实际经历的当下感受；均可省略，不是对外发言或每轮总结。",nativeArgumentSchema(finalSilenceParameters()).dump(),false}};
    }
    std::vector<ayt::inference::ToolDefinition> definitions;
    std::map<std::string,nlohmann::json> catalog, fullCatalog;
    static constexpr const char* routerName="ha_catalog_invoke";
    bool routed=false;
    explicit NativeToolBridge(const nlohmann::json& tools){
        definitions=finalDefinitions();
        for(const auto& tool:tools){const auto canonical=tool.at("name").get<std::string>();if(!nativeToolName(canonical).empty())fullCatalog.emplace(canonical,tool);}
        routed=fullCatalog.size()>126;
        if(routed){
            auto names=nlohmann::json::array();for(const auto& [name,tool]:fullCatalog)names.push_back(name);
            const nlohmann::json schema={{"type","object"},{"additionalProperties",false},{"required",nlohmann::json::array({"tool","arguments_json"})},{"properties",{{"tool",{{"type","string"},{"enum",names}}},{"arguments_json",{{"type","string"},{"maxLength",65536},{"description","目标工具参数的JSON对象字符串，例如 {\"source_id\":\"真实回执id\"}；无参数用 {}"}}}}}};
            definitions.push_back({routerName,"调用本轮完整目录内的工具。原生函数有数量上限，未单独展开的能力仍可通过此入口使用；先用capabilities.list查该工具的说明和参数，再传准确名称与JSON参数。沿用同一来源、审批和执行回执。",schema.dump(),false});
        }
        auto ordered=tools;
        if(routed)std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){const auto priority=[](const auto& t){const auto n=t.at("name").template get<std::string>();return n.starts_with("napcat.")||n=="mentor.history"||n=="task.status"||n=="task.checkpoint";};return priority(a)>priority(b);});
        for(const auto& tool:ordered){
            const auto canonical=tool.at("name").get<std::string>(),name=nativeToolName(canonical);if(name.empty()||definitions.size()>=128)continue;
            std::string purpose=tool.contains("description")&&tool.at("description").is_string()?tool.at("description").get<std::string>():std::string{};
            if(purpose.size()>2048){std::size_t end=2048;while(end&&((static_cast<unsigned char>(purpose[end])&0xc0)==0x80))--end;purpose.resize(end);}
            definitions.push_back({name,canonical+" — "+purpose,nativeArgumentSchema(tool.at("parameters")).dump(),false});catalog.emplace(name,tool);
        }
    }
    TurnValidation candidate(const std::vector<ayt::inference::ToolCall>& calls,std::size_t batch)const{
        using Json=nlohmann::json;Json items=Json::array();
        for(const auto& call:calls)if(call.name=="ha_turn_reply"||call.name=="ha_turn_silence"){
            if(calls.size()!=1)return {false,"native_final_mixed_with_operations",{}};
            try{
                if(!ayt::serializer::validateJsonDocument(call.arguments,"{}"))return {false,"native_final_arguments",{}};
                const auto finals=finalDefinitions();const auto& declaration=finals[call.name=="ha_turn_reply"?0:1];auto args=Json::parse(call.arguments);
                args=normalizeToolArguments(std::move(args),call.name=="ha_turn_reply"?finalReplyParameters():finalSilenceParameters());
                if(!ayt::serializer::validateJsonDocument(args.dump(),declaration.parameters))return {false,"native_final_arguments",{}};
                Json turn={{"version",1},{"kind",call.name=="ha_turn_reply"?"reply":"silence"},{"text",call.name=="ha_turn_reply"?args.value("text",Json(nullptr)):Json(nullptr)},{"calls",Json::array()}};
                for(const auto* key:{"messages","presentation","carry_forward","appraisal","feeling"})if(args.contains(key))turn[key]=args.at(key);return validateTurn(turn.dump());
            }catch(...){return {false,"native_final_arguments",{}};}
        }
        try{for(std::size_t i=0;i<calls.size();++i){
            if(!ayt::serializer::validateJsonDocument(calls[i].arguments,"{}"))return {false,"native_argument_json",{}};
            auto args=Json::parse(calls[i].arguments);const Json* declaration=nullptr;
            if(calls[i].name==routerName&&routed){
                if(!args.is_object()||args.size()!=2||!args.contains("tool")||!args.at("tool").is_string()||!args.contains("arguments_json")||!args.at("arguments_json").is_string())return {false,"native_router_arguments",{}};
                const auto target=fullCatalog.find(args.at("tool").get<std::string>());if(target==fullCatalog.end())return {false,"native_tool_not_declared",{}};
                const auto encoded=args.at("arguments_json").get<std::string>();if(encoded.size()>65536||!ayt::serializer::validateJsonDocument(encoded,"{\"type\":\"object\"}"))return {false,"native_argument_json",{}};
                declaration=&target->second;args=Json::parse(encoded);
            }else{const auto found=catalog.find(calls[i].name);if(found==catalog.end())return {false,"native_tool_not_declared",{}};declaration=&found->second;}
            args=normalizeToolArguments(std::move(args),declaration->at("parameters"));
            if(!ayt::serializer::validateJsonDocument(args.dump(),declaration->at("parameters").dump()))return {false,"native_arguments_invalid",{}};
            // Stable execution slots survive provider-generated ID changes on a
            // job retry. The normal ledger still rejects changed arguments.
            items.push_back({{"id","n"+std::to_string(batch)+"_"+std::to_string(i)},{"tool",declaration->at("name")},{"args",std::move(args)}});
        }}catch(...){return {false,"native_argument_json",{}};}
        return validateTurn(Json{{"version",1},{"kind","call"},{"text",nullptr},{"calls",items}}.dump());
    }
};
}
