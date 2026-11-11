#pragma once
#include <AYPlugin/IPlugin.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <string>
namespace hibernation {
using PluginJson=nlohmann::json;
// Host-only delivery adapter. Never declared in the model's tool catalogue.
class IChannelReplySink {public:virtual ~IChannelReplySink()=default;virtual PluginJson enqueueChannelReply(const PluginJson& envelope)=0;};
struct PluginHostContext {
    std::filesystem::path dataRoot,projectRoot;
    bool desktopEnabled=false;
    std::function<void(PluginJson)> emit;
    std::filesystem::path draftsRoot,materialsRoot;
    std::function<PluginJson(const std::string&,const PluginJson&,const std::function<bool()>&)> browser;
};
/// v2 native ABI: same compiler/runtime as the host. Native plugins are trusted
/// installed code, not a sandbox. bind/tick/event/shutdown run on the UI owner;
/// invoke runs on the bounded model worker and must cooperate with cancellation.
class IRolePlugin:public ayt::plugin::IPlugin {
public:
    virtual int roleApiVersion()const{return 2;}
    virtual bool bind(PluginHostContext host,PluginJson configuration,std::string& error)=0;
    virtual PluginJson tools()const=0;
    virtual PluginJson invoke(const std::string& tool,const PluginJson& args,const std::function<bool()>& cancelled)=0;
    virtual void tick(double elapsedSeconds){}
    virtual void event(const PluginJson& value){}
    virtual PluginJson snapshot()const{return PluginJson::object();}
    virtual bool restore(const PluginJson& value){return value.is_object();}
};
inline PluginJson toolSuccess(PluginJson result=PluginJson::object()){return {{"ok",true},{"result",std::move(result)}};}
inline PluginJson toolFailure(const char* code,bool retryable=false){return {{"ok",false},{"error",{{"code",code},{"retryable",retryable}}}};}
}
