#include "../PluginCommon.h"
#include <Hibernation/PocketLights.h>
#include <random>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class Pocket final:public BasePlugin {
    mutable std::mutex mutex_;PluginHostContext host_;PocketLights lights_;bool paused_=false;double clock_=0;int scramble_=4,limit_=24;
    std::vector<double> starts_;std::mt19937 random_{std::random_device{}()};
    void emit(const char* kind){host_.emit({{"plugin","pocket"},{"kind",kind},{"game",lights_.observe().at("game")},{"occurred_ms",nowMs()},{"wake_model",false}});}
public:
    const char* getName()const override{return "pocket";}
    const char* getVersion()const override{return "0.1.0";}
    const char* getDescription()const override{return "A persistent finite nine-lantern puzzle for self-chosen quiet activity";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& error)override{
        try{const double scramble=number(cfg,"scrambleSteps",4,2,8),limit=number(cfg,"maximumMoves",24,12,64);if(std::floor(scramble)!=scramble||std::floor(limit)!=limit)throw 0;
            std::lock_guard lock(mutex_);host_=std::move(host);scramble_=static_cast<int>(scramble);limit_=static_cast<int>(limit);error.clear();return true;
        }catch(...){error="pocket_configuration";return false;}
    }
    PluginJson tools()const override{
        const auto identity=object({{"game_id",{{"type","string"},{"minLength",1},{"maxLength",32}}},{"expected_revision",{{"type","integer"},{"minimum",1},{"maximum",67}}}},PluginJson::array({"game_id","expected_revision"}));
        auto press=identity;press["properties"]["cell"]={{"type","integer"},{"minimum",0},{"maximum",8}};press["required"].push_back("cell");
        return PluginJson::array({
            tool("pocket.observe","看看自己口袋里九盏灯的小谜题和实际进度。没有额外窗口；只能按真实规则改变灯，不需要向用户逐步汇报。",object()),
            tool("pocket.start","开始一局可以解开的九盏灯。目标全熄灭，布局和步数上限由程序生成；有未结束的局时先继续或放下它。",object()),
            tool("pocket.press","按一格，程序翻转自身与上下左右邻格。cell从左上0到右下8。使用实际game_id与revision；胜负、步数与灯不能直接指定。",press),
            tool("pocket.leave","把未完成的九盏灯暂时放下，保留实际尝试结果，不假装已经解开。",identity)
        });
    }
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::lock_guard lock(mutex_);
        if(name=="pocket.observe")return toolSuccess(lights_.observe());if(paused_)return toolFailure("runtime_paused");
        std::string error;
        if(name=="pocket.start"){
            std::erase_if(starts_,[&](double at){return clock_-at>=300;});if(starts_.size()>=4)return toolFailure("pocket_start_rate");
            error=lights_.start(static_cast<std::uint32_t>(random_()),scramble_,limit_);if(error.empty()){starts_.push_back(clock_);emit("pocket.started");}
        }else if(name=="pocket.press"){
            error=lights_.press(args.at("game_id"),args.at("expected_revision"),args.at("cell"));if(error.empty())emit(lights_.game().at("status")=="won"?"pocket.solved":lights_.active()?"pocket.changed":"pocket.left");
        }else if(name=="pocket.leave"){
            error=lights_.leave(args.at("game_id"),args.at("expected_revision"));if(error.empty())emit("pocket.left");
        }else return toolFailure("tool_not_available");
        if(!error.empty())return toolFailure(error.c_str());auto result=lights_.observe();result["applied"]=true;result["completed"]=lights_.game().at("status")=="won";return toolSuccess(std::move(result));
    }
    void tick(double dt)override{std::lock_guard lock(mutex_);if(!paused_&&std::isfinite(dt))clock_+=std::clamp(dt,0.0,.25);}
    void event(const PluginJson& value)override{std::lock_guard lock(mutex_);if(value.value("kind",std::string{})=="runtime.paused")paused_=value.value("paused",false);}
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto result=lights_.snapshot();result["current"]=lights_.observe();return result;}
    bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);auto core=value;if(core.is_object())core.erase("current");return lights_.restore(core);}
    void shutdown()override{}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Pocket;}
