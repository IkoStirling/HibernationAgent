#include <Hibernation/RoleState.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace hibernation {
namespace {
const std::set<std::string> activities={"idle","engaging","watching","reflecting","resting","withdrawing"};
double bounded(const nlohmann::json& j,const char* key,double fallback){const auto x=j.value(key,fallback);if(!std::isfinite(x)||x<0||x>1)throw std::runtime_error("role_state_value");return x;}
double approach(double current,double target,double limit){return current+std::clamp(target-current,-limit,limit);}
}
void RoleState::restore(const nlohmann::json& j,std::int64_t now){std::lock_guard lock(mutex_);if(!j.is_object())throw std::runtime_error("role_state_shape");
    if(j.value("version",1)!=1)throw std::runtime_error("role_state_version");
    energy_=bounded(j,"energy",0.72);openness_=bounded(j,"openness",0.55);mood_=bounded(j,"mood",0.5);attention_=bounded(j,"attention",0.2);
    targetMood_=bounded(j,"target_mood",mood_);targetOpenness_=bounded(j,"target_openness",openness_);targetAttention_=bounded(j,"target_attention",attention_);
    activity_=j.value("activity",std::string("idle"));desired_=j.value("desired_activity",activity_);if(!activities.contains(activity_)||!activities.contains(desired_))throw std::runtime_error("role_state_activity");
    evidence_=j.value("evidence",std::string{});weatherCode_=j.value("weather_code",-1);weatherMs_=j.value("weather_observed_ms",std::int64_t(0));
    weatherComfort_=bounded(j,"weather_comfort",1.0);
    // Preserve exact values on restart. Offline time does not jump the persona
    // or replay hours of simulated frames. Fresh elapsed time starts here.
    lastMs_=now;enteredMs_=now;stimulusMs_=now;
}
nlohmann::json RoleState::snapshot()const{std::lock_guard lock(mutex_);return {{"version",1},{"energy",energy_},{"openness",openness_},{"mood",mood_},{"attention",attention_},{"target_mood",targetMood_},{"target_openness",targetOpenness_},{"target_attention",targetAttention_},{"activity",activity_},{"desired_activity",desired_},{"evidence",evidence_},{"entered_ms",enteredMs_},{"updated_ms",lastMs_},{"weather_code",weatherCode_},{"weather_observed_ms",weatherMs_},{"weather_comfort",weatherComfort_}};}
void RoleState::tick(std::int64_t now,bool advance){std::lock_guard lock(mutex_);if(!lastMs_){lastMs_=enteredMs_=now;return;}if(!advance){const auto elapsed=std::max<std::int64_t>(0,now-lastMs_);enteredMs_+=elapsed;stimulusMs_+=elapsed;lastMs_=std::max(lastMs_,now);return;}const double dt=std::clamp((now-lastMs_)/1000.0,0.0,0.25);if(now>lastMs_)lastMs_=now;
    mood_=approach(mood_,targetMood_,dt*0.003);openness_=approach(openness_,std::clamp(targetOpenness_-(1.0-weatherComfort_)*0.025,0.0,1.0),dt*0.003);attention_=approach(attention_,targetAttention_,dt*0.025);
    const auto rate=activity_=="resting"?0.0008:activity_=="watching"? -0.00012:activity_=="engaging"?-0.0002:0.00008;energy_=std::clamp(energy_+dt*(rate-(1.0-weatherComfort_)*0.00006),0.0,1.0);
    if(activity_!=desired_&&now-enteredMs_>=3000){activity_=desired_;enteredMs_=now;}
    if(now-stimulusMs_>120000&&desired_=="engaging"){desired_="idle";targetAttention_=0.2;}
}
bool RoleState::propose(std::string activity,double intensity,const std::string& evidence,std::int64_t now,std::string& issue){std::lock_guard lock(mutex_);
    if(!activities.contains(activity)||!std::isfinite(intensity)||intensity< -1||intensity>1||evidence.empty()||evidence.size()>256){issue="invalid_state_stimulus";return false;}
    // Repeated tool calls cannot accumulate arbitrary mood changes: proposals
    // establish one bounded target relative to neutral, not additive setters.
    desired_=std::move(activity);targetOpenness_=std::clamp(0.55+intensity*0.15,0.25,0.8);targetAttention_=desired_=="resting"?0.08:desired_=="idle"?0.2:0.75;
    evidence_=evidence;stimulusMs_=now;issue.clear();return true;
}
void RoleState::weather(double temperature,int code,std::int64_t observedAt){std::lock_guard lock(mutex_);if(!std::isfinite(temperature)||temperature< -90||temperature>65||code<0||code>99||observedAt<=weatherMs_)return;
    const double comfort=std::clamp(1.0-std::abs(temperature-20.0)/40.0,0.0,1.0);const double sky=code<=3?0.03:code>=51?-0.04:0.0;
    targetMood_=std::clamp(0.46+comfort*0.08+sky,0.35,0.65);weatherComfort_=comfort;weatherCode_=code;weatherMs_=observedAt;
}
}
