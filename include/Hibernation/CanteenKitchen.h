#pragma once
#include <Hibernation/RolePlugin.h>
#include <AYInventory/Inventory.h>
#include <AYInventory/SnapshotJson.h>
#include <cuisine/RecipeRules.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
namespace hibernation::canteen {
using Json=PluginJson;
struct Tag {std::string name;double intensity=0;};
struct Ingredient {std::string id,name,category;std::vector<Tag> tags;};
struct Recipe {std::string id,name,kind,base;std::vector<bsimmer::cuisine::Slot> slots;double seconds;std::string texture;};
inline const std::vector<Ingredient>& ingredients(){static const std::vector<Ingredient> v={
 {"flour","面粉","grain",{{"grain",.8}}},{"water","清水","water",{}},{"butter","黄油","fat",{{"buttery",.7}}},
 {"milk","牛奶","milk",{{"creamy",.8}}},{"sugar","糖","sweetener",{{"sweet",.65}}},{"tea_leaf","茶叶","tea",{{"tea",.8},{"bitter",.25}}}};return v;}
inline const std::vector<Recipe>& recipes(){static const std::vector<Recipe> v={
 {"bread","黄油小面包","bread","flour",{{"water",""},{"butter",""}},45,"松软的面包心，略酥的表面"},
 {"pudding","牛奶布丁","pudding","milk",{{"sugar",""}},35,"柔软、细滑"},
 {"tea","清茶","tea","water",{{"tea_leaf",""}},12,"清透的茶水"}};return v;}
inline const Recipe* recipe(const std::string& id){for(const auto& r:recipes())if(r.id==id)return &r;return nullptr;}
inline const Ingredient* ingredient(const std::string& id){for(const auto& i:ingredients())if(i.id==id)return &i;return nullptr;}
inline Json recipeView(const Recipe& r){Json inputs=Json::array();inputs.push_back(r.base);for(const auto& s:r.slots)inputs.push_back(s.specific);return {{"id",r.id},{"name",r.name},{"template",r.kind},{"ingredients",inputs},{"seconds",r.seconds},{"texture",r.texture}};}
inline Json menu(){Json out=Json::array();for(const auto& r:recipes())out.push_back(recipeView(r));return out;}
inline Json sensory(const Recipe& r){std::vector<Tag> tags;auto add=[&](const std::string& id){const auto* i=ingredient(id);bsimmer::cuisine::mergeTagsMax(tags,std::span<const Tag>(i->tags));};add(r.base);for(const auto& s:r.slots)add(s.specific);Json v=Json::array();for(const auto& t:tags)v.push_back({{"name",t.name},{"strength",t.intensity}});return {{"tags",v},{"texture",r.texture},{"basis","program_simulated_food_properties"}};}
class Kitchen {
 std::unique_ptr<ayt::inventory::Store> stock_;Json batches_=Json::array(),job_=nullptr,outbox_=nullptr,history_=Json::array();std::vector<Json> events_;
 double clock_=0,nextAutomatic_=60,nextSupply_=3600;std::uint64_t sequence_=0;std::size_t automaticRecipe_=0;std::string identity_;
 double durationScale_=1,shelfLife_=1800,automaticInterval_=900,supplyInterval_=3600;bool automatic_=true;
 std::string next(const char* kind){return identity_+"-"+kind+"-"+std::to_string(++sequence_);}
 void emit(const std::string& kind,Json data){data["kind"]=kind;data["at"]=clock_;events_.push_back(std::move(data));}
 Json* batch(const std::string& id){for(auto& b:batches_)if(b.at("id")==id)return &b;return nullptr;}
 void archive(Json b,const std::string& state){b["status"]=state;b["ended_at"]=clock_;history_.push_back(b);while(history_.size()>24)history_.erase(history_.begin());}
 std::string freeShelf()const{for(int i=0;i<3;++i){const auto slot="plate-"+std::to_string(i);if(!stock_->occupying({ayt::inventory::Place::Container,"shelf",slot}))return slot;}return {};}
 bool materialAvailable(const Recipe& r)const{if(stock_->reservableQuantity(r.base,"pantry")<1)return false;for(const auto& s:r.slots)if(stock_->reservableQuantity(s.specific,"pantry")<1)return false;return true;}
 void finish(){const auto* r=recipe(job_.at("recipe_id"));const auto slot=freeShelf();if(slot.empty()){job_["phase"]="waiting_for_shelf";return;}const std::string id=job_.at("batch_id");
  if(!stock_->commitCraft(job_.at("id").get<std::string>(),id,{ayt::inventory::Place::Container,"shelf",slot}))throw std::runtime_error("canteen_commit_failed");
  Json b={{"id",id},{"recipe_id",r->id},{"name",r->name},{"template",r->kind},{"status","ready"},{"created_at",clock_},{"expires_at",clock_+shelfLife_},{"origin",job_.at("origin")},{"sensory",sensory(*r)}};batches_.push_back(b);emit("canteen.ready",{{"batch",b},{"completed",true}});job_=nullptr;
 }
 void replenish(bool initial){for(const auto& i:ingredients()){const auto cap=i.id=="water"?24u:12u;std::uint64_t available=0;for(const auto& item:stock_->snapshot().items)if(item.location.ownerId=="pantry"&&item.definitionId==i.id)available+=item.quantity;const auto quantity=std::min<std::uint64_t>(initial?(i.id=="water"?8:4):2,cap-std::min<std::uint64_t>(cap,available));if(!quantity)continue;
   // Fresh supplies use a free stable slot; existing holds are never moved.
   std::string slot;for(int n=0;n<128;++n){auto s="supply-"+std::to_string(n);if(!stock_->occupying({ayt::inventory::Place::Container,"pantry",s})){slot=s;break;}}
   if(slot.empty())continue;if(!stock_->createItem({next("supply"),i.id,static_cast<std::uint32_t>(quantity),{ayt::inventory::Place::Container,"pantry",slot}}))throw std::runtime_error("canteen_supply_failed");
  }if(!initial)emit("canteen.restocked",{{"source","program_supply"}});
 }
public:
 Kitchen(){std::random_device random;identity_="k"+std::to_string(random())+std::to_string(random());std::vector<ayt::inventory::Definition> defs;for(const auto& i:ingredients())defs.push_back({i.id,24});for(const auto& r:recipes())defs.push_back({"meal."+r.id,1});auto made=ayt::inventory::Store::create(1,std::move(defs));if(!made)throw std::runtime_error("canteen_catalog");stock_=std::make_unique<ayt::inventory::Store>(std::move(*made));
  ayt::inventory::Container pantry{"pantry",{}},shelf{"shelf",{}};for(int i=0;i<128;++i)pantry.slots.insert("supply-"+std::to_string(i));for(int i=0;i<3;++i)shelf.slots.insert("plate-"+std::to_string(i));if(!stock_->addContainer(pantry)||!stock_->addContainer(shelf))throw std::runtime_error("canteen_containers");replenish(true);
 }
 void configure(bool automatic,double interval,double supply,double lifetime){automatic_=automatic;automaticInterval_=interval;supplyInterval_=supply;shelfLife_=lifetime;nextSupply_=std::min(nextSupply_,clock_+supplyInterval_);nextAutomatic_=std::min(nextAutomatic_,clock_+automaticInterval_);}
 Json observe()const{Json pantry=Json::array(),shelf=Json::array(),can=Json::array();for(const auto& i:ingredients())pantry.push_back({{"id",i.id},{"name",i.name},{"available",stock_->reservableQuantity(i.id,"pantry")}});
  for(const auto& b:batches_){auto v=b;v["remaining_seconds"]=std::max(0.0,b.at("expires_at").get<double>()-clock_);v["temperature"]=std::max(20.0,60.0-(clock_-b.at("created_at").get<double>())/30.0);shelf.push_back(std::move(v));}
  for(const auto& r:recipes())if(!job_.is_object()&&batches_.size()<3&&materialAvailable(r))can.push_back(r.id);
  Json current=job_;if(current.is_object()){current["remaining_seconds"]=std::max(0.0,current.at("complete_at").get<double>()-clock_);current.erase("complete_at");}
  return {{"menu",menu()},{"pantry",pantry},{"shelf",shelf},{"job",current},{"ready_count",shelf.size()},{"shelf_capacity",3},{"can_cook",can},{"delivery_pending",!outbox_.is_null()},{"recent",history_},{"automatic",automatic_},{"guide","食堂按实际库存制作，cook的accepted表示开始；只有canteen.ready才是成品。take只是请求取出，实际桌面物件由habitat发布并确认；之后仍需走近食用。感官属性是程序模拟，小爱可以形成自己的偏好，不要求每次点评或主动汇报。"}};
 }
 Json cook(const std::string& recipeId,const Json& supplied=Json(nullptr),const std::string& origin="character"){
  const auto* r=recipe(recipeId);if(!r)return toolFailure("canteen_recipe_unknown");if(job_.is_object())return toolFailure("canteen_busy");if(batches_.size()>=3)return toolFailure("canteen_shelf_full");
  std::vector<std::string> ids;if(supplied.is_null()){ids.push_back(r->base);for(const auto& s:r->slots)ids.push_back(s.specific);}else{if(!supplied.is_array()||supplied.size()>4)return toolFailure("canteen_ingredients");for(const auto& id:supplied){if(!id.is_string())return toolFailure("canteen_ingredients");ids.push_back(id);}}
  std::vector<bsimmer::cuisine::Slot> slots={{r->base,""}};slots.insert(slots.end(),r->slots.begin(),r->slots.end());std::vector<bsimmer::cuisine::Ingredient> chosen;for(const auto& id:ids){const auto* i=ingredient(id);if(!i)return toolFailure("canteen_ingredient_unknown");chosen.push_back({i->id,i->category});}if(!bsimmer::cuisine::match(slots,chosen))return toolFailure("canteen_ingredients_mismatch");
  ayt::inventory::Recipe craft{r->id,{},"meal."+r->id,1};std::map<std::string,std::uint32_t> counts;for(const auto& id:ids)++counts[id];for(const auto& [id,n]:counts)craft.inputs.push_back({id,n});const auto jobId=next("cook");if(!stock_->reserveCraft(craft,"pantry",jobId))return toolFailure("canteen_ingredients_insufficient");
  job_={{"id",jobId},{"batch_id",next("meal")},{"recipe_id",r->id},{"phase","cooking"},{"origin",origin},{"started_at",clock_},{"complete_at",clock_+r->seconds*durationScale_},{"ingredients",ids}};emit("canteen.started",{{"job",job_},{"completed",false}});return toolSuccess({{"accepted",true},{"completed",false},{"job",job_}});
 }
 Json cancel(const std::string& id){if(!job_.is_object()||job_.at("id")!=id)return toolFailure("canteen_job_missing");if(!stock_->cancelCraft(id))return toolFailure("canteen_cancel_failed");emit("canteen.cancelled",{{"job",job_},{"materials_released",true}});job_=nullptr;return toolSuccess({{"cancelled",true},{"materials_released",true}});}
 Json discard(const std::string& id){auto* b=batch(id);if(!b)return toolFailure("canteen_batch_missing");if(b->at("status")!="ready")return toolFailure("canteen_delivery_pending");if(!stock_->destroyItem(id))return toolFailure("canteen_discard_failed");const auto old=*b;archive(old,"discarded");std::erase_if(batches_.get_ref<Json::array_t&>(),[&](const Json& item){return item.at("id")==id;});emit("canteen.discarded",{{"batch",old}});return toolSuccess({{"discarded",true}});}
 Json take(const std::string& id){if(!outbox_.is_null())return toolFailure("canteen_delivery_pending");auto* b=batch(id);if(!b||b->at("status")!="ready")return toolFailure("canteen_batch_missing");const double remaining=b->at("expires_at").get<double>()-clock_;if(remaining<5)return toolFailure("canteen_batch_expiring");
  const auto ticket=next("take");outbox_={{"ticket",ticket},{"batch",*b},{"remaining_seconds",remaining}};(*b)["status"]="dispatching";emit("canteen.delivery.request",outbox_);return toolSuccess({{"accepted",true},{"completed",false},{"ticket",ticket},{"batch_id",id},{"status","waiting_for_desktop_publication"}});
 }
 void delivered(const Json& receipt){if(outbox_.is_null()||!receipt.is_object()||receipt.value("ticket",std::string{})!=outbox_.at("ticket").get<std::string>())return;const auto id=outbox_.at("batch").at("id").get<std::string>();auto* b=batch(id);if(!b)throw std::runtime_error("canteen_pending_batch_missing");
  if(receipt.value("ok",false)){if(!stock_->destroyItem(id))throw std::runtime_error("canteen_delivery_commit_failed");auto old=*b;old["ticket"]=outbox_.at("ticket");old["object_id"]=receipt.at("object_id");archive(old,"placed_on_desktop");std::erase_if(batches_.get_ref<Json::array_t&>(),[&](const Json& v){return v.at("id")==id;});emit("canteen.taken",{{"batch",old},{"ticket",outbox_.at("ticket")},{"completed",true}});}else{(*b)["status"]="ready";emit("canteen.delivery_failed",{{"batch_id",id},{"ticket",outbox_.at("ticket")},{"error",receipt.value("error",std::string("desktop_unavailable"))}});}outbox_=nullptr;
 }
 void retryDelivery(){if(!outbox_.is_null())emit("canteen.delivery.request",outbox_);}
 void tick(double elapsed){if(!std::isfinite(elapsed)||elapsed<0||elapsed>60)return;clock_+=elapsed;
  for(auto it=batches_.begin();it!=batches_.end();){if(it->at("status")=="ready"&&it->at("expires_at").get<double>()<=clock_){const auto old=*it;if(!stock_->destroyItem(old.at("id").get<std::string>()))throw std::runtime_error("canteen_expiry_failed");archive(old,"expired");emit("canteen.expired",{{"batch",old}});it=batches_.erase(it);}else ++it;}
  if(job_.is_object()&&clock_>=job_.at("complete_at").get<double>())finish();
  if(clock_>=nextSupply_){replenish(false);nextSupply_=clock_+supplyInterval_;}
  if(automatic_&&clock_>=nextAutomatic_){nextAutomatic_=clock_+automaticInterval_;if(batches_.empty()&&job_.is_null())for(std::size_t offset=0;offset<recipes().size();++offset){const auto index=(automaticRecipe_+offset)%recipes().size();const auto& r=recipes()[index];if(materialAvailable(r)){cook(r.id,nullptr,"canteen");automaticRecipe_=(index+1)%recipes().size();break;}}}
 }
 std::vector<Json> takeEvents(){auto out=std::move(events_);events_.clear();return out;}
 Json snapshot()const{const auto encoded=ayt::inventory::encodeSnapshotJson(stock_->snapshotWithReservations());if(!encoded)throw std::runtime_error("canteen_save");return {{"version",1},{"identity",identity_},{"sequence",sequence_},{"automatic_recipe",automaticRecipe_},{"clock",clock_},{"next_automatic",nextAutomatic_},{"next_supply",nextSupply_},{"stock",Json::parse(*encoded)},{"job",job_},{"batches",batches_},{"outbox",outbox_},{"history",history_}};}
 bool restore(const Json& value){try{if(!value.is_object()||value.dump().size()>1048576||value.value("version",0)!=1||!value.at("batches").is_array()||value.at("batches").size()>3||!value.at("history").is_array()||value.at("history").size()>24)return false;const auto validTime=[](const Json& v){return v.is_number()&&std::isfinite(v.get<double>())&&v.get<double>()>=0&&v.get<double>()<1e10;};for(const auto* key:{"clock","next_automatic","next_supply"})if(!validTime(value.at(key)))return false;
   const auto identity=value.at("identity").get<std::string>();if(identity.empty()||identity.size()>64||identity.find_first_not_of("k0123456789")!=std::string::npos||!value.at("sequence").is_number_unsigned())return false;
   auto decoded=ayt::inventory::decodeSnapshotJson(value.at("stock").dump());if(!decoded)return false;auto candidate=*stock_;if(!candidate.restore(*decoded))return false;
   const auto job=value.at("job");if(!job.is_null()){if(!recipe(job.at("recipe_id"))||!validTime(job.at("complete_at")))return false;const auto found=decoded->pendingCrafts.find(job.at("id").get<std::string>());if(found==decoded->pendingCrafts.end()||found->second.recipe.id!=job.at("recipe_id").get<std::string>())return false;}if(decoded->pendingCrafts.size()!=(job.is_null()?0:1)||!decoded->pendingItemHolds.empty())return false;
   std::set<std::string> ids;for(const auto& b:value.at("batches")){const auto* r=recipe(b.at("recipe_id"));if(!r||!ids.insert(b.at("id").get<std::string>()).second||!validTime(b.at("created_at"))||!validTime(b.at("expires_at"))||b.at("expires_at")<=b.at("created_at")||(b.at("status")!="ready"&&b.at("status")!="dispatching"))return false;const auto* item=candidate.findItem(b.at("id").get<std::string>());if(!item||item->definitionId!="meal."+r->id||item->location.ownerId!="shelf"||item->quantity!=1)return false;}
   for(const auto& item:decoded->items){if(item.location.ownerId=="shelf"&&!ids.contains(item.id))return false;if(item.location.ownerId=="pantry"&&!ingredient(item.definitionId))return false;}
   const auto outbox=value.at("outbox");int dispatching=0;for(const auto& b:value.at("batches"))if(b.at("status")=="dispatching"){++dispatching;if(outbox.is_null()||outbox.at("batch").at("id")!=b.at("id"))return false;}if(dispatching!=(outbox.is_null()?0:1))return false;
   if(decoded->containers.size()!=2||!candidate.hasContainer("pantry")||!candidate.hasContainer("shelf"))return false;
   if(!job.is_null()){const auto* r=recipe(job.at("recipe_id"));ayt::inventory::Recipe expected{r->id,{},"meal."+r->id,1};std::map<std::string,std::uint32_t> counts;counts[r->base]++;for(const auto& slot:r->slots)counts[slot.specific]++;for(const auto& [id,n]:counts)expected.inputs.push_back({id,n});const auto& pending=decoded->pendingCrafts.at(job.at("id").get<std::string>());if(pending.recipe!=expected||pending.sourceContainerId!="pantry"||!validTime(job.at("started_at"))||std::abs(job.at("complete_at").get<double>()-job.at("started_at").get<double>()-r->seconds)>1e-6)return false;}
   for(const auto& b:value.at("batches")){const auto* r=recipe(b.at("recipe_id"));if(b.at("template")!=r->kind||b.at("name")!=r->name||b.at("sensory")!=sensory(*r)||(b.at("origin")!="character"&&b.at("origin")!="canteen")||b.at("created_at").get<double>()>value.at("clock").get<double>()||b.at("expires_at").get<double>()-b.at("created_at").get<double>()>43200)return false;}
   if(!outbox.is_null()){if(!outbox.at("ticket").is_string()||outbox.at("ticket").get<std::string>().size()>80||!validTime(outbox.at("remaining_seconds"))||outbox.at("remaining_seconds").get<double>()>43200)return false;}
   stock_->swap(candidate);identity_=identity;sequence_=value.at("sequence");automaticRecipe_=value.value("automatic_recipe",std::size_t(0))%recipes().size();clock_=value.at("clock");nextAutomatic_=value.at("next_automatic");nextSupply_=value.at("next_supply");job_=job;batches_=value.at("batches");outbox_=outbox;history_=value.at("history");events_.clear();return true;
  }catch(...){return false;}}
};
}
