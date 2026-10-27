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
        return {{"type","object"},{"additionalProperties",false},{"properties",{{"text",{{"type","string"},{"minLength",1},{"maxLength",6000}}},{"presentation",turn.at("properties").at("presentation")},{"carry_forward",personalCarrySchema()},{"appraisal",appraisalParameters()}}},{"required",nlohmann::json::array({"text"})}};
    }
    static nlohmann::json finalSilenceParameters(){return {{"type","object"},{"properties",{{"carry_forward",personalCarrySchema()},{"appraisal",appraisalParameters()}}},{"additionalProperties",false}};}
    static std::vector<ayt::inference::ToolDefinition> finalDefinitions(){
        return {{"ha_turn_reply","提交本轮最终对白。text直接写对对方说的台词，普通交谈不加舞台旁白或整段扮演引号；明确请求故事或剧本时按所需文体。校验和审核后才显示，不与操作提案同批。",nativeArgumentSchema(finalReplyParameters()).dump(),false},
                {"ha_turn_silence","选择本轮不发言；不代表发生了任何动作。可选carry_forward仅留下一点有来源的持续关注，不是对外发言。",nativeArgumentSchema(finalSilenceParameters()).dump(),false}};
    }
    std::vector<ayt::inference::ToolDefinition> definitions;
    std::map<std::string,nlohmann::json> catalog;
    explicit NativeToolBridge(const nlohmann::json& tools){
        definitions=finalDefinitions();
        for(const auto& tool:tools){
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
                const auto finals=finalDefinitions();const auto& declaration=finals[call.name=="ha_turn_reply"?0:1];auto args=Json::parse(call.arguments);
                args=normalizeToolArguments(std::move(args),call.name=="ha_turn_reply"?finalReplyParameters():finalSilenceParameters());
                if(!ayt::serializer::validateJsonDocument(args.dump(),declaration.parameters))return {false,"native_final_arguments",{}};
                Json turn={{"version",1},{"kind",call.name=="ha_turn_reply"?"reply":"silence"},{"text",call.name=="ha_turn_reply"?args.at("text"):Json(nullptr)},{"calls",Json::array()}};
                for(const auto* key:{"presentation","carry_forward","appraisal"})if(args.contains(key))turn[key]=args.at(key);return validateTurn(turn.dump());
            }catch(...){return {false,"native_final_arguments",{}};}
        }
        try{for(std::size_t i=0;i<calls.size();++i){const auto found=catalog.find(calls[i].name);if(found==catalog.end())return {false,"native_tool_not_declared",{}};
            auto args=Json::parse(calls[i].arguments);args=normalizeToolArguments(std::move(args),found->second.at("parameters"));
            if(!ayt::serializer::validateJsonDocument(args.dump(),found->second.at("parameters").dump()))return {false,"native_arguments_invalid",{}};
            // Stable execution slots survive provider-generated ID changes on a
            // job retry. The normal ledger still rejects changed arguments.
            items.push_back({{"id","n"+std::to_string(batch)+"_"+std::to_string(i)},{"tool",found->second.at("name")},{"args",std::move(args)}});
        }}catch(...){return {false,"native_argument_json",{}};}
        return validateTurn(Json{{"version",1},{"kind","call"},{"text",nullptr},{"calls",items}}.dump());
    }
};
}
