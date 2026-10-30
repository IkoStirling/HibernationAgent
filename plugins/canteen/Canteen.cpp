#include "../PluginCommon.h"
#include <Hibernation/CanteenKitchen.h>
#include <Hibernation/ForagingBridge.h>
#include <mutex>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class Canteen final:public BasePlugin,public IForagingInventory {
 PluginHostContext host_;canteen::Kitchen kitchen_;mutable std::mutex mutex_;bool paused_=true,offline_=false,restoreFailed_=false,allowOrders_=true;PluginJson rejectedSnapshot_=PluginJson::object();double deliveryRetry_=0;
 void drain(){for(auto e:kitchen_.takeEvents()){e["plugin"]="canteen";e["occurred_ms"]=nowMs();e["wake_model"]=false;host_.emit(std::move(e));}}
public:
 const char* getName()const override{return "canteen";}
 const char* getVersion()const override{return "0.3.0";}
 const char* getDescription()const override{return "A small persistent kitchen with shared BSimmer recipe rules";}
 std::vector<const char*> getDependencies()const override{return {"habitat"};}
 bool bind(PluginHostContext host,PluginJson config,std::string& issue)override{try{
  configurationTypes(config,{},{"automatic","allowSupplyOrders"});const auto interval=number(config,"automaticIntervalSeconds",900,60,43200),supply=number(config,"supplyIntervalSeconds",3600,300,86400),life=number(config,"shelfLifeSeconds",1800,60,43200);
  std::lock_guard lock(mutex_);host_=std::move(host);kitchen_.configure(config.value("automatic",true),interval,supply,life);if(config.contains("equipment")&&!kitchen_.configureEquipment(config.at("equipment").get<std::vector<std::string>>())){issue="canteen_equipment_configuration";return false;}allowOrders_=config.value("allowSupplyOrders",true);issue.clear();return true;
 }catch(...){issue="canteen_configuration";return false;}}
 PluginJson tools()const override{const auto id=PluginJson{{"type","string"},{"minLength",1},{"maxLength",96}};return PluginJson::array({
  tool("canteen.menu","按名称或编号搜索共享菜谱，分页查看；cuisine可为chinese/western/fantasy/foundation/legacy，kind为dish或preparation。空查询列第一页，完整配方用canteen.recipe。",object({{"query",{{"type","string"},{"maxLength",80}}},{"cuisine",{{"type","string"},{"maxLength",32}}},{"kind",{{"type","string"},{"enum",PluginJson::array({"dish","preparation"})}}},{"offset",{{"type","integer"},{"minimum",0},{"maximum",512}}},{"limit",{{"type","integer"},{"minimum",1},{"maximum",12}}}})),
  tool("canteen.recipe","查看一道真实菜谱的定量原料、加工步骤、器具、中间产物、来源和游戏化改编说明。recipe_id来自menu，不猜编号。",object({{"recipe_id",{{"type","string"},{"minLength",1},{"maxLength",64}}}},PluginJson::array({"recipe_id"}))),
  tool("canteen.plan","只读规划从现有仓库原料到目标料理的完整加工链，列出实际缺料和缺少的器具；不会自动补给、消耗或制作。",object({{"recipe_id",{{"type","string"},{"minLength",1},{"maxLength",64}}}},PluginJson::array({"recipe_id"}))),
  tool("canteen.supplies","按名称或编号查询可申请的原料，分页返回；这是程序虚拟补给目录，不是现实商店。",object({{"query",{{"type","string"},{"maxLength",80}}},{"offset",{{"type","integer"},{"minimum",0},{"maximum",512}}},{"limit",{{"type","integer"},{"minimum",1},{"maximum",12}}}})),
  tool("canteen.request_supplies","申请少量程序虚拟原料到仓库，不能申请成品或中间产物。一次最多8种、合计16份，每种1至4份，冷却30秒；受用户补给开关控制。成功回执才表示已入库，不是现实采购。",object({{"items",{{"type","array"},{"minItems",1},{"maxItems",8},{"items",object({{"ingredient_id",{{"type","string"},{"minLength",1},{"maxLength",64}}},{"quantity",{{"type","integer"},{"minimum",1},{"maximum",4}}}},PluginJson::array({"ingredient_id","quantity"}))}}}},PluginJson::array({"items"}))),
  tool("canteen.inventory","查看随身背包和食堂共用仓库中的物品、来源、可食用性和剩余新鲜时间。",object()),
  tool("canteen.store","将背包中的食材和食物一次存入仓库。保留物品身份、来源和剩余新鲜时间；满仓则不移动。",object()),
  tool("canteen.move_supply","将已有物品在背包(backpack)和仓库(pantry)之间移动。item_id来自inventory，不能使用桌面object_id。",object({{"item_id",{{"type","string"},{"minLength",1},{"maxLength",128}}},{"destination",{{"enum",PluginJson::array({"backpack","pantry"})}}}},PluginJson::array({"item_id","destination"}))),
  tool("canteen.eat_supply","直接吃掉背包或仓库中已有的一份可食用物品。item_id来自inventory；原料不一定能直接吃，不凭描述判断。",object({{"item_id",{{"type","string"},{"minLength",1},{"maxLength",128}}}},PluginJson::array({"item_id"}))),
  tool("canteen.observe","查看食堂菜单、真实可用食材、正在制作的料理、成品架和最近结果；无需每次汇报。",object()),
  tool("canteen.cook","从现有库存开始制作一道料理。recipe_id来自canteen.menu/recipe，支持完整加工链；可选择提交ingredient_ids，顺序无关，但必须匹配配方。accepted只是开始，材料会预留，完成后进入成品架。",object({{"recipe_id",{{"type","string"},{"minLength",1},{"maxLength",64}}},{"ingredient_ids",{{"type","array"},{"minItems",1},{"maxItems",16},{"items",{{"type","string"},{"maxLength",64}}}}}},PluginJson::array({"recipe_id"}))),
  tool("canteen.cancel","取消指定制作任务并释放尚未消耗的食材，不能撤销已完成料理。",object({{"job_id",id}},PluginJson::array({"job_id"}))),
  tool("canteen.take","把成品架上指定料理放到桌面。先返回待交付；实际物件发布后会收到canteen.taken。之后仍需通过habitat.act走近食用，不等于已经吃过。",object({{"batch_id",id}},PluginJson::array({"batch_id"}))),
  tool("canteen.discard","丢弃成品架上的指定料理，保留真实记录；不能改成新鲜料理或退回食材。",object({{"batch_id",id}},PluginJson::array({"batch_id"})))
 });}
 PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancel)override{if(cancel&&cancel())return toolFailure("cancelled");std::lock_guard lock(mutex_);if(restoreFailed_)return toolFailure("canteen_saved_state_invalid");if(name=="canteen.menu")return toolSuccess(kitchen_.menu({args.value("query",std::string{}),args.value("cuisine",std::string{}),args.value("kind",std::string{}),args.value("offset",0u),args.value("limit",8u)}));if(name=="canteen.recipe")return kitchen_.recipeDetails(args.at("recipe_id"));if(name=="canteen.plan")return kitchen_.plan(args.at("recipe_id"));if(name=="canteen.supplies")return toolSuccess(kitchen_.supplyCatalog(args.value("query",std::string{}),args.value("offset",0u),args.value("limit",8u)));if(name=="canteen.observe")return toolSuccess(kitchen_.observe());if(name=="canteen.inventory")return toolSuccess(kitchen_.inventory());if(paused_||offline_)return toolFailure("runtime_paused");PluginJson result;
  if(name=="canteen.request_supplies"){if(!allowOrders_)return toolFailure("canteen_supply_orders_disabled");result=kitchen_.requestSupplies(args.at("items"));}
  else if(name=="canteen.store")result=kitchen_.storeSupplies();
  else if(name=="canteen.move_supply")result=kitchen_.moveSupply(args.at("item_id"),args.at("destination"));
  else if(name=="canteen.eat_supply")result=kitchen_.eatSupply(args.at("item_id"));
  else if(name=="canteen.cook")result=kitchen_.cook(args.at("recipe_id"),args.value("ingredient_ids",PluginJson(nullptr)));
  else if(name=="canteen.cancel")result=kitchen_.cancel(args.at("job_id"));
  else if(name=="canteen.take"){if(!host_.desktopEnabled)return toolFailure("desktop_required");result=kitchen_.take(args.at("batch_id"));}
  else if(name=="canteen.discard")result=kitchen_.discard(args.at("batch_id"));else return toolFailure("tool_not_available");drain();return result;
 }
 void tick(double elapsed)override{std::lock_guard lock(mutex_);if(paused_||offline_||restoreFailed_)return;const double dt=std::isfinite(elapsed)?std::clamp(elapsed,0.0,.25):0;kitchen_.tick(dt);deliveryRetry_+=dt;if(deliveryRetry_>=2){deliveryRetry_=0;kitchen_.retryDelivery();}drain();}
 void event(const PluginJson& event)override{std::lock_guard lock(mutex_);const auto kind=event.value("kind",std::string{});if(kind=="runtime.paused"){paused_=event.value("paused",false);offline_=event.value("offline",false);return;}if(kind=="runtime.canteen.delivery_result"){kitchen_.delivered(event);drain();}}
 PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto v=restoreFailed_?rejectedSnapshot_:kitchen_.snapshot();v["current"]=restoreFailed_?PluginJson{{"issue","canteen_saved_state_invalid"},{"can_cook",PluginJson::array()},{"ready_count",0}}:kitchen_.observe();v["desktop"]=host_.desktopEnabled;return v;}
 void shutdown()override{}
 PluginJson receiveCollection(const PluginJson& item,double remaining,ayt::inventory::IInteractionHost& source)override{std::lock_guard lock(mutex_);if(restoreFailed_||paused_||offline_)return toolFailure("resource_inventory_unavailable");auto result=kitchen_.collect(item,remaining,source);drain();return result;}
 bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);const bool ok=kitchen_.restore(value);restoreFailed_=!ok;if(ok)deliveryRetry_=2;else rejectedSnapshot_=value;return ok;}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Canteen;}
