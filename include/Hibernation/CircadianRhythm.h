#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace hibernation {
// A role's virtual routine. Wall time selects a window; only acknowledged,
// unpaused process time counts as rest. It never blocks the user's computer.
class CircadianRhythm {
public:
 using Json=nlohmann::json;
 struct Preferences {
  bool configured=false,enabled=false;int startMinute=1380,endMinute=420;
  double dragWakeProbability=.2,messageWakeProbability=.65,inertiaSeconds=600;
  std::int64_t revision=0;
  static Preferences read(const Json& v){
   if(!v.is_object())throw std::runtime_error("circadian_preferences");
   for(const auto& [key,value]:v.items())if(key!="enabled"&&key!="start_minute"&&key!="end_minute"&&key!="drag_wake_probability"&&key!="message_wake_probability"&&key!="inertia_seconds")throw std::runtime_error("circadian_preferences");
   Preferences p;p.configured=true;
   if(!v.at("enabled").is_boolean()||!v.at("start_minute").is_number_integer()||!v.at("end_minute").is_number_integer())throw std::runtime_error("circadian_preferences");
   const auto start=v.at("start_minute").get<std::int64_t>(),end=v.at("end_minute").get<std::int64_t>();if(start<0||start>1439||end<0||end>1439)throw std::runtime_error("circadian_preferences");p.enabled=v.at("enabled");p.startMinute=static_cast<int>(start);p.endMinute=static_cast<int>(end);
   const auto duration=(p.endMinute-p.startMinute+1440)%1440;
   if(p.startMinute<0||p.startMinute>1439||p.endMinute<0||p.endMinute>1439||duration<60||duration>720)throw std::runtime_error("circadian_window_60_to_720_minutes");
   auto number=[&](const char* key,double fallback,double low,double high){const auto n=v.value(key,fallback);if(!std::isfinite(n)||n<low||n>high)throw std::runtime_error("circadian_preferences");return n;};
   p.dragWakeProbability=number("drag_wake_probability",.2,0,1);p.messageWakeProbability=number("message_wake_probability",.65,0,1);p.inertiaSeconds=number("inertia_seconds",600,30,3600);return p;
  }
  Json json()const{return {{"enabled",enabled},{"start_minute",startMinute},{"end_minute",endMinute},{"drag_wake_probability",dragWakeProbability},{"message_wake_probability",messageWakeProbability},{"inertia_seconds",inertiaSeconds}};}
 };
 const std::string& phase()const{return phase_;}const std::string& token()const{return token_;}
 bool sleeping()const{return phase_=="sleeping";}bool pending()const{return phase_!="idle";}
 double inertia()const{return inertia_;}
 bool configure(const Json& value,std::int64_t expected){
  auto candidate=Preferences::read(value);if(expected!=preferences_.revision||preferences_.revision>=1000000)return false;
  if(preferences_.configured&&candidate.json()==preferences_.json())return true;
  const bool newWindow=candidate.startMinute!=preferences_.startMinute||candidate.endMinute!=preferences_.endMinute||(candidate.enabled&&!preferences_.enabled);
  candidate.revision=preferences_.revision+1;preferences_=candidate;
  if(newWindow)lastWindow_.clear();
  if(!candidate.enabled)finish("disabled",false);else if(pending())finish("schedule_changed",false);
  nextAttempt_=clock_+5;return true;
 }
 // windowDay is the local date on which this window STARTED, including yesterday
 // for an overnight window. This prevents immediate repeated sleep after waking.
 static std::string windowDay(const Preferences& p,int minute,const std::string& today,const std::string& yesterday){return p.startMinute>p.endMinute&&minute<p.endMinute?yesterday:today;}
 int remainingMinutes(int minute)const{
  const auto& p=preferences_;if(!p.configured||!p.enabled||minute<0||minute>1439)return 0;
  const bool inside=p.startMinute<p.endMinute?(minute>=p.startMinute&&minute<p.endMinute):(minute>=p.startMinute||minute<p.endMinute);
  return inside?(p.endMinute-minute+1440)%1440:0;
 }
 bool mayStart(int minute,const std::string& today,const std::string& yesterday)const{return !pending()&&clock_>=nextAttempt_&&remainingMinutes(minute)>0&&windowDay(preferences_,minute,today,yesterday)!=lastWindow_;}
 // Reconcile local calendar after suspend / clock changes without counting the
 // absent wall time as active sleep. Crossing midnight within one window is OK.
 void reconcileWindow(int minute,const std::string& today,const std::string& yesterday){
  if(!pending()||minute<0||minute>1439||today.empty()||yesterday.empty())return;
  if(remainingMinutes(minute)==0)finish("window_ended",false);
  else if(windowDay(preferences_,minute,today,yesterday)!=window_)finish("clock_window_changed",false);
 }
 bool request(const std::string& token,int minute,const std::string& today,const std::string& yesterday){
  if(token.empty()||token.size()>96||!mayStart(minute,today,yesterday))return false;
  token_=token;phase_="queued";duration_=remainingMinutes(minute)*60.;elapsed_=0;requested_=clock_;window_=windowDay(preferences_,minute,today,yesterday);emit("rhythm.sleep_queued","window_selected");return true;
 }
 void tick(double dt,bool advance,bool available,bool busy,const std::string& actorState){
  if(!advance||!std::isfinite(dt)||dt<=0)return;dt=std::min(dt,.25);clock_+=dt;
  // An exponential decay is continuous even across saved/reloaded state.
  if(sleeping()){const double target=std::min(.8,.3+elapsed_/3600.);inertia_+=std::clamp(target-inertia_,-.03*dt,.03*dt);}
  else {inertia_*=std::exp(-dt/preferences_.inertiaSeconds);if(inertia_<.0001)inertia_=0;}
  if(!pending())return;
  if(!available){finish("character_unavailable",false);return;}
  if(phase_=="sleeping"){elapsed_=std::min(duration_,elapsed_+dt);totalRest_+=dt;if(elapsed_>=duration_)finish("duration_reached",false);return;}
  if(clock_-requested_>120){finish("intent_expired",false);return;}
  if(phase_=="requesting"){if(clock_-poseRequested_>2)finish("pose_unavailable",false);return;}
  if(!busy&&(actorState=="idle"||actorState=="curious")){
   phase_="requesting";poseRequested_=clock_;events_.push_back({{"kind","rhythm.pose.request"},{"token",token_},{"seconds",duration_},{"circadian",true}});
  }
 }
 bool acknowledge(const std::string& token){if(phase_!="requesting"||token!=token_)return false;phase_="sleeping";lastWindow_=window_;emit("rhythm.sleep_started","pose_confirmed");return true;}
 // draw is supplied by the host, keeping policy reproducible in tests.
 bool wake(const std::string& reason,double draw,bool force=false){
  if(!pending())return true;
  if(force){finish(reason,sleeping());return true;}
  if(!std::isfinite(draw)||draw<0||draw>=1)return false;
  if(clock_<nextWakeAttempt_)return false;nextWakeAttempt_=clock_+10;
  const double probability=reason=="pointer_interaction"?preferences_.dragWakeProbability:preferences_.messageWakeProbability;
  if(draw>=probability){emit("rhythm.wake_declined",reason);return false;}
  const bool wasSleeping=sleeping();finish(reason,wasSleeping);if(wasSleeping){lastWindow_.clear();nextAttempt_=clock_+600;}return true;
 }
 void unavailable(const std::string& reason){finish(reason,false);}
 Json observe()const{return {{"configured",preferences_.configured},{"preferences",preferences_.json()},{"preferences_revision",preferences_.revision},{"phase",phase_},{"remaining_seconds",pending()?duration_-elapsed_:0},{"elapsed_seconds",elapsed_},{"sleep_inertia",inertia_},{"active_rest_seconds",totalRest_},{"guide","这是虚拟作息及程序连续状态。睡眠仅在姿态确认后开始。拖动或普通消息可能未唤醒，不把未回应解释成用户的错；醒后允许短暂迷糊、安静或轻微不快，但不强迫抱怨。可按自己的经历调整时段；管理员能随时停止。"}};}
 Json snapshot()const{auto v=observe();v["version"]=1;v["clock"]=clock_;v["token"]=token_;v["duration"]=duration_;v["requested"]=requested_;v["next_attempt"]=nextAttempt_;v["last_window"]=lastWindow_;v["window"]=window_;return v;}
 bool restore(const Json& value){
  try{
   if(!value.is_object()||value.at("version")!=1)return false;
   auto p=Preferences::read(value.at("preferences"));p.configured=value.at("configured");p.revision=value.at("preferences_revision");if(p.revision<0||p.revision>1000000)return false;
   auto finite=[&](const char* key,double high){const double n=value.at(key);if(!std::isfinite(n)||n<0||n>high)throw std::runtime_error("circadian_state");return n;};
   const auto clock=finite("clock",1e10),duration=finite("duration",43200),elapsed=finite("elapsed_seconds",43200),inertia=finite("sleep_inertia",1),total=finite("active_rest_seconds",1e10),requested=finite("requested",clock),next=finite("next_attempt",1e10);
   const auto phase=value.at("phase").get<std::string>(),token=value.at("token").get<std::string>(),window=value.at("window").get<std::string>(),last=value.at("last_window").get<std::string>();
   if((phase!="idle"&&phase!="queued"&&phase!="requesting"&&phase!="sleeping")||token.size()>96||window.size()>32||last.size()>32||elapsed>duration||(phase!="idle"&&(token.empty()||duration<1)))return false;
   preferences_=p;clock_=clock;duration_=duration;elapsed_=elapsed;inertia_=inertia;totalRest_=total;requested_=requested;nextAttempt_=std::max(clock+5,next);phase_=phase;token_=token;window_=window;lastWindow_=last;nextWakeAttempt_=clock;events_.clear();
   // Offline time does not advance rest or inertia and old pose leases never
   // survive process restart. Keep actual elapsed data in the interruption.
   if(pending()){const bool wasSleeping=sleeping();finish("application_restarted",false);if(wasSleeping)lastWindow_.clear();nextAttempt_=clock_+5;}return true;
  }catch(...){return false;}
 }
 std::vector<Json> takeEvents(){auto out=std::move(events_);events_.clear();return out;}
private:
 void emit(const char* kind,const std::string& reason){events_.push_back({{"kind",kind},{"token",token_},{"reason",reason},{"circadian",true},{"elapsed_seconds",elapsed_},{"requested_seconds",duration_},{"sleep_inertia",inertia_},{"preferences_revision",preferences_.revision}});}
 void finish(const std::string& reason,bool disturbed){
  if(!pending())return;const bool actual=sleeping();
  // Readiness changes while resting and after waking, never by an instantaneous
  // mood assignment at the wake event. The actual interruption reason stays in
  // the receipt; it does not force a negative interpretation or canned speech.
  (void)disturbed;
  events_.push_back({{"kind","rhythm.pose.release"},{"token",token_},{"circadian",true}});emit(actual?"rhythm.sleep_ended":"rhythm.sleep_cancelled",reason);
  phase_="idle";token_.clear();duration_=elapsed_=0;nextAttempt_=clock_+300;
 }
 Preferences preferences_;std::string phase_="idle",token_,window_,lastWindow_;
 double clock_=0,duration_=0,elapsed_=0,requested_=0,poseRequested_=0,nextAttempt_=5,nextWakeAttempt_=0,inertia_=0,totalRest_=0;
 std::vector<Json> events_;
};
}
