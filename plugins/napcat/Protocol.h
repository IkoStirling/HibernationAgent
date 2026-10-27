#pragma once
#include "../PluginCommon.h"
#include "FaceNames.h"
#include <Hibernation/ChannelAccess.h>
#include <AYCrypto.h>
#include <bcrypt.h>
#include <array>
#include <optional>
#include <set>
namespace hibernation::napcat {
using namespace hibernation::plugins;
inline std::string digits(const PluginJson& value,bool message=false){
    std::string s;if(value.is_string())s=value.get<std::string>();else if(value.is_number_unsigned())s=std::to_string(value.get<std::uint64_t>());else if(value.is_number_integer())s=std::to_string(value.get<std::int64_t>());else throw std::runtime_error("qq_id_type");
    const auto offset=message&&s.starts_with('-')?1u:0u;
    if(s.size()<=offset||s.size()>20||!std::all_of(s.begin()+offset,s.end(),[](char c){return c>='0'&&c<='9';})||(!message&&(s.front()=='0')))throw std::runtime_error("qq_id");return s;
}
inline std::set<std::string> idList(std::string value){std::set<std::string> out;std::string current;for(char c:value+','){if(c==','||c==' '||c=='\n'||c=='\r'||c=='\t'){if(!current.empty()){out.insert(digits(current));current.clear();}}else current+=c;}if(out.size()>128)throw std::runtime_error("qq_allowlist_limit");return out;}
inline bool envName(const std::string& s){return s.size()>=1&&s.size()<=128&&(std::isalpha(static_cast<unsigned char>(s[0]))||s[0]=='_')&&std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});}
inline std::string digest(const std::string& value){auto h=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);if(!h)throw std::runtime_error("hash_unavailable");return h->hashString(reinterpret_cast<const uint8_t*>(value.data()),value.size());}
inline std::string signature(const std::string& secret,const std::string& body){
    // OneBot HTTP event compatibility requires HMAC-SHA1 (not archive crypto).
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,20> bytes{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA1_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0)throw std::runtime_error("hmac_unavailable");
    auto status=BCryptCreateHash(algorithm,&hash,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(secret.data())),static_cast<ULONG>(secret.size()),0);
    if(status>=0)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(body.data())),static_cast<ULONG>(body.size()),0);
    if(status>=0)status=BCryptFinishHash(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw std::runtime_error("hmac_failed");
    constexpr char hex[]="0123456789abcdef";std::string out="sha1=";for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
inline bool authenticate(const std::string& secret,const std::string& body,const std::string& supplied){if(secret.size()<16||secret.size()>4096)return false;const auto expected=signature(secret,body);if(supplied.size()!=expected.size())return false;unsigned difference=0;for(size_t i=0;i<expected.size();++i)difference|=static_cast<unsigned char>(expected[i])^static_cast<unsigned char>(supplied[i]);return difference==0;}
struct Settings {
    hibernation::ChannelAccess access;
    std::string bot,owner,origin,apiEnvironment,eventEnvironment;
    std::set<std::string> users,groups;
    int port=18890,queueLimit=32,cooldownSeconds=5,mergeWindowMs=1500,maxAgeSeconds=1800,maxReplyCharacters=1500;
    bool mentionOnly=true,replyAt=true;
};
// Only application-owned messages cross the native-plugin error boundary.
// Never include exception text, token values or arbitrary configuration data.
inline std::string configurationMessage(std::string_view code){
    if(code=="napcat_security_type"||code=="napcat_control_trust")return "QQ 权限设置无效，请检查安全性开关及操作可信范围。";
    if(code=="napcat_self_id_required")return "请填写机器人登录的 QQ 号。";
    if(code=="napcat_self_id_invalid")return "机器人 QQ 号格式不正确，请只填写数字。";
    if(code=="napcat_owner_id_required")return "请填写号主本人的 QQ 号。";
    if(code=="napcat_owner_id_invalid")return "号主 QQ 号格式不正确，请只填写数字。";
    if(code=="owner_is_bot")return "号主 QQ 号不能与机器人 QQ 号相同。";
    if(code=="napcat_api_token_missing")return "未配置 NapCat HTTP Server token。请设置 HIBERNATION_NAPCAT_API_TOKEN 环境变量（或配置中指定的变量），然后重启后台；变量值须与 NapCat HTTP Server 的 token 一致。";
    if(code=="napcat_api_token_invalid")return "NapCat HTTP Server token 格式不正确，须为 16 至 4096 个字符，且不能包含换行。";
    if(code=="napcat_event_secret_missing")return "未配置 NapCat HTTP Client 事件验签 token。请设置 HIBERNATION_NAPCAT_EVENT_SECRET 环境变量（或配置中指定的变量），然后重启后台；变量值须与 NapCat HTTP Client 的 token 一致。";
    if(code=="napcat_event_secret_invalid")return "NapCat HTTP Client 事件验签 token 须为 16 至 4096 个字符。";
    if(code=="napcat_callback_port_in_use")return "NapCat 事件上报端口已被占用，请更换本机 HTTP 上报端口，并同步修改 NapCat HTTP Client 上报地址。";
    if(code=="napcat_callback_port_invalid")return "本机 HTTP 上报端口须为 1024 至 65535 的整数。";
    if(code=="napcat_origin")return "NapCat HTTP Server 地址无效，请填写本机 HTTP 地址（如 http://127.0.0.1:3000）或 HTTPS 地址，不要包含接口路径。";
    if(code=="napcat_environment")return "凭据环境变量名不正确：请填写两个不同的变量名，不要把 token 本身填在变量名中。";
    if(code=="napcat_users_invalid")return "私聊白名单格式不正确，请填写数字 QQ 号，多个号码用逗号分隔，最多 128 个。";
    if(code=="napcat_groups_invalid")return "群白名单格式不正确，请填写数字群号，多个号码用逗号分隔，最多 128 个。";
    if(code=="napcat_integer"||code=="plugin_configuration_range")return "NapCat 数值设置超出允许范围，请检查队列数量、冷却时间、消息过期时间与回复长度。";
    if(code=="napcat_listener_failed")return "NapCat 事件上报监听启动失败，请检查端口及本机网络权限。";
    if(code=="plugin_configuration_type"||code=="plugin_configuration_string")return "NapCat 配置字段类型或长度不正确，请检查输入。";
    return "NapCat 初始化失败，请检查独立记忆目录的读写权限及本机监听设置。";
}
inline std::string credentialProblem(const Settings& settings){
    const auto token=ayt::net::http::environmentCredential(settings.apiEnvironment);
    if(token.empty())return "napcat_api_token_missing";
    if(token.size()<16||token.size()>4096||token.find_first_of("\r\n")!=std::string::npos)return "napcat_api_token_invalid";
    const auto secret=ayt::net::http::environmentCredential(settings.eventEnvironment);
    if(secret.empty())return "napcat_event_secret_missing";
    if(secret.size()<16||secret.size()>4096)return "napcat_event_secret_invalid";
    return {};
}
inline Settings settings(const PluginJson& j){
    configurationTypes(j,{"selfUserId","ownerUserId","apiOrigin","apiTokenEnvironment","eventSecretEnvironment","allowedUsers","allowedGroups"},{"groupMentionOnly","replyAtSender"});Settings s;
    if(!j.contains("selfUserId")||j.at("selfUserId")=="")throw std::runtime_error("napcat_self_id_required");
    try{s.bot=digits(j.at("selfUserId"));}catch(...){throw std::runtime_error("napcat_self_id_invalid");}
    if(!j.contains("ownerUserId")||j.at("ownerUserId")=="")throw std::runtime_error("napcat_owner_id_required");
    try{s.owner=digits(j.at("ownerUserId"));}catch(...){throw std::runtime_error("napcat_owner_id_invalid");}
    if(s.bot==s.owner)throw std::runtime_error("owner_is_bot");
    s.origin=j.value("apiOrigin",std::string("http://127.0.0.1:3000"));
    if(!ayt::net::http::validOrigin(s.origin)||(!s.origin.starts_with("http://127.0.0.1:")&&!s.origin.starts_with("http://localhost:")&&!s.origin.starts_with("https://")))throw std::runtime_error("napcat_origin");
    s.apiEnvironment=j.value("apiTokenEnvironment",std::string("HIBERNATION_NAPCAT_API_TOKEN"));s.eventEnvironment=j.value("eventSecretEnvironment",std::string("HIBERNATION_NAPCAT_EVENT_SECRET"));
    if(!envName(s.apiEnvironment)||!envName(s.eventEnvironment)||s.apiEnvironment==s.eventEnvironment)throw std::runtime_error("napcat_environment");
    auto integer=[&](const char* key,int fallback,int low,int high){if(j.contains(key)&&!j.at(key).is_number_integer())throw std::runtime_error("napcat_integer");return static_cast<int>(number(j,key,fallback,low,high));};
    try{s.port=integer("callbackPort",18890,1024,65535);}catch(...){throw std::runtime_error("napcat_callback_port_invalid");}
    s.queueLimit=integer("queueLimit",32,1,64);s.cooldownSeconds=integer("cooldownSeconds",5,1,300);s.maxAgeSeconds=integer("maxMessageAgeSeconds",1800,30,86400);s.maxReplyCharacters=integer("maxReplyCharacters",1500,64,4000);
    s.mergeWindowMs=integer("mergeWindowMs",1500,0,5000);
    try{s.users=idList(j.value("allowedUsers",std::string{}));}catch(...){throw std::runtime_error("napcat_users_invalid");}
    try{s.groups=idList(j.value("allowedGroups",std::string{}));}catch(...){throw std::runtime_error("napcat_groups_invalid");}
    s.access=hibernation::ChannelAccess::read(j);s.mentionOnly=j.value("groupMentionOnly",true);s.replyAt=j.value("replyAtSender",true);return s;
}
inline std::optional<PluginJson> parseEvent(const PluginJson& event,const Settings& s,std::int64_t now){
    if(!event.is_object()||event.value("post_type",std::string{})!="message")return {};
    if(digits(event.at("self_id"))!=s.bot)return {};
    const auto type=event.at("message_type").get<std::string>();if(type!="private"&&type!="group")return {};
    const auto sender=digits(event.at("user_id"));if(sender==s.bot)return {};
    if(event.contains("sender")&&event.at("sender").contains("user_id")&&digits(event.at("sender").at("user_id"))!=sender)throw std::runtime_error("sender_mismatch");
    const auto stamp=event.at("time").get<std::int64_t>();if(stamp<now/1000-s.maxAgeSeconds||stamp>now/1000+60)return {};
    std::string group;if(type=="group"){group=digits(event.at("group_id"));if(!s.groups.contains(group))return {};}else if(sender!=s.owner&&!s.users.contains(sender))return {};
    const auto mid=digits(event.at("message_id"),true);const auto& segments=event.at("message");
    // Require array mode: CQ-code strings and embedded control syntax never
    // become executable segments. Media references are data, never local paths to open.
    if(!segments.is_array()||segments.size()>128)throw std::runtime_error("message_array_required");
    bool mentioned=false;std::string text;PluginJson attachments=PluginJson::array();for(const auto& segment:segments){const auto kind=segment.at("type").get<std::string>();const auto data=segment.value("data",PluginJson::object());
        if(kind=="text")text+=data.at("text").get<std::string>();
        else if(kind=="at"){if(data.at("qq")!="all"&&digits(data.at("qq"))==s.bot)mentioned=true;}
        else if(kind=="reply"){} // No lookup of another member's quoted content.
        else if(kind=="image"||kind=="mface"){
            if(attachments.size()>=8)throw std::runtime_error("image_count_limit");
            auto field=[&](const char* key,size_t limit){const auto value=data.value(key,std::string{});if(value.size()>limit)throw std::runtime_error("image_reference_limit");return value;};
            attachments.push_back({{"kind",kind},{"file",field("file_id",2048).empty()?field("file",2048):field("file_id",2048)},{"url",field("url",8192)},{"summary",field("summary",256)}});
            text+="[收到图片/表情包，需实际读取后才能描述内容]";
        }
        else if(kind=="face")text+=qqFaceText(digits(data.at("id"),true));
        else text+="[未读取的"+kind.substr(0,24)+"消息]";
    }
    if(type=="group"&&s.mentionOnly&&!mentioned)return {};
    if(text.empty()||text.size()>12000||text.find_first_not_of(" \r\n\t")==std::string::npos)return {};
    const auto key="qq-"+digest(s.bot+":"+type+":"+group+":"+sender+":"+mid).substr(0,48);
    const auto contentSignature=digest(text+attachments.dump());
    for(size_t i=0;i<attachments.size();++i){attachments[i]["id"]="qqimage-"+digest(key+":"+std::to_string(i)).substr(0,40);attachments[i]["message_id"]=mid;attachments[i]["received_ms"]=now;}
    return PluginJson{{"id",key},{"type",type},{"self_id",s.bot},{"sender_id",sender},{"group_id",group},{"message_id",mid},{"text",text},{"content_signature",contentSignature},{"attachments",attachments},{"created_ms",now},{"next_ms",now+s.mergeWindowMs},{"event_time",stamp},{"status","queued"},{"attempts",0}};
}
inline std::string conversationScope(const PluginJson& row){return row.at("type").get<std::string>()+":"+row.at("group_id").get<std::string>()+":"+row.at("sender_id").get<std::string>();}
inline std::string desktopOperationKey(const std::string& request,const std::string& tool,const PluginJson& args,const std::string& readToken){
    const bool fresh=tool=="computer.screenshot"||tool=="widget.status"||tool=="body.observe";
    return "qqcall-"+digest(request+":"+tool+":"+args.dump()+(fresh?":"+readToken:std::string{})).substr(0,48);
}
inline PluginJson identity(const PluginJson& row,const Settings& s){return {{"channel","qq"},{"transport","napcat"},{"self_user_id",s.bot},{"owner_user_id",s.owner},{"sender_user_id",row.at("sender_id")},{"sender_is_owner",row.at("sender_id")==s.owner},{"group_id",row.at("group_id")},{"message_type",row.at("type")},{"message_id",row.at("message_id")},{"memory_policy",s.access.isolateMemory?"only_this_conversation_and_sender":"shared_character_separate_archives"},{"desktop_control",s.access.controls(row.at("sender_id")==s.owner)},{"effective_security",s.access.json()}};}
inline std::filesystem::path conversation(const std::filesystem::path& root,const PluginJson& row){
    const auto sender=digits(row.at("sender_id"));if(row.at("type")=="private")return root/"private"/sender;
    return root/"groups"/digits(row.at("group_id"))/"members"/sender;
}
inline std::string boundedReply(const std::string& text,int characters){size_t pos=0;int count=0;while(pos<text.size()&&count<characters){const auto c=static_cast<unsigned char>(text[pos]);pos+=c<128?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;++count;}if(pos>=text.size())return text;auto out=text.substr(0,pos);const auto end=out.find_last_of("\n");if(end!=std::string::npos&&end>out.size()/2)out.resize(end);return out+"…";}
}
