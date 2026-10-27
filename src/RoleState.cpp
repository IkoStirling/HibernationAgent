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
    const auto version=j.value("version",1);if(version!=1&&version!=2&&version!=3)throw std::runtime_error("role_state_version");
    energy_=bounded(j,"energy",0.72);openness_=bounded(j,"openness",0.55);mood_=bounded(j,"mood",0.5);attention_=bounded(j,"attention",0.2);
    targetMood_=bounded(j,"target_mood",mood_);targetOpenness_=bounded(j,"target_openness",openness_);targetAttention_=bounded(j,"target_attention",attention_);
    activity_=j.value("activity",std::string("idle"));desired_=j.value("desired_activity",activity_);if(!activities.contains(activity_)||!activities.contains(desired_))throw std::runtime_error("role_state_activity");
    evidence_=j.value("evidence",std::string{});weatherCode_=j.value("weather_code",-1);weatherMs_=j.value("weather_observed_ms",std::int64_t(0));
    weatherComfort_=bounded(j,"weather_comfort",1.0);
    willingness_=bounded(j,"willingness",0.55);socialLoad_=bounded(j,"social_load",0);targetSocialLoad_=bounded(j,"target_social_load",socialLoad_);weatherMood_=bounded(j,"weather_mood",targetMood_);
    targetValence_=j.value("target_valence",0.0);if(!std::isfinite(targetValence_)||std::abs(targetValence_)>1)throw std::runtime_error("role_state_value");
    appraisalValence_=j.value("appraisal_valence",targetValence_);appraisalEvidence_=j.value("appraisal_source_id",std::string{});
    if(!std::isfinite(appraisalValence_)||std::abs(appraisalValence_)>1||appraisalEvidence_.size()>256)throw std::runtime_error("role_state_appraisal");
    mode_=j.value("conversation_mode",std::string("balanced"));if(mode_!="balanced"&&mode_!="brief"&&mode_!="quiet")throw std::runtime_error("role_state_mode");
    focus_=j.value("focus",std::string{});if(focus_.size()>1024)throw std::runtime_error("role_state_focus");
    // Preserve exact values on restart. Offline time does not jump the persona
    // or replay hours of simulated frames. Fresh elapsed time starts here.
    const auto saved=j.value("updated_ms",now);lastMs_=now;enteredMs_=now-std::clamp<std::int64_t>(saved-j.value("entered_ms",saved),0,604800000);stimulusMs_=now-std::clamp<std::int64_t>(j.value("stimulus_age_ms",std::int64_t(0)),0,604800000);
    appraisalMs_=now-std::clamp<std::int64_t>(j.value("appraisal_age_ms",j.value("stimulus_age_ms",std::int64_t(0))),0,604800000);
}
nlohmann::json RoleState::snapshot()const{std::lock_guard lock(mutex_);return {{"version",3},{"energy",energy_},{"openness",openness_},{"mood",mood_},{"attention",attention_},{"willingness",willingness_},{"social_load",socialLoad_},{"target_social_load",targetSocialLoad_},{"target_valence",targetValence_},{"appraisal_source_id",appraisalEvidence_},{"appraisal_valence",appraisalValence_},{"appraisal_age_ms",std::max<std::int64_t>(0,lastMs_-appraisalMs_)},{"weather_mood",weatherMood_},{"focus",focus_},{"conversation_mode",mode_},{"behavior",{{"preferred_response",mode_=="quiet"?"silence_or_brief":mode_=="brief"?"brief":"natural"},{"autonomy","may_decline_or_remain_silent"},{"instruction","将状态体现在语气、篇幅和注意对象上；不报数值、不朗读状态，不因建议而强制回应或拒绝。"}}},{"target_mood",targetMood_},{"target_openness",targetOpenness_},{"target_attention",targetAttention_},{"activity",activity_},{"desired_activity",desired_},{"evidence",evidence_},{"entered_ms",enteredMs_},{"updated_ms",lastMs_},{"stimulus_age_ms",std::max<std::int64_t>(0,lastMs_-stimulusMs_)},{"weather_code",weatherCode_},{"weather_observed_ms",weatherMs_},{"weather_comfort",weatherComfort_}};}
void RoleState::tick(std::int64_t now,bool advance){std::lock_guard lock(mutex_);if(!lastMs_){lastMs_=enteredMs_=now;return;}if(!advance){const auto elapsed=std::max<std::int64_t>(0,now-lastMs_);enteredMs_+=elapsed;stimulusMs_+=elapsed;appraisalMs_+=elapsed;lastMs_=std::max(lastMs_,now);return;}const double dt=std::clamp((now-lastMs_)/1000.0,0.0,0.25);if(now>lastMs_)lastMs_=now;
    if(weatherMs_&&now-weatherMs_>3600000){weatherMood_=0.5;weatherComfort_=1;weatherCode_=-1;}
    if(now-appraisalMs_>600000)targetValence_=0;
    targetMood_=std::clamp(weatherMood_+targetValence_*0.12,0.2,0.8);
    targetSocialLoad_=approach(targetSocialLoad_,0,dt*(activity_=="resting"?0.002:0.0005));socialLoad_=approach(socialLoad_,targetSocialLoad_,dt*0.005);
    const auto targetWillingness=std::clamp(0.55+(energy_-0.72)*0.5+(openness_-0.55)*0.4-socialLoad_*0.4+(mood_-0.5)*0.1-(activity_=="resting"?0.2:activity_=="withdrawing"?0.25:0),0.05,0.9);
    willingness_=approach(willingness_,targetWillingness,dt*0.003);
    if(mode_=="balanced"&&willingness_<0.35)mode_="brief";else if(mode_=="brief"&&willingness_>0.43)mode_="balanced";else if(mode_=="brief"&&willingness_<0.2)mode_="quiet";else if(mode_=="quiet"&&willingness_>0.28)mode_="brief";
    mood_=approach(mood_,targetMood_,dt*0.003);openness_=approach(openness_,std::clamp(targetOpenness_-(1.0-weatherComfort_)*0.025,0.0,1.0),dt*0.003);attention_=approach(attention_,targetAttention_,dt*0.025);
    const auto rate=activity_=="resting"?0.0008:activity_=="watching"? -0.00012:activity_=="engaging"?-0.0002:0.00008;energy_=std::clamp(energy_+dt*(rate-(1.0-weatherComfort_)*0.00006),0.0,1.0);
    if(activity_!=desired_&&now-enteredMs_>=3000){activity_=desired_;enteredMs_=now;}
    if(now-stimulusMs_>120000&&desired_=="engaging"){desired_="idle";targetAttention_=0.2;}
}
bool RoleState::propose(std::string activity,std::optional<double> intensity,const std::string& evidence,std::int64_t now,std::string& issue,std::optional<double> valence,std::string focus){std::lock_guard lock(mutex_);
    if(!activities.contains(activity)||(intensity&&(!std::isfinite(*intensity)||*intensity< -1||*intensity>1))||(valence&&(!std::isfinite(*valence)||std::abs(*valence)>1))||evidence.empty()||evidence.size()>256||focus.size()>1024){issue="invalid_state_stimulus";return false;}
    // Repeated tool calls cannot accumulate arbitrary mood changes: proposals
    // establish one bounded target relative to neutral, not additive setters.
    desired_=std::move(activity);if(intensity)targetOpenness_=std::clamp(0.55+*intensity*0.15,0.25,0.8);targetAttention_=desired_=="resting"?0.08:desired_=="idle"?0.2:0.75;
    evidence_=evidence;stimulusMs_=now;
    if(valence&&(appraisalEvidence_!=evidence||appraisalValence_!=*valence)){appraisalEvidence_=evidence;appraisalValence_=targetValence_=*valence;appraisalMs_=now;}
    if(!focus.empty())focus_=std::move(focus);issue.clear();return true;
}
bool RoleState::appraise(double valence,const std::string& evidence,std::int64_t now,std::string& issue){std::lock_guard lock(mutex_);
    if(!std::isfinite(valence)||std::abs(valence)>1||evidence.empty()||evidence.size()>256||now<0){issue="invalid_state_appraisal";return false;}
    if(appraisalEvidence_!=evidence||appraisalValence_!=valence){appraisalEvidence_=evidence;appraisalValence_=targetValence_=valence;appraisalMs_=now;}
    issue.clear();return true;
}
void RoleState::contact(std::int64_t now){std::lock_guard lock(mutex_);targetSocialLoad_=std::min(1.0,targetSocialLoad_+0.015);if(desired_=="idle"||desired_=="engaging"){desired_="engaging";targetAttention_=0.75;stimulusMs_=now;}}
void RoleState::weather(double temperature,int code,std::int64_t observedAt){std::lock_guard lock(mutex_);if(!std::isfinite(temperature)||temperature< -90||temperature>65||code<0||code>99||observedAt<=weatherMs_)return;
    const double comfort=std::clamp(1.0-std::abs(temperature-20.0)/40.0,0.0,1.0);const double sky=code<=3?0.03:code>=51?-0.04:0.0;
    weatherMood_=std::clamp(0.46+comfort*0.08+sky,0.35,0.65);weatherComfort_=comfort;weatherCode_=code;weatherMs_=observedAt;
}
}
