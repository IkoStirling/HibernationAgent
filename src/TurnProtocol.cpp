#include <Hibernation/TurnProtocol.h>
#include <Hibernation/ModelJson.h>
#include <AYSerializer/JsonSchema.h>
#include <set>
#include <algorithm>
namespace hibernation {
namespace {
using Json=nlohmann::json;
bool hasType(const Json& schema,const std::string& type){if(!schema.contains("type"))return false;const auto& value=schema.at("type");return value.is_string()?value==type:value.is_array()&&std::find(value.begin(),value.end(),type)!=value.end();}
Json strictSchema(Json schema){
    if(schema.contains("anyOf"))for(auto& branch:schema["anyOf"])branch=strictSchema(std::move(branch));
    if(schema.contains("enum")&&!schema.contains("type")&&!schema.at("enum").empty()){Json types=Json::array();for(const auto& value:schema.at("enum")){const std::string type=value.is_string()?"string":value.is_number_integer()?"integer":value.is_number()?"number":value.is_boolean()?"boolean":value.is_object()?"object":value.is_array()?"array":"null";if(std::find(types.begin(),types.end(),type)==types.end())types.push_back(type);}schema["type"]=types.size()==1?types[0]:types;}
    if(hasType(schema,"object")){
        if(!schema.contains("properties"))schema["properties"]=Json::object();auto required=schema.value("required",Json::array());Json all=Json::array();schema["additionalProperties"]=false;
        for(auto it=schema["properties"].begin();it!=schema["properties"].end();++it){auto child=strictSchema(it.value());if(std::find(required.begin(),required.end(),it.key())==required.end())child={{"anyOf",Json::array({child,{{"type","null"}}})}};it.value()=std::move(child);all.push_back(it.key());}schema["required"]=std::move(all);
    }
    if(hasType(schema,"array")&&schema.contains("items"))schema["items"]=strictSchema(schema.at("items"));return schema;
}
}
nlohmann::json personalCarrySchema(){return Json::parse(R"({"type":"array","maxItems":2,"description":"可选，私下留给未来自己的具体关注、偏好或未完打算；不是思维过程或每轮总结。值得续接才填，否则省略或空数组。和self.notice相同的权限、来源、版本校验。","items":{"type":"object","additionalProperties":false,"required":["id","expected_revision","kind","topic","note","stance","source_ids"],"properties":{"id":{"type":"string","minLength":1,"maxLength":96},"expected_revision":{"type":"integer","minimum":0},"kind":{"type":"string","enum":["interest","preference","intention"]},"topic":{"type":"string","minLength":1,"maxLength":160},"note":{"type":"string","minLength":1,"maxLength":400},"stance":{"type":"string","enum":["curious","like","avoid","want"]},"source_ids":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"string","minLength":1,"maxLength":256}}}}})");}
nlohmann::json personalCloseParameters(){return Json::parse(R"({"type":"object","additionalProperties":false,"required":["id","expected_revision","outcome","note","source_ids"],"properties":{"id":{"type":"string","minLength":1,"maxLength":256},"expected_revision":{"type":"integer","minimum":1},"outcome":{"type":"string","enum":["completed","abandoned","released"]},"note":{"type":"string","minLength":1,"maxLength":600},"source_ids":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"string","minLength":1,"maxLength":256}}}})");}
std::string turnSchema(){auto schema=Json::parse(R"({"type":"object","additionalProperties":false,"required":["version","kind","text","calls"],"properties":{
"version":{"enum":[1]},"kind":{"enum":["call","reply","silence"]},"text":{"anyOf":[{"type":"null"},{"type":"string","maxLength":6000}]},
"calls":{"type":"array","maxItems":4,"items":{"type":"object","additionalProperties":false,"required":["id","tool","args"],"properties":{"id":{"type":"string","minLength":1,"maxLength":32},"tool":{"type":"string","minLength":1,"maxLength":96},"args":{"type":"object"}}}},
"presentation":{"type":"object","additionalProperties":false,"properties":{"speech":{"type":"boolean"},"bubble":{"type":"boolean"}}}
}})");schema["properties"]["carry_forward"]={{"anyOf",Json::array({personalCarrySchema(),{{"type","null"}}})}};return schema.dump();}
std::string turnProviderSchema(const nlohmann::json& catalog){
    auto schema=Json::parse(turnSchema());schema["required"].push_back("presentation");schema["properties"]["presentation"]["required"]=Json::array({"speech","bubble"});Json variants=Json::array();
    for(const auto& tool:catalog){auto call=schema["properties"]["calls"]["items"];call["properties"]["tool"]={{"type","string"},{"enum",Json::array({tool.at("name")})}};call["properties"]["args"]=strictSchema(tool.at("parameters"));variants.push_back(strictSchema(std::move(call)));}
    schema["properties"]["calls"]["items"]={{"anyOf",variants}};return strictSchema(std::move(schema)).dump();
}
nlohmann::json normalizeToolArguments(nlohmann::json args,const nlohmann::json& schema){
    if(schema.contains("anyOf")){
        for(const auto& branch:schema.at("anyOf")){auto candidate=normalizeToolArguments(args,branch);if(ayt::serializer::validateJsonDocument(candidate.dump(),branch.dump()))return candidate;}
        return args; // Unknown/required fields remain intact and fail validation.
    }
    if(args.is_object()&&schema.contains("properties")){const auto required=schema.value("required",Json::array());for(auto it=args.begin();it!=args.end();){if(schema.at("properties").contains(it.key())){const auto& child=schema.at("properties").at(it.key());if(it.value().is_null()&&std::find(required.begin(),required.end(),it.key())==required.end()&&!ayt::serializer::validateJsonDocument("null",child.dump())){it=args.erase(it);continue;}it.value()=normalizeToolArguments(it.value(),child);}++it;}}
    else if(args.is_array()&&schema.contains("items"))for(auto& child:args)child=normalizeToolArguments(std::move(child),schema.at("items"));return args;
}
TurnValidation validateTurn(std::string_view text){
    if(text.size()>128*1024)return {false,"response_too_large",{}};const auto envelope=modelJsonEnvelope(text);if(!envelope)return {false,"invalid_turn_schema",{}};text=*envelope;
    if(!ayt::serializer::validateJsonDocument(text,turnSchema()))return {false,"invalid_turn_schema",{}};
    auto value=nlohmann::json::parse(text);if(value.contains("carry_forward")&&value.at("carry_forward").is_null())value.erase("carry_forward");const auto kind=value.at("kind").get<std::string>();
    if(kind=="call"&&(value.at("calls").empty()||!value.at("text").is_null()))return {false,"call_shape",{}};
    if(kind=="reply"&&(!value.at("calls").empty()||!value.at("text").is_string()||value.at("text").get<std::string>().empty()))return {false,"reply_shape",{}};
    if(kind=="reply"){const auto text=value.at("text").get<std::string>();if(text.find_first_not_of(" \t\r\n")==std::string::npos||std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32&&c!='\t'&&c!='\r'&&c!='\n';}))return {false,"reply_text",{}};}
    if(kind=="silence"&&(!value.at("calls").empty()||!value.at("text").is_null()))return {false,"silence_shape",{}};
    if(kind=="call"&&value.contains("carry_forward")&&!value.at("carry_forward").empty())return {false,"carry_requires_final",{}};
    std::set<std::string> ids;
    auto valid=[](const std::string& s,bool dotted){return std::all_of(s.begin(),s.end(),[&](unsigned char c){return std::isalnum(c)||c=='_'||c=='-'||(dotted&&c=='.');});};
    for(const auto& call:value.at("calls")){const auto id=call.at("id").get<std::string>(),tool=call.at("tool").get<std::string>();if(!ids.insert(id).second||!valid(id,false)||!valid(tool,true)||call.at("args").dump().size()>32*1024)return {false,"invalid_call_identity_or_size",{}};}
    return {true,"ok",std::move(value)};
}
TurnValidation plainReplyCandidate(std::string_view text){
    if(text.empty()||text.size()>6000)return {false,"plain_reply_size",{}};
    while(!text.empty()&&(text.front()==' '||text.front()=='\n'||text.front()=='\r'||text.front()=='\t'))text.remove_prefix(1);
    while(!text.empty()&&(text.back()==' '||text.back()=='\n'||text.back()=='\r'||text.back()=='\t'))text.remove_suffix(1);
    if(text.empty()||text.find_first_of("{}[]`<>")!=std::string_view::npos||text.starts_with("\xef\xbb\xbf")||Json::accept(text))return {false,"plain_reply_not_dialogue",{}};
    for(const unsigned char c:text)if(c<32&&c!='\n'&&c!='\r'&&c!='\t')return {false,"plain_reply_control",{}};
    return validateTurn(Json{{"version",1},{"kind","reply"},{"text",text},{"calls",Json::array()}}.dump());
}
}
