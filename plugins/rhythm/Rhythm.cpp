#include "../PluginCommon.h"
#include <Hibernation/QuietRhythm.h>
#include <Hibernation/CircadianRhythm.h>
#include <Hibernation/CircadianProtocol.h>
#include <random>
#include <tuple>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class Rhythm final:public BasePlugin {
    PluginHostContext host_;PluginJson cfg_;mutable std::recursive_mutex mutex_;QuietRhythm rhythm_;CircadianRhythm circadian_;PluginJson lastDisturbance_=PluginJson::object();
    bool autonomous_=false,paused_=false,busy_=false,input_=false,actor_=false,bound_=false,activityPending_=false;double energy_=.72;std::string activity_="idle",actorState_="idle";std::int64_t actorSeen_=0;
    std::uint64_t epoch_=0,sequence_=0;std::mt19937 random_{std::random_device{}()};
    static int localHour(){SYSTEMTIME time{};GetLocalTime(&time);return time.wHour;}
    std::string nextToken(){return "rest-"+std::to_string(nowMs())+"-"+std::to_string(sequence_++);}
    void drain(){auto events=rhythm_.takeEvents();for(auto e:circadian_.takeEvents())events.push_back(std::move(e));for(auto event:events){event["plugin"]="rhythm";event["occurred_ms"]=nowMs();event["wake_model"]=false;host_.emit(std::move(event));}}
    static std::tuple<int,std::string,std::string> calendar(){SYSTEMTIME t{};GetLocalTime(&t);auto date=[](const SYSTEMTIME& v){return std::to_string(v.wYear)+"-"+std::to_string(v.wMonth)+"-"+std::to_string(v.wDay);};FILETIME f{};SystemTimeToFileTime(&t,&f);ULARGE_INTEGER n{};n.LowPart=f.dwLowDateTime;n.HighPart=f.dwHighDateTime;n.QuadPart-=864000000000ULL;f.dwLowDateTime=n.LowPart;f.dwHighDateTime=n.HighPart;SYSTEMTIME yesterday{};FileTimeToSystemTime(&f,&yesterday);return {t.wHour*60+t.wMinute,date(t),date(yesterday)};}
    double draw(){return std::generate_canonical<double,32>(random_);}
