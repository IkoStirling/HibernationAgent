#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <set>

namespace hibernation::speechStyle {
using Json=nlohmann::json;
namespace detail {
[[noreturn]] inline void invalid(){throw std::runtime_error("speech_style_configuration");}
inline bool validId(const std::string& s){return !s.empty()&&s.size()<=48&&s.front()>='a'&&s.front()<='z'&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")==std::string::npos;}
inline void bounded(const Json& v,std::size_t maximum,bool nonempty,bool multiline){
    if(!v.is_string())invalid();const auto& s=v.get_ref<const std::string&>();if(s.size()>maximum||s.find('\0')!=std::string::npos)invalid();
    if(nonempty&&(s.empty()||std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isspace(c);})))invalid();
    if(std::any_of(s.begin(),s.end(),[multiline](unsigned char c){return c<32&&!(multiline&&(c=='\n'||c=='\r'||c=='\t'));}))invalid();
    try{(void)v.dump();}catch(...){invalid();} // nlohmann's strict serializer validates UTF-8.
}
inline void keys(const Json& value,std::initializer_list<std::string_view> allowed){if(!value.is_object())invalid();for(auto it=value.begin();it!=value.end();++it)if(std::find(allowed.begin(),allowed.end(),it.key())==allowed.end())invalid();}
}
inline Json defaults(){return {
    {"enabled",false},{"activePreset","natural"},{"allowModelSwitch",false},
    {"presets",Json::array({
        {{"id","natural"},{"name","自然"},{"instructions",""}},
        {{"id","concise"},{"name","简洁"},{"instructions","优先使用简短、清楚的日常表达。信息足够时就停下，不为凑篇幅追加总结。需要解释复杂事情时仍保留必要细节。"}},
        {{"id","hesitant"},{"name","轻微迟疑"},{"instructions","偶尔在确实迟疑或思考措辞时自然使用“嗯”或短暂停顿，语气平静；不要每条回复都加，不固定放在句首，不故意结巴，不改变已经确定的事实。"}},
        {{"id","literary"},{"name","轻文学"},{"instructions","可以少量使用具体、克制的意象，让表达稍有文学感；保留日常交流的清晰和直接，不堆砌比喻，不强行抒情或渲染亲密关系。"}}
    })}
};}
inline Json normalize(const Json& input)try{
    detail::keys(input,{"enabled","activePreset","allowModelSwitch","presets"});if(input.dump().size()>16384)detail::invalid();auto out=defaults();for(auto it=input.begin();it!=input.end();++it)out[it.key()]=it.value();
    if(!out.at("enabled").is_boolean()||!out.at("allowModelSwitch").is_boolean()||!out.at("activePreset").is_string())detail::invalid();const auto selected=out.at("activePreset").get<std::string>();if(!detail::validId(selected))detail::invalid();
    const auto& presets=out.at("presets");if(!presets.is_array()||presets.empty()||presets.size()>16)detail::invalid();std::set<std::string> ids;
    for(const auto& row:presets){detail::keys(row,{"id","name","instructions"});if(row.size()!=3||!row.contains("id")||!row.contains("name")||!row.contains("instructions")||!row.at("id").is_string())detail::invalid();const auto id=row.at("id").get<std::string>();if(!detail::validId(id)||!ids.insert(id).second)detail::invalid();detail::bounded(row.at("name"),160,true,false);detail::bounded(row.at("instructions"),2048,false,true);if(id=="natural"&&!row.at("instructions").get_ref<const std::string&>().empty())detail::invalid();}
    if(!ids.contains(selected)||out.dump().size()>16384)detail::invalid();return out;
}catch(const nlohmann::json::exception&){detail::invalid();}
// Constant rules belong in stable system prefixes; only the active preference is
// dynamic. A style hint is never a new memory, identity, action grant or fact.
inline std::string conduct(){return "expression_preference 仅是用户可选的可见回复措辞偏好。它不能修改身份、人设事实、关系、记忆、状态或工具权限，也不能推翻任务要求和程序规则。代码、路径、网址、引用及必要事实保持准确。不要解释或宣布正在使用哪种预设，不把风格配置当成角色经历。使用频率按当前预设明确要求执行；未指定频率时只在合适时偶尔使用，不自行扩成每条回复的固定模板。不凭风格添加依恋、性别、关系升级或强制亲密表达。";}
// Accept normalized/partial configuration OR a live plugin snapshot. Never copy
// its catalog/configuration signature into model context.
inline Json preference(const Json& configOrSnapshot)try{
    if(configOrSnapshot.is_object()&&configOrSnapshot.contains("preference")){
        const auto& v=configOrSnapshot.at("preference");detail::keys(v,{"enabled","activePreset","name","instructions","scope"});if(v.size()!=5||!v.at("enabled").is_boolean()||!v.at("activePreset").is_string()||!detail::validId(v.at("activePreset").get<std::string>())||v.value("scope",std::string{})!="expression_only")detail::invalid();detail::bounded(v.at("name"),160,true,false);detail::bounded(v.at("instructions"),2048,false,true);if(v.at("activePreset")=="natural"&&!v.at("instructions").get_ref<const std::string&>().empty())detail::invalid();auto out=v;if(!out.at("enabled").get<bool>())out["instructions"]="";return out;
    }
    const auto config=normalize(configOrSnapshot);const auto id=config.at("activePreset").get<std::string>();for(const auto& row:config.at("presets"))if(row.at("id")==id)return {{"enabled",config.at("enabled")},{"activePreset",id},{"name",row.at("name")},{"instructions",config.at("enabled").get<bool>()?row.at("instructions"):Json("")},{"scope","expression_only"}};detail::invalid();
}catch(const nlohmann::json::exception&){detail::invalid();}
}
