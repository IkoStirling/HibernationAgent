#pragma once
#include "Ledger.h"
#include <Hibernation/DesktopServices.h>
namespace hibernation::napcat {
inline PluginJson viewImageTool(){return tool("napcat.view_image","读取当前 QQ 发言人刚发来的实际图片/表情包。id 必须来自 channel_identity.received_images；这些只是附件索引，读取成功且 image_attached=true 才能描述画面。可用于图片后接文字的连续会话。动态图只提供首帧；QQ内置 face 只有表情ID，不等于图片。",object({{"id",{{"type","string"},{"maxLength",64}}}},PluginJson::array({"id"})));}
struct ImageAddress {std::string origin,path;explicit operator bool()const{return !origin.empty();}};
inline ImageAddress imageAddress(std::string url,const std::string& apiOrigin){
    if(url.empty()||url.size()>8192||url.find_first_of("\r\n\\#")!=std::string::npos||url.find('\0')!=std::string::npos)return {};
    const auto scheme=url.find("://");if(scheme==std::string::npos)return {};const auto path=url.find_first_of("/?",scheme+3);auto origin=url.substr(0,path);auto route=path==std::string::npos?"/":url.substr(path);if(route.starts_with('?'))route="/"+route;
    if(!ayt::net::http::validOrigin(origin))return {};
    // No token on image GETs; redirects are disabled by AYNetwork.
    if(origin==apiOrigin)return {origin,route};
    auto host=origin.substr(scheme+3);std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});
    const bool qq=host=="multimedia.nt.qq.com.cn"||host=="qq.com"||host.ends_with(".qq.com")||host=="qpic.cn"||host.ends_with(".qpic.cn")||host.ends_with(".gtimg.cn")||host.ends_with(".gtimg.com");
    if(!qq||(origin.substr(0,scheme)!="http"&&origin.substr(0,scheme)!="https"))return {};
    return {"https://"+host,route};
}
template<class Api>std::string incomingOriginal(const PluginJson& image,const Settings& settings,Api api,const std::function<bool()>& cancelled){
    auto fetch=[&](const std::string& url){const auto address=imageAddress(url,settings.origin);if(!address)return std::string{};ayt::net::http::Request request;request.origin=address.origin;request.path=address.path;request.method="GET";request.timeout=std::chrono::seconds(8);request.maxResponseBytes=2*1024*1024;request.cancelled=cancelled;request.useSystemProxy=false;const auto response=ayt::net::http::perform(request);return response&&response.httpStatus==200?response.body:std::string{};};
    auto bytes=fetch(image.value("url",std::string{}));
    if(bytes.empty()&&!image.value("file",std::string{}).empty()&&!(cancelled&&cancelled())){const auto response=api("get_image",PluginJson{{"file",image.at("file")}});const auto data=PluginJson::parse(response.body,nullptr,false);if(response&&response.httpStatus==200&&data.is_object()&&data.value("status",std::string{})=="ok"&&data.value("data",PluginJson::object()).is_object())bytes=fetch(data.at("data").value("url",std::string{}));}
    return bytes;
}
template<class Api>PluginJson readIncomingImage(Ledger& ledger,const PluginJson& image,const Settings& settings,Api api,const std::function<bool()>& cancelled){
    const auto id=image.at("id").get<std::string>();const auto cached=ledger.incomingImage(id);if(!cached.empty())return cached;
    auto fetch=[&](const std::string& url){const auto address=imageAddress(url,settings.origin);if(!address)return toolFailure("qq_image_url_unavailable");ayt::net::http::Request request;request.origin=address.origin;request.path=address.path;request.method="GET";request.timeout=std::chrono::seconds(8);request.maxResponseBytes=8*1024*1024;request.cancelled=cancelled;request.useSystemProxy=false;const auto response=ayt::net::http::perform(request);if(!response||response.httpStatus!=200)return toolFailure("qq_image_download_failed");return hibernation::prepareImageObservation(response.body);};
    auto result=fetch(image.value("url",std::string{}));
    if(!result.value("ok",false)&&!image.value("file",std::string{}).empty()&&!(cancelled&&cancelled())){
        const auto response=api("get_image",PluginJson{{"file",image.at("file")}});if(response&&response.httpStatus==200){const auto data=PluginJson::parse(response.body,nullptr,false);if(data.is_object()&&data.value("status",std::string{})=="ok"&&data.value("data",PluginJson::object()).is_object())result=fetch(data.at("data").value("url",std::string{}));}
    }
    if(result.value("ok",false)){result["result"]["attachment_id"]=id;result["result"]["message_id"]=image.at("message_id");result["result"]["origin"]="received_qq_image";ledger.cacheIncomingImage(id,result);}return result;
}
}
