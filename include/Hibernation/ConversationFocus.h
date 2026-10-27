#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <limits>
#include <set>
#include <string>
namespace hibernation {
using FocusJson=nlohmann::json;
inline bool focusChoice(const FocusJson& value,const char* field,std::initializer_list<const char*> allowed){
    if(!value.contains(field)||!value.at(field).is_string())return false;const auto& text=value.at(field).get_ref<const std::string&>();return std::any_of(allowed.begin(),allowed.end(),[&](const auto* item){return text==item;});
}
inline bool validFocusPreferences(const FocusJson& value){return value.is_object()&&value.size()==3&&focusChoice(value,"subject",{"shared","character","user"})&&focusChoice(value,"advice",{"unspecified","allow","avoid"})&&focusChoice(value,"questions",{"natural","avoid_repeated"});}
inline bool validConversationFocus(const FocusJson& value){
    if(!value.is_object()||!value.contains("version")||value.at("version")!=1||!value.contains("preferences")||!validFocusPreferences(value.at("preferences")))return false;
    for(const auto* field:{"source_id","source_quote"})if(!value.contains(field)||!value.at(field).is_string()||value.at(field).get_ref<const std::string&>().empty())return false;
    if(value.at("source_id").get_ref<const std::string&>().size()>200||value.at("source_quote").get_ref<const std::string&>().size()>640)return false;
    for(const auto* field:{"created_ms","expires_ms","source_revision"})if(!value.contains(field)||!value.at(field).is_number_integer())return false;
    const auto created=value.at("created_ms").get<std::int64_t>(),expires=value.at("expires_ms").get<std::int64_t>();if(created<0||expires<=created||expires-created>1200000||value.at("source_revision").get<std::int64_t>()<1)return false;
    if(!value.contains("used_event_ids")||!value.at("used_event_ids").is_array()||value.at("used_event_ids").size()>4)return false;
    std::set<std::string> ids;for(const auto& id:value.at("used_event_ids"))if(!id.is_string()||id.get_ref<const std::string&>().empty()||id.get_ref<const std::string&>().size()>200||!ids.insert(id.get<std::string>()).second)return false;return true;
}
inline std::optional<FocusJson> proposeConversationFocus(const FocusJson& before,const FocusJson& proposal,
    const std::string& currentUserText,const std::string& eventId,std::int64_t sourceRevision,std::int64_t now,std::string& issue){
    issue.clear();if(proposal.is_null())return {};
    if(!proposal.is_object()||proposal.size()!=4||!proposal.contains("source_quote")||!proposal.at("source_quote").is_string()){issue="focus_format";return {};}
    auto preferences=proposal;preferences.erase("source_quote");if(!validFocusPreferences(preferences)){issue="focus_preferences";return {};}
    const auto quote=proposal.at("source_quote").get<std::string>();const auto characters=std::count_if(quote.begin(),quote.end(),[](unsigned char c){return (c&0xc0)!=0x80;});
    if(characters<4||characters>160||quote.find_first_not_of(" \t\r\n")==std::string::npos||currentUserText.find(quote)==std::string::npos){issue="focus_quote_not_current";return {};}
    if(eventId.empty()||eventId.size()>200||sourceRevision<1||now<0||now>std::numeric_limits<std::int64_t>::max()-1200000){issue="focus_source";return {};}
    if(validConversationFocus(before)&&before.at("source_id")==eventId&&before.at("source_revision")==sourceRevision&&before.at("preferences")==preferences&&before.at("source_quote")==quote)return std::optional<FocusJson>{std::in_place,before};
    const auto generation=before.is_object()&&before.contains("generation")&&before.at("generation").is_number_integer()?before.at("generation").get<std::int64_t>():0;if(generation<0||generation>=1000000000){issue="focus_generation";return {};}
    return std::optional<FocusJson>{std::in_place,FocusJson{{"version",1},{"generation",generation+1},{"preferences",preferences},{"source_id",eventId},{"source_revision",sourceRevision},{"source_quote",quote},{"created_ms",now},{"expires_ms",now+1200000},{"used_event_ids",FocusJson::array()}}};
}
struct ConversationFocusProjection {FocusJson record=FocusJson::object(),view=FocusJson::object();bool changed=false;};
inline ConversationFocusProjection conversationFocusForTurn(const FocusJson& saved,const std::string& eventId,std::int64_t now){
    ConversationFocusProjection result;result.record=saved;if(!validConversationFocus(saved)||eventId.empty()||now<saved.at("created_ms").get<std::int64_t>()||now>=saved.at("expires_ms").get<std::int64_t>())return result;
    auto& used=result.record["used_event_ids"];if(eventId!=saved.at("source_id").get<std::string>()&&std::find(used.begin(),used.end(),eventId)==used.end()){
        if(used.size()>=4)return result;used.push_back(eventId);result.changed=true;
    }
    result.view=saved.at("preferences");result.view["source_quote"]=saved.at("source_quote");result.view["meaning"]="对方近期明确表达的交流偏好；当前表达优先，角色仍可选择回应、拒绝或安静，不授予任何工具权限。";return result;
}
}
