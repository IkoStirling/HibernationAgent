#pragma once
#include <Hibernation/RolePlugin.h>
#include <AYNetwork/HttpClient.h>
#include <Windows.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <atomic>
#include <initializer_list>
#include <stdexcept>
namespace hibernation::plugins {
inline std::int64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
inline std::wstring wide(const std::string& s){if(s.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return {};std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
inline std::string pathUtf8(const std::filesystem::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
inline std::string urlEncode(const std::string& s){constexpr char hex[]="0123456789ABCDEF";std::string out;for(unsigned char c:s)if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out.push_back(c);else{out.push_back('%');out.push_back(hex[c>>4]);out.push_back(hex[c&15]);}return out;}
inline PluginJson object(PluginJson properties=PluginJson::object(),PluginJson required=PluginJson::array()){return {{"type","object"},{"additionalProperties",false},{"properties",std::move(properties)},{"required",std::move(required)}};}
inline PluginJson tool(const char* name,const char* description,PluginJson parameters){return {{"name",name},{"description",description},{"parameters",std::move(parameters)}};}
inline double number(const PluginJson& j,const char* key,double fallback,double lo,double hi){const auto value=j.value(key,fallback);if(!std::isfinite(value)||value<lo||value>hi)throw std::runtime_error("plugin_configuration_range");return value;}
inline void configurationTypes(const PluginJson& cfg,std::initializer_list<const char*> strings,std::initializer_list<const char*> booleans){
    if(!cfg.is_object())throw std::runtime_error("plugin_configuration_type");
    for(const auto* key:strings)if(cfg.contains(key)){const auto& value=cfg.at(key);if(!value.is_string())throw std::runtime_error("plugin_configuration_type");const auto& text=value.get_ref<const std::string&>();if(text.size()>2048||text.find('\0')!=std::string::npos)throw std::runtime_error("plugin_configuration_string");}
    for(const auto* key:booleans)if(cfg.contains(key)&&!cfg.at(key).is_boolean())throw std::runtime_error("plugin_configuration_type");
    if(cfg.contains("proxyOrigin")){const auto proxy=cfg.at("proxyOrigin").get<std::string>();if(!proxy.empty()&&(!proxy.starts_with("http://")||!ayt::net::http::validOrigin(proxy)))throw std::runtime_error("plugin_configuration_proxy");}
}
inline PluginJson fetchJson(const std::string& origin,const std::string& path,const std::function<bool()>& cancelled){ayt::net::http::Request request;request.origin=origin;request.path=path;request.method="GET";request.useSystemProxy=true;request.timeout=std::chrono::seconds(10);request.maxResponseBytes=1024*1024;request.cancelled=cancelled;request.headers={{"User-Agent","HibernationAgent/0.2"},{"Accept","application/json"}};const auto result=ayt::net::http::perform(request);if(!result||result.httpStatus!=200)throw std::runtime_error("remote_request_failed");return PluginJson::parse(result.body);}
class BasePlugin:public IRolePlugin {
public:
    const char* getVersion()const override{return "0.2.0";}
    std::vector<const char*> getDependencies()const override{return {};}
    bool initialize()override{return true;}
};
}
