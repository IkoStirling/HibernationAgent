#include "../PluginCommon.h"
#include <Hibernation/QuietRhythm.h>
#include <random>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class Rhythm final:public BasePlugin {
    PluginHostContext host_;PluginJson cfg_;mutable std::recursive_mutex mutex_;QuietRhythm rhythm_;
    bool paused_=false,busy_=false,input_=false,actor_=false,bound_=false;double energy_=.72;std::string activity_="idle",actorState_="idle";std::int64_t actorSeen_=0;
    std::uint64_t epoch_=0,sequence_=0;std::mt19937 random_{std::random_device{}()};
    static int localHour(){SYSTEMTIME time{};GetLocalTime(&time);return time.wHour;}
    std::string nextToken(){return "rest-"+std::to_string(nowMs())+"-"+std::to_string(sequence_++);}
    void drain(){for(auto event:rhythm_.takeEvents()){event["plugin"]="rhythm";event["occurred_ms"]=nowMs();event["wake_model"]=false;host_.emit(std::move(event));}}
public:
    const char* getName()const override{return "rhythm";}
    const char* getDescription()const override{return "Bounded quiet intervals driven by actual active time and confirmed desktop poses";}
    std::vector<const char*> getDependencies()const override{return {"widget"};}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        try{configurationTypes(cfg,{},{"automaticRest"});number(cfg,"idleBeforeRestSeconds",300,5,7200);number(cfg,"restSeconds",180,15,1800);number(cfg,"automaticProbability",.3,0,1);std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);if(!bound_){rhythm_.postponeAutomatic(cfg_.value("idleBeforeRestSeconds",300.0));bound_=true;}issue.clear();return true;}catch(...){issue="rhythm_configuration";return false;}
    }
    PluginJson tools()const override{return PluginJson::array({
        tool("rhythm.observe","了解自己的安静休息、实际本地时段与大致精力，不推测用户是否在看屏幕。",object()),
        tool("rhythm.rest","选择短暂休息15至1800秒；先返回意图，交谈/语音完成后才确认实际姿态。新输入或拖动会取消，不要求用户等待或回应。",object({{"seconds",{{"type","number"},{"minimum",15},{"maximum",1800}}}},PluginJson::array({"seconds"}))),
        tool("rhythm.wake","结束自己当前的休息或待开始意图，不控制用户电脑。",object())
    });}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::lock_guard lock(mutex_);
        if(name=="rhythm.observe")return toolSuccess(rhythm_.observe(localHour(),energy_));
        if(name=="rhythm.wake"){const bool changed=rhythm_.wake("chosen_wake");drain();return toolSuccess({{"changed",changed},{"phase",rhythm_.phase()}});}
        if(name!="rhythm.rest")return toolFailure("tool_not_available");if(!host_.desktopEnabled)return toolFailure("desktop_required");if(paused_)return toolFailure("runtime_paused");if(!actor_||nowMs()-actorSeen_>2500)return toolFailure("desktop_character_required");
        const auto error=rhythm_.request(nextToken(),args.at("seconds"),epoch_,"model");if(!error.empty())return toolFailure(error.c_str());return toolSuccess({{"accepted",true},{"completed",false},{"token",rhythm_.token()}});
    }
    void tick(double dt)override{
        std::lock_guard lock(mutex_);const bool actor=actor_&&nowMs()-actorSeen_<2500;
        rhythm_.tick(dt,!paused_,busy_,input_,actor,actorState_,epoch_);
        if(!paused_&&host_.desktopEnabled&&cfg_.value("automaticRest",true)&&actor&&!busy_&&!input_&&rhythm_.phase()=="idle"&&rhythm_.idle()>=cfg_.value("idleBeforeRestSeconds",300.0)&&rhythm_.clock()>=rhythm_.nextAutomatic()){
            rhythm_.postponeAutomatic(60);if(QuietRhythm::mayRest(localHour(),energy_,activity_)&&std::bernoulli_distribution(cfg_.value("automaticProbability",.3))(random_))rhythm_.request(nextToken(),cfg_.value("restSeconds",180.0),epoch_,"automatic");
        }
        drain();
    }
    void event(const PluginJson& event)override{
        std::lock_guard lock(mutex_);const auto kind=event.value("kind",std::string{});
        if(kind=="runtime.paused")paused_=event.value("paused",false);
        if(kind=="runtime.attention"){busy_=event.value("busy",false);input_=event.value("input_active",false);epoch_=event.value("epoch",std::uint64_t(0));}
        if(kind=="runtime.character"){const auto energy=event.value("energy",energy_);if(std::isfinite(energy)&&energy>=0&&energy<=1)energy_=energy;activity_=event.value("activity",activity_);}
        if(kind=="widget.position"){actor_=event.value("active",false);actorState_=event.value("state",actorState_);actorSeen_=nowMs();}
        if(kind=="widget.rest_started")rhythm_.acknowledged(event.value("token",std::string{}));
        if(kind=="widget.rest_interrupted"&&event.value("token",std::string{})==rhythm_.token())rhythm_.wake(event.value("reason",std::string("character_interrupted")));
        if(kind=="widget.interacted")rhythm_.wake("pointer_interaction");
        drain();
    }
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto value=rhythm_.snapshot();value["desktop"]=host_.desktopEnabled;value["local_hour"]=localHour();return value;}
    bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);return rhythm_.restore(value);}
    void shutdown()override{std::lock_guard lock(mutex_);rhythm_.wake("plugin_disabled");drain();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Rhythm;}