public:
    const char* getName()const override{return "rhythm";}
    const char* getDescription()const override{return "Bounded quiet intervals driven by actual active time and confirmed desktop poses";}
    std::vector<const char*> getDependencies()const override{return {"widget"};}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        try{configurationTypes(cfg,{},{"automaticRest","circadianEnabled"});number(cfg,"idleBeforeRestSeconds",300,5,7200);number(cfg,"restSeconds",180,15,1800);number(cfg,"automaticProbability",.3,0,1);std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);if(!cfg_.value("circadianEnabled",true))circadian_.unavailable("administrator_disabled");if(!bound_){rhythm_.postponeAutomatic(cfg_.value("idleBeforeRestSeconds",300.0));bound_=true;}drain();issue.clear();return true;}catch(...){issue="rhythm_configuration";return false;}
    }
    PluginJson tools()const override{return PluginJson::array({
        tool("rhythm.observe","了解自己的安静休息、实际本地时段与大致精力，不推测用户是否在看屏幕。",object()),
        tool("rhythm.rest","选择短暂休息15至1800秒；先返回意图，交谈/语音完成后才确认实际姿态。新输入或拖动会取消，不要求用户等待或回应。",object({{"seconds",{{"type","number"},{"minimum",15},{"maximum",1800}}}},PluginJson::array({"seconds"}))),
        tool("rhythm.wake","自主结束短休或长睡，也可供管理员强制唤醒，不控制用户电脑。",object()),
        tool("rhythm.configure","按自己的经历选择虚拟作息；先 rhythm.observe 获取 preferences_revision。时间是本机当地午夜后的分钟数，可跨午夜，时长1至12小时。不会强制现在睡觉；关闭 enabled 可取消作息。醒来不必抱怨，不强制解释内部数值。",circadianProposalSchema())
    });}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::lock_guard lock(mutex_);
        if(name=="rhythm.observe"){auto value=rhythm_.observe(localHour(),energy_);value["circadian"]=circadian_.observe();value["circadian_available"]=cfg_.value("circadianEnabled",true);return toolSuccess(value);}
        if(name=="rhythm.configure"){if(!cfg_.value("circadianEnabled",true))return toolFailure("circadian_disabled");const auto prior=circadian_.observe().at("preferences_revision");if(!circadian_.configure(args.at("preferences"),args.at("expected_revision")))return toolFailure("circadian_revision");const auto current=circadian_.observe();const bool unchanged=current.at("preferences_revision")==prior;if(!unchanged)host_.emit({{"kind","rhythm.preferences_changed"},{"plugin","rhythm"},{"previous_revision",prior},{"preferences_revision",current.at("preferences_revision")},{"preferences",current.at("preferences")},{"wake_model",false}});drain();return toolSuccess({{"accepted",true},{"completed",false},{"deduplicated",unchanged},{"persistence",unchanged?"unchanged":"host_checkpoint_pending"},{"circadian",circadian_.observe()}});}
        if(name=="rhythm.wake"){const bool pending=circadian_.pending();circadian_.wake("chosen_wake",0,true);const bool changed=rhythm_.wake("chosen_wake")||pending;drain();return toolSuccess({{"changed",changed},{"phase",rhythm_.phase()},{"circadian",circadian_.observe()}});}
        if(name!="rhythm.rest")return toolFailure("tool_not_available");if(!host_.desktopEnabled)return toolFailure("desktop_required");if(paused_)return toolFailure("runtime_paused");if(!actor_||nowMs()-actorSeen_>2500)return toolFailure("desktop_character_required");
        if(circadian_.pending())return toolFailure("circadian_rest_pending");const auto error=rhythm_.request(nextToken(),args.at("seconds"),epoch_,"model");if(!error.empty())return toolFailure(error.c_str());return toolSuccess({{"accepted",true},{"completed",false},{"token",rhythm_.token()}});
    }
    void tick(double dt)override{
        std::lock_guard lock(mutex_);const bool actor=actor_&&nowMs()-actorSeen_<2500;
        rhythm_.tick(dt,!paused_,busy_,input_,actor,actorState_,epoch_);
        if(!paused_){const auto [minute,today,yesterday]=calendar();circadian_.reconcileWindow(minute,today,yesterday);}
        circadian_.tick(dt,!paused_,actor,busy_||input_,actorState_);
        if(cfg_.value("circadianEnabled",true)&&host_.desktopEnabled&&!paused_&&actor&&!busy_&&!input_&&!activityPending_&&rhythm_.phase()=="idle"&&rhythm_.idle()>=cfg_.value("idleBeforeRestSeconds",300.0)){const auto [minute,today,yesterday]=calendar();if(circadian_.mayStart(minute,today,yesterday))circadian_.request(nextToken(),minute,today,yesterday);}
        if(!circadian_.pending()&&!autonomous_&&!paused_&&host_.desktopEnabled&&cfg_.value("automaticRest",true)&&actor&&!busy_&&!input_&&rhythm_.phase()=="idle"&&rhythm_.idle()>=cfg_.value("idleBeforeRestSeconds",300.0)&&rhythm_.clock()>=rhythm_.nextAutomatic()){
            rhythm_.postponeAutomatic(60);if(QuietRhythm::mayRest(localHour(),energy_,activity_)&&std::bernoulli_distribution(cfg_.value("automaticProbability",.3))(random_))rhythm_.request(nextToken(),cfg_.value("restSeconds",180.0),epoch_,"automatic");
        }
        drain();
    }
    void event(const PluginJson& event)override{
        std::lock_guard lock(mutex_);const auto kind=event.value("kind",std::string{});
        if(kind=="runtime.autonomy")autonomous_=event.value("enabled",false);
        if(kind=="runtime.activity_state")activityPending_=event.value("pending",false);
        if(kind=="runtime.activity_cancel")rhythm_.wake("activity_cancelled");
        if(kind=="runtime.paused")paused_=event.value("paused",false);
        if(kind=="runtime.attention"){busy_=event.value("busy",false);input_=event.value("input_active",false);epoch_=event.value("epoch",std::uint64_t(0));}
        if(kind=="runtime.character"){const auto energy=event.value("energy",energy_);if(std::isfinite(energy)&&energy>=0&&energy<=1)energy_=energy;activity_=event.value("activity",activity_);}
        if(kind=="widget.position"){actor_=event.value("active",false);actorState_=event.value("state",actorState_);actorSeen_=nowMs();}
        if(kind=="runtime.circadian.disturbance"){const bool woke=circadian_.wake(event.value("reason",std::string("message")),draw(),event.value("force",false));lastDisturbance_={{"id",event.value("id",std::string{})},{"woke",woke},{"sleeping",circadian_.sleeping()}};}
        if(kind=="widget.rest_started"){rhythm_.acknowledged(event.value("token",std::string{}));circadian_.acknowledge(event.value("token",std::string{}));}
        if(kind=="widget.rest_interrupted"&&event.value("token",std::string{})==rhythm_.token())rhythm_.wake(event.value("reason",std::string("character_interrupted")));
        if(kind=="widget.rest_interrupted"&&event.value("token",std::string{})==circadian_.token())circadian_.unavailable(event.value("reason",std::string("character_interrupted")));
        if(kind=="widget.interacted"){rhythm_.wake("pointer_interaction");if(event.value("interaction",std::string{})=="drag")circadian_.wake("pointer_interaction",draw());}
        drain();
    }
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto value=rhythm_.snapshot();value["desktop"]=host_.desktopEnabled;value["local_hour"]=localHour();value["circadian"]=circadian_.snapshot();value["last_disturbance"]=lastDisturbance_;
        // Host scheduling metadata only. Character projections and rhythm.observe
        // intentionally omit this bit; the existing activityPending gate remains.
        const auto [minute,today,yesterday]=calendar();value["circadian_admission_ready"]=host_.desktopEnabled&&cfg_.value("circadianEnabled",true)&&circadian_.mayStart(minute,today,yesterday);return value;}
    bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);if(value.contains("circadian")){CircadianRhythm candidate;if(!candidate.restore(value.at("circadian")))return false;if(!rhythm_.restore(value))return false;circadian_=std::move(candidate);if(!cfg_.value("circadianEnabled",true))circadian_.unavailable("administrator_disabled");return true;}return rhythm_.restore(value);}
    void shutdown()override{std::lock_guard lock(mutex_);rhythm_.wake("plugin_disabled");circadian_.unavailable("plugin_disabled");drain();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Rhythm;}
