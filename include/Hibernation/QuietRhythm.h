#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace hibernation {
// A virtual resting interval, driven only by active process time and a verified
// widget pose acknowledgement. It never controls the user's computer or audio.
class QuietRhythm {
public:
    using Json=nlohmann::json;
    const std::string& phase()const{return phase_;}
    const std::string& token()const{return token_;}
    double clock()const{return clock_;}
    double idle()const{return idle_;}
    double nextAutomatic()const{return nextAutomatic_;}
    static bool restfulHour(int hour){return hour>=0&&hour<=23&&(hour>=23||hour<7);}
    static bool mayRest(int hour,double energy,const std::string& activity){return std::isfinite(energy)&&(restfulHour(hour)||energy<.35)&&(activity=="idle"||activity=="resting");}
    void postponeAutomatic(double seconds){nextAutomatic_=clock_+std::max(1.0,seconds);}
    std::string request(const std::string& token,double seconds,std::uint64_t epoch,const std::string& origin){
        if(phase_!="idle")return "rest_already_pending";
        if(token.empty()||token.size()>96||!std::isfinite(seconds)||seconds<15||seconds>1800||(origin!="model"&&origin!="automatic"))return "rest_request_invalid";
        token_=token;duration_=seconds;elapsed_=0;requestedAt_=clock_;epoch_=epoch;origin_=origin;phase_="queued";return {};
    }
    void tick(double elapsed,bool advance,bool busy,bool input,bool actor,const std::string& actorState,std::uint64_t epoch){
        if(!advance||!std::isfinite(elapsed))return;const double dt=std::clamp(elapsed,0.0,.25);clock_+=dt;
        if(busy||input||!actor)idle_=0;else idle_+=dt;
        if(phase_=="idle")return;
        if(epoch!=epoch_||input||!actor){finish(phase_=="active"?"interrupted":"cancelled",!actor?"character_unavailable":input?"input_started":"new_conversation");return;}
        if(phase_=="active"){
            if(busy){finish("interrupted","attention_changed");return;}
            elapsed_=std::min(duration_,elapsed_+dt);if(elapsed_>=duration_)finish("finished","duration_reached");return;
        }
        if(clock_-requestedAt_>=120){finish("cancelled","intent_expired");return;}
        if(phase_=="requesting"){if(clock_-poseRequestedAt_>1.5)finish("cancelled","pose_unavailable");return;}
        if(!busy&&(actorState=="idle"||actorState=="curious")){
            phase_="requesting";poseRequestedAt_=clock_;events_.push_back({{"kind","rhythm.pose.request"},{"token",token_},{"seconds",duration_}});
        }
    }
    bool acknowledged(const std::string& token){
        if(phase_!="requesting"||token!=token_)return false;phase_="active";events_.push_back(event("rhythm.rest_started","pose_confirmed"));return true;
    }
    bool wake(const std::string& reason){if(phase_=="idle")return false;finish(phase_=="active"?"interrupted":"cancelled",reason);return true;}
    Json observe(int hour,double energy)const{return {{"phase",phase_},{"remaining_seconds",phase_=="active"?duration_-elapsed_:duration_},{"day_phase",hour<7?"night":hour<12?"morning":hour<18?"afternoon":hour<23?"evening":"night"},{"energy",energy<.35?"low":energy>.8?"rested":"steady"},{"guide","这是角色的安静休息，不会锁定电脑、改变系统音量或要求用户陪同。accepted只是意图；实际休息有姿态确认。允许醒来、拒绝或保持安静，不朗读内部参数。"}};}
    Json snapshot()const{return {{"version",1},{"clock",clock_},{"idle_seconds",idle_},{"next_automatic",nextAutomatic_},{"phase",phase_},{"token",token_},{"duration",duration_},{"elapsed",elapsed_},{"origin",origin_},{"requested_at",requestedAt_},{"epoch",epoch_}};}
    bool restore(const Json& value){
        try{
            if(!value.is_object()||value.value("version",0)!=1)return false;auto finite=[&](const char* key,double max){const double number=value.at(key);if(!std::isfinite(number)||number<0||number>max)throw 0;return number;};
            const auto phase=value.at("phase").get<std::string>(),token=value.at("token").get<std::string>(),origin=value.at("origin").get<std::string>();if((phase!="idle"&&phase!="queued"&&phase!="requesting"&&phase!="active")||token.size()>96||(origin!="model"&&origin!="automatic"&& !origin.empty()))return false;
            const auto clock=finite("clock",1e10),duration=finite("duration",1800),elapsed=finite("elapsed",1800),requested=finite("requested_at",clock),next=finite("next_automatic",1e10);if(elapsed>duration||(phase!="idle"&&(token.empty()||duration<15)))return false;
            clock_=clock;duration_=duration;elapsed_=elapsed;token_=token;origin_=origin;requestedAt_=requested;nextAutomatic_=std::max(clock_+300,next);phase_=phase;idle_=0;events_.clear();
            if(phase_!="idle")finish(phase_=="active"?"interrupted":"cancelled","application_restarted");return true;
        }catch(...){return false;}
    }
    std::vector<Json> takeEvents(){auto out=std::move(events_);events_.clear();return out;}
private:
    Json event(const std::string& kind,const std::string& reason)const{return {{"kind",kind},{"token",token_},{"origin",origin_},{"requested_seconds",duration_},{"elapsed_seconds",elapsed_},{"reason",reason},{"clock",clock_}};}
    void finish(const std::string& result,const std::string& reason){
        if(phase_=="idle")return;events_.push_back({{"kind","rhythm.pose.release"},{"token",token_}});events_.push_back(event("rhythm.rest_"+result,reason));phase_="idle";token_.clear();duration_=elapsed_=0;idle_=0;nextAutomatic_=std::max(nextAutomatic_,clock_+600);
    }
    double clock_=0,idle_=0,nextAutomatic_=300,duration_=0,elapsed_=0,requestedAt_=0,poseRequestedAt_=0;
    std::string phase_="idle",token_,origin_;std::uint64_t epoch_=0;std::vector<Json> events_;
};
}
