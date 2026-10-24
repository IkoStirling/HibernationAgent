#include <Hibernation/TurnProtocol.h>
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
std::string turnSchema(){return R"({"type":"object","additionalProperties":false,"required":["version","kind","text","calls"],"properties":{
"version":{"enum":[1]},"kind":{"enum":["call","reply","silence"]},"text":{"anyOf":[{"type":"null"},{"type":"string","maxLength":6000}]},
"calls":{"type":"array","maxItems":4,"items":{"type":"object","additionalProperties":false,"required":["id","tool","args"],"properties":{"id":{"type":"string","minLength":1,"maxLength":32},"tool":{"type":"string","minLength":1,"maxLength":96},"args":{"type":"object"}}}},
"presentation":{"type":"object","additionalProperties":false,"properties":{"speech":{"type":"boolean"},"bubble":{"type":"boolean"}}}
}})";}
std::string turnProviderSchema(const nlohmann::json& catalog){
    auto schema=Json::parse(turnSchema());schema["required"].push_back("presentation");schema["properties"]["presentation"]["required"]=Json::array({"speech","bubble"});Json variants=Json::array();
    for(const auto& tool:catalog){auto call=schema["properties"]["calls"]["items"];call["properties"]["tool"]={{"type","string"},{"enum",Json::array({tool.at("name")})}};call["properties"]["args"]=strictSchema(tool.at("parameters"));variants.push_back(strictSchema(std::move(call)));}
    schema["properties"]["calls"]["items"]={{"anyOf",variants}};return strictSchema(std::move(schema)).dump();
}
nlohmann::json normalizeToolArguments(nlohmann::json args,const nlohmann::json& schema){
    if(args.is_object()&&schema.contains("properties")){const auto required=schema.value("required",Json::array());for(auto it=args.begin();it!=args.end();){if(schema.at("properties").contains(it.key())){const auto& child=schema.at("properties").at(it.key());if(it.value().is_null()&&std::find(required.begin(),required.end(),it.key())==required.end()&&!ayt::serializer::validateJsonDocument("null",child.dump())){it=args.erase(it);continue;}it.value()=normalizeToolArguments(it.value(),child);}++it;}}
    else if(args.is_array()&&schema.contains("items"))for(auto& child:args)child=normalizeToolArguments(std::move(child),schema.at("items"));return args;
}
TurnValidation validateTurn(std::string_view text){
    if(text.size()>128*1024)return {false,"response_too_large",{}};
    auto trim=[](std::string_view& s){while(!s.empty()&&(s.front()==' '||s.front()=='\n'||s.front()=='\r'||s.front()=='\t'))s.remove_prefix(1);while(!s.empty()&&(s.back()==' '||s.back()=='\n'||s.back()=='\r'||s.back()=='\t'))s.remove_suffix(1);};trim(text);
    // Deterministic wrapper normalization only; never guess missing braces,
    // coerce types, or execute an arbitrary substring of prose.
    if(text.starts_with("\xef\xbb\xbf"))text.remove_prefix(3);
    if(text.starts_with("```json\n")&&text.ends_with("```")){text.remove_prefix(8);text.remove_suffix(3);trim(text);}
    if(!ayt::serializer::validateJsonDocument(text,turnSchema()))return {false,"invalid_turn_schema",{}};
    auto value=nlohmann::json::parse(text);const auto kind=value.at("kind").get<std::string>();
    if(kind=="call"&&(value.at("calls").empty()||!value.at("text").is_null()))return {false,"call_shape",{}};
    if(kind=="reply"&&(!value.at("calls").empty()||!value.at("text").is_string()||value.at("text").get<std::string>().empty()))return {false,"reply_shape",{}};
    if(kind=="reply"){const auto text=value.at("text").get<std::string>();if(text.find_first_not_of(" \t\r\n")==std::string::npos||std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32&&c!='\t'&&c!='\r'&&c!='\n';}))return {false,"reply_text",{}};}
    if(kind=="silence"&&(!value.at("calls").empty()||!value.at("text").is_null()))return {false,"silence_shape",{}};
    std::set<std::string> ids;
    auto valid=[](const std::string& s,bool dotted){return std::all_of(s.begin(),s.end(),[&](unsigned char c){return std::isalnum(c)||c=='_'||c=='-'||(dotted&&c=='.');});};
    for(const auto& call:value.at("calls")){const auto id=call.at("id").get<std::string>(),tool=call.at("tool").get<std::string>();if(!ids.insert(id).second||!valid(id,false)||!valid(tool,true)||call.at("args").dump().size()>32*1024)return {false,"invalid_call_identity_or_size",{}};}
    return {true,"ok",std::move(value)};
}
}
