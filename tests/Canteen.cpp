#include <Hibernation/CanteenKitchen.h>
#include <iostream>
using namespace hibernation;
using namespace hibernation::canteen;
int checks=0;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);++checks;}
std::uint64_t available(const Kitchen& k,const std::string& id){const auto view=k.observe();for(const auto& v:view.at("pantry"))if(v.at("id")==id)return v.at("available");throw 0;}
void step(Kitchen& k,double seconds){while(seconds>0){const auto dt=std::min(1.0,seconds);k.tick(dt);seconds-=dt;}}
int main(){try{
 using bsimmer::cuisine::Slot;using bsimmer::cuisine::Ingredient;
 auto m=bsimmer::cuisine::match(std::vector<Slot>{{"","milk"},{"milk",""}},std::vector<Ingredient>{{"milk","milk"},{"oat_milk","milk"}});
 check(m&&m->at(0)==1&&m->at(1)==0,"overlapping category uses non-greedy exact assignment");
 check(!bsimmer::cuisine::match(std::vector<Slot>{{"milk",""},{"milk",""}},std::vector<Ingredient>{{"milk","milk"}}),"one ingredient cannot fill two units");
 const auto ordered=bsimmer::cuisine::match(std::vector<Slot>{{"","milk"},{"","milk"}},std::vector<Ingredient>{{"z","milk"},{"a","milk"}});check(ordered&&ordered->at(0)==1,"ambiguous slots choose canonical definition order independent of caller order");
 Kitchen k;k.configure(false,900,3600,1800);check(k.observe().at("menu").size()==3,"three real recipes");const auto flour=available(k,"flour"),water=available(k,"water");
 check(!k.cook("bread",Json::array({"flour","sugar","water"})).at("ok").get<bool>(),"invalid substitution rejected");check(available(k,"flour")==flour,"invalid recipe does not consume inventory");
 auto start=k.cook("bread",Json::array({"butter","water","flour"}));check(start.at("ok"),"ingredients match independent of order");check(start.at("result").at("completed")==false,"acceptance is not a ready meal");
 check(available(k,"flour")==flour-1&&available(k,"water")==water-1,"pending ingredients reserved from available stock");check(!k.cook("tea").at("ok").get<bool>(),"single cooking position mutex");
 const std::string firstJob=start.at("result").at("job").at("id");check(k.cancel(firstJob).at("ok"),"cancel existing pending work");check(available(k,"flour")==flour&&available(k,"water")==water,"cancel releases exact inputs");
 start=k.cook("tea");step(k,5);const auto frozen=Json::parse(k.snapshot().dump());Kitchen resumed;resumed.configure(false,900,3600,1800);check(resumed.restore(frozen),"restore exact pending engine reservation");step(resumed,6);check(resumed.observe().at("ready_count")==0,"restart never finishes the job early");step(resumed,1);check(resumed.observe().at("ready_count")==1,"resumed cooking finishes exactly once");check(available(resumed,"water")==water-1,"completion does not reserve or consume twice");
 auto view=resumed.observe();std::string meal=view.at("shelf")[0].at("id");auto take=resumed.take(meal);check(take.at("ok")&&take.at("result").at("completed")==false,"take waits for real desktop publication");check(resumed.observe().at("ready_count")==1,"pending handoff retains reserved shelf item");
 auto pending=resumed.snapshot();Kitchen recovery;recovery.configure(false,900,3600,1800);check(recovery.restore(Json::parse(pending.dump())),"recover pending durable handoff");recovery.retryDelivery();const auto events=recovery.takeEvents();check(events.size()==1&&events[0].at("ticket")==take.at("result").at("ticket"),"recovery repeats the same delivery identity");
 recovery.delivered({{"ticket",take.at("result").at("ticket")},{"ok",false},{"error","world_object_limit"}});check(recovery.observe().at("ready_count")==1&&!recovery.observe().at("delivery_pending").get<bool>(),"full desktop returns meal to shelf without duplication");
 take=recovery.take(meal);const auto receipt=Json{{"ticket",take.at("result").at("ticket")},{"ok",true},{"object_id","actual-food-1"}};recovery.delivered(receipt);recovery.delivered(receipt);check(recovery.observe().at("ready_count")==0&&recovery.observe().at("recent").size()==1,"duplicate acknowledgement cannot consume twice");check(recovery.observe().at("recent")[0].at("object_id")=="actual-food-1","actual object provenance retained");check(!recovery.take(meal).at("ok").get<bool>(),"taken food cannot be issued again");
 Kitchen full;full.configure(false,900,3600,1800);for(int n=0;n<3;++n){check(full.cook("tea").at("ok"),"next cooking slot available");step(full,12);}check(!full.cook("bread").at("ok").get<bool>(),"bounded three-place shelf");const auto breadBefore=available(full,"flour");check(breadBefore==4,"full shelf does not reserve new materials");
 check(full.discard(full.observe().at("shelf")[0].at("id")).at("ok"),"discard frees a plate");check(full.cook("pudding").at("ok"),"freed plate permits another recipe");step(full,35);auto warm=full.observe().at("shelf").back();step(full,30);check(full.observe().at("shelf").back().at("temperature")<warm.at("temperature"),"served temperature cools continuously");
 Kitchen expiry;expiry.configure(false,900,3600,60);expiry.cook("tea");step(expiry,12);const auto expiryId=expiry.observe().at("shelf")[0].at("id");step(expiry,60);check(expiry.observe().at("ready_count")==0&&!expiry.take(expiryId).at("ok").get<bool>(),"expired shelf food cannot be refreshed by taking");
 Kitchen automatic;automatic.configure(true,60,3600,1800);step(automatic,60);check(automatic.observe().at("job").at("origin")=="canteen","automatic supply is attributed to kitchen, not character");step(automatic,45);check(automatic.observe().at("ready_count")==1,"automatic production without any model call");step(automatic,180);check(automatic.observe().at("ready_count")==1&&automatic.observe().at("job").is_null(),"automatic cooking stops while food remains");
 const auto before=recovery.snapshot();auto corrupt=before;corrupt["stock"]["schemaVersion"]=99999;check(!recovery.restore(corrupt)&&recovery.snapshot()==before,"invalid restore leaves existing state unchanged");
 std::cout<<"PASS "<<checks<<" cuisine, inventory, clock, recovery and handoff checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
