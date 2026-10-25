#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
#include <cstdint>
namespace hibernation {
// Program observations cannot prove that a person heard or ignored a voice.
inline nlohmann::json assessAudioDelivery(const nlohmann::json& output,bool ready,int configuredVolume,std::int64_t now){
    using Json=nlohmann::json;
    auto finding=[](const char* code,const char* message){return Json{{"code",code},{"message",message},{"heard_confirmed",false}};};
    if(!ready)return finding("device_unavailable","声音输出尚未就绪。文字仍可正常显示。");
    if(configuredVolume==0)return finding("app_volume_zero","应用里的朗读音量为零。");
    const auto checked=output.value("checked_at_ms",std::int64_t(0));
    if(!checked||now<checked||now-checked>15000)return finding("observation_stale","声音输出状态尚未更新。");
    const auto status=output.value("query_state",std::string("unavailable"));
    if(status=="unsupported")return finding("unsupported","此系统暂不提供声音通道诊断。");
    if(status=="unavailable")return finding("unknown","暂时无法读取系统声音输出状态。");
    auto blocked=[](const Json& volume){if(!volume.value("known",false))return false;if(volume.value("muted",false)||volume.value("master",1.0)<=0.0001)return true;const auto channels=volume.value("channels",Json::array());return !channels.empty()&&std::all_of(channels.begin(),channels.end(),[](const auto& gain){return gain.template get<double>()<=0.0001;});};
    unsigned active=0,blockedActive=0,defaultActive=0,fullyKnown=0;
    for(const auto& session:output.value("own_sessions",Json::array()))if(session.value("state",std::string{})=="active"){
        ++active;if(session.value("default_for_console",false)||session.value("default_for_multimedia",false))++defaultActive;
        const auto own=session.value("session",Json::object()),endpoint=session.value("endpoint",Json::object());
        if(own.value("known",false)&&endpoint.value("known",false)&&!own.value("channels",Json::array()).empty()&&!endpoint.value("channels",Json::array()).empty())++fullyKnown;
        if(blocked(session.value("session",Json::object()))||blocked(session.value("endpoint",Json::object())))++blockedActive;
    }
    if(active&&blockedActive==active)return finding("active_sessions_muted","已观测到的本程序活动声音会话处于静音或零音量。");
    if(active&&(status=="partial"||output.value("truncated",false)))return finding("partially_unknown","只观测到部分声音通道，暂不能完整确认输出路径。");
    if(active&&fullyKnown<active)return finding("volume_unknown","已发现活动会话，但部分音量或声道状态无法读取。");
    if(active&&!defaultActive)return finding("different_output","观测到的本程序声音会话使用了系统默认设备以外的输出。");
    if(active)return finding("no_known_blocker","已发现本程序的活动声音会话，未发现整体静音；这不能确认声音已被听到。");
    const auto console=output.value("console",Json::object()),multimedia=output.value("multimedia",Json::object());
    if(!console.value("available",false)&&!multimedia.value("available",false))return finding("default_missing","系统当前没有可用的默认播放设备。");
    if(blocked(console.value("volume",Json::object()))&&blocked(multimedia.value("volume",Json::object())))return finding("defaults_muted","系统默认播放设备处于静音或零音量。");
    return finding("idle_or_unknown","当前没有观测到本程序的活动声音会话；可能尚未朗读，不能据此判定无声。");
}
}
