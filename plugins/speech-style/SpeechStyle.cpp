#include "../PluginCommon.h"
#include <Hibernation/SpeechStyle.h>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class SpeechStyle final:public BasePlugin {
    mutable std::mutex mutex_;
    PluginHostContext host_;
    PluginJson configuration_=speechStyle::defaults();
    std::string selected_="natural";
    bool paused_=false;
    PluginJson preferenceUnlocked()const{auto selected=configuration_;selected["activePreset"]=selected_;return speechStyle::preference(selected);}
    PluginJson viewUnlocked()const{
        auto catalog=PluginJson::array();for(const auto& preset:configuration_.at("presets"))catalog.push_back({{"id",preset.at("id")},{"name",preset.at("name")}});
        return {{"preference",preferenceUnlocked()},{"catalog",std::move(catalog)},{"allowModelSwitch",configuration_.at("allowModelSwitch")}};
    }
public:
    const char* getName()const override{return "speech-style";}
    const char* getVersion()const override{return "0.1.0";}
    const char* getDescription()const override{return "User-selected expression preferences without changing character identity, memories or permissions";}
    bool bind(PluginHostContext host,PluginJson input,std::string& issue)override{
        try{
            // Finish validation before changing either host or active selection.
            auto normalized=speechStyle::normalize(input);const auto selected=normalized.at("activePreset").get<std::string>();
            std::lock_guard lock(mutex_);host_=std::move(host);configuration_=std::move(normalized);selected_=selected;issue.clear();return true;
        }catch(...){issue="speech_style_configuration";return false;}
    }
    PluginJson tools()const override{
        std::lock_guard lock(mutex_);auto available=PluginJson::array({tool("speech_style.observe","查看当前表达偏好与可选名称。偏好只影响措辞，不是经历、事实、身份、关系或工具权限，也不需要向用户宣布。",object())});
        if(configuration_.at("enabled").get<bool>()&&configuration_.at("allowModelSwitch").get<bool>()){
            auto ids=PluginJson::array();for(const auto& preset:configuration_.at("presets"))ids.push_back(preset.at("id"));available.push_back(tool("speech_style.select","仅在当前交流合适时选择已有的表达预设。不创建或改写预设，不改变人设与权限，不为了使用工具而反复切换。",object({{"presetId",{{"type","string"},{"enum",std::move(ids)}}}},PluginJson::array({"presetId"}))));
        }
        return available;
    }
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::function<void(PluginJson)> emit;PluginJson event,result;
        {
            std::lock_guard lock(mutex_);
            if(name=="speech_style.observe"){if(!args.is_object()||!args.empty())return toolFailure("speech_style_arguments");return toolSuccess(viewUnlocked());}
            if(name!="speech_style.select")return toolFailure("tool_not_available");
            if(!configuration_.at("enabled").get<bool>())return toolFailure("speech_style_disabled");if(!configuration_.at("allowModelSwitch").get<bool>())return toolFailure("speech_style_switch_disabled");if(paused_)return toolFailure("runtime_paused");
            if(!args.is_object()||args.size()!=1||!args.contains("presetId")||!args.at("presetId").is_string())return toolFailure("speech_style_arguments");const auto id=args.at("presetId").get<std::string>();bool found=false;for(const auto& preset:configuration_.at("presets"))if(preset.at("id")==id){found=true;break;}if(!found)return toolFailure("speech_style_preset_unknown");
            if(cancelled&&cancelled())return toolFailure("cancelled");const bool changed=selected_!=id;selected_=id;result={{"preference",preferenceUnlocked()},{"changed",changed}};
            if(changed){emit=host_.emit;event={{"plugin","speech-style"},{"kind","style.changed"},{"preset_id",selected_},{"wake",false},{"wake_model",false},{"occurred_ms",nowMs()}};}
        }
        if(emit)emit(std::move(event));return toolSuccess(std::move(result));
    }
    void tick(double)override{}
    void event(const PluginJson& input)override{std::lock_guard lock(mutex_);if(input.value("kind",std::string{})=="runtime.paused")paused_=input.value("paused",false);}
    PluginJson snapshot()const override{
        std::lock_guard lock(mutex_);auto snapshot=viewUnlocked();snapshot["version"]=1;snapshot["selectedPreset"]=selected_;
        // Private restart provenance, not model context: a stale snapshot can
        // restore a model choice only when the owner's configuration is identical.
        snapshot["configurationSignature"]=configuration_.dump();return snapshot;
    }
    bool restore(const PluginJson& snapshot)override{
        try{
            if(!snapshot.is_object()||snapshot.value("version",0)!=1||!snapshot.contains("configurationSignature")||!snapshot.at("configurationSignature").is_string()||!snapshot.contains("selectedPreset")||!snapshot.at("selectedPreset").is_string()||snapshot.dump().size()>65536)return false;
            const auto signature=snapshot.at("configurationSignature").get<std::string>();if(signature.size()>16384)return false;const auto selected=snapshot.at("selectedPreset").get<std::string>();if(!speechStyle::detail::validId(selected))return false;
            std::lock_guard lock(mutex_);if(signature!=configuration_.dump()||!configuration_.at("allowModelSwitch").get<bool>())return true;auto candidate=configuration_;candidate["activePreset"]=selected;(void)speechStyle::normalize(candidate);selected_=selected;return true;
        }catch(...){return false;}
    }
    void shutdown()override{}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new SpeechStyle;}
