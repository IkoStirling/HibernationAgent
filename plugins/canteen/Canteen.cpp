#include "../PluginCommon.h"
#include <Hibernation/CanteenKitchen.h>
#include <mutex>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class Canteen final:public BasePlugin {
 PluginHostContext host_;canteen::Kitchen kitchen_;mutable std::mutex mutex_;bool paused_=true,offline_=false,restoreFailed_=false;PluginJson rejectedSnapshot_=PluginJson::object();double deliveryRetry_=0;
 void drain(){for(auto e:kitchen_.takeEvents()){e["plugin"]="canteen";e["occurred_ms"]=nowMs();e["wake_model"]=false;host_.emit(std::move(e));}}
public:
 const char* getName()const override{return "canteen";}
 const char* getVersion()const override{return "0.1.0";}
 const char* getDescription()const override{return "A small persistent kitchen with shared BSimmer recipe rules";}
 std::vector<const char*> getDependencies()const override{return {"habitat"};}
 bool bind(PluginHostContext host,PluginJson config,std::string& issue)override{try{
  configurationTypes(config,{},{"automatic"});const auto interval=number(config,"automaticIntervalSeconds",900,60,43200),supply=number(config,"supplyIntervalSeconds",3600,300,86400),life=number(config,"shelfLifeSeconds",1800,60,43200);
  std::lock_guard lock(mutex_);host_=std::move(host);kitchen_.configure(config.value("automatic",true),interval,supply,life);issue.clear();return true;
 }catch(...){issue="canteen_configuration";return false;}}
 PluginJson tools()const override{const auto id=PluginJson{{"type","string"},{"minLength",1},{"maxLength",96}};return PluginJson::array({
  tool("canteen.observe","查看食堂菜单、真实可用食材、正在制作的料理、成品架和最近结果；无需每次汇报。",object()),
  tool("canteen.cook","从现有库存开始制作一道料理。recipe_id为bread/pudding/tea；可选择提交ingredient_ids，顺序无关，但必须匹配配方。accepted只是开始，材料会预留，完成后进入成品架。",object({{"recipe_id",{{"type","string"},{"enum",PluginJson::array({"bread","pudding","tea"})}}},{"ingredient_ids",{{"type","array"},{"minItems",1},{"maxItems",4},{"items",{{"type","string"},{"maxLength",32}}}}}},PluginJson::array({"recipe_id"}))),
  tool("canteen.cancel","取消指定制作任务并释放尚未消耗的食材，不能撤销已完成料理。",object({{"job_id",id}},PluginJson::array({"job_id"}))),
  tool("canteen.take","把成品架上指定料理放到桌面。先返回待交付；实际物件发布后会收到canteen.taken。之后仍需通过habitat.act走近食用，不等于已经吃过。",object({{"batch_id",id}},PluginJson::array({"batch_id"}))),
  tool("canteen.discard","丢弃成品架上的指定料理，保留真实记录；不能改成新鲜料理或退回食材。",object({{"batch_id",id}},PluginJson::array({"batch_id"})))
 });}
 PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancel)override{if(cancel&&cancel())return toolFailure("cancelled");std::lock_guard lock(mutex_);if(restoreFailed_)return toolFailure("canteen_saved_state_invalid");if(name=="canteen.observe")return toolSuccess(kitchen_.observe());if(paused_||offline_)return toolFailure("runtime_paused");PluginJson result;
  if(name=="canteen.cook")result=kitchen_.cook(args.at("recipe_id"),args.value("ingredient_ids",PluginJson(nullptr)));
  else if(name=="canteen.cancel")result=kitchen_.cancel(args.at("job_id"));
  else if(name=="canteen.take"){if(!host_.desktopEnabled)return toolFailure("desktop_required");result=kitchen_.take(args.at("batch_id"));}
  else if(name=="canteen.discard")result=kitchen_.discard(args.at("batch_id"));else return toolFailure("tool_not_available");drain();return result;
 }
 void tick(double elapsed)override{std::lock_guard lock(mutex_);if(paused_||offline_||restoreFailed_)return;const double dt=std::isfinite(elapsed)?std::clamp(elapsed,0.0,.25):0;kitchen_.tick(dt);deliveryRetry_+=dt;if(deliveryRetry_>=2){deliveryRetry_=0;kitchen_.retryDelivery();}drain();}
 void event(const PluginJson& event)override{std::lock_guard lock(mutex_);const auto kind=event.value("kind",std::string{});if(kind=="runtime.paused"){paused_=event.value("paused",false);offline_=event.value("offline",false);return;}if(kind=="runtime.canteen.delivery_result"){kitchen_.delivered(event);drain();}}
 PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto v=restoreFailed_?rejectedSnapshot_:kitchen_.snapshot();v["current"]=restoreFailed_?PluginJson{{"issue","canteen_saved_state_invalid"},{"can_cook",PluginJson::array()},{"ready_count",0}}:kitchen_.observe();v["desktop"]=host_.desktopEnabled;return v;}
 void shutdown()override{}
 bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);const bool ok=kitchen_.restore(value);restoreFailed_=!ok;if(ok)deliveryRetry_=2;else rejectedSnapshot_=value;return ok;}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Canteen;}
