#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace hibernation {
// Application rules for virtual desk objects. Numeric needs are owned here;
// model tools propose concrete actions and cannot assign these values.
class SmallWorld {
public:
    using Json=nlohmann::json;
    static bool knownKind(const std::string& kind){return kind=="berry"||kind=="leaf"||kind=="seed"||kind=="paper_star"||kind=="sprout";}
    static bool edible(const std::string& kind){return kind=="berry"||kind=="leaf"||kind=="seed";}
    static const char* growthStage(int value){return value<=0?"newly_planted":value==1?"leaves":value==2?"bud":"flowering";}
    double clock()const{return clock_;}
    bool pending()const{return !pending_.is_null();}
    bool walking()const{return pending()&&pending_.at("phase")=="walking";}
    bool queued()const{return pending()&&pending_.at("phase")=="queued";}
    bool acting()const{return pending()&&pending_.at("phase")=="acting";}
    std::size_t size()const{return objects_.size();}
    const Json& objects()const{return objects_;}
    const Json& action()const{return pending_;}
    bool wantsFood()const{return std::min(appetite_,targetAppetite_)>.48;}
    static bool newObservation(const Json& item){return item.value("observed_growth",-1)!=item.value("growth",0);}
    void configure(std::size_t maximum,double ttl){maximum_=maximum;ttl_=ttl;}
    Json object(const std::string& id)const{for(const auto& item:objects_)if(item.at("id")==id)return item;return nullptr;}
    bool place(const std::string& id,const std::string& kind,double x,double y){
        if(id.empty()||id.size()>96||!knownKind(kind)||!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1||object(id).is_object()||size()>=maximum_)return false;
        objects_.push_back({{"id",id},{"kind",kind},{"x",x},{"y",y},{"born",clock_},{"expires",clock_+ttl_},{"growth",0},{"observed_growth",-1}});
        emit("habitat.appeared",objects_.back());return true;
    }
    std::string request(const std::string& id,const std::string& action,const std::string& token){
        if(pending())return "world_action_busy";
        const auto item=object(id);if(item.is_null())return "world_object_missing";
        const auto kind=item.at("kind").get<std::string>();
        if(action!="look"&&action!="eat"&&action!="plant")return "world_action_invalid";
        if(action=="eat"&&!edible(kind))return "world_object_not_food";
        if(action=="eat"&&std::min(appetite_,targetAppetite_)<.12)return "world_not_hungry";
        if(action=="plant"&&kind!="seed")return "world_seed_required";
        if(token.empty()||token.size()>96)return "world_action_identity";
        pending_={{"id",id},{"action",action},{"token",token},{"phase","queued"},{"deadline",clock_+120.0}};
        return {};
    }
    bool beginWalk(){
        if(!queued())return false;const auto item=object(pending_.at("id"));if(item.is_null()){cancel("object_missing");return false;}
        pending_["phase"]="walking";events_.push_back({{"kind","habitat.navigate"},{"id",item.at("id")},{"token",pending_.at("token")},{"x",item.at("x")},{"y",item.at("y")}});return true;
    }
    bool arrive(const std::string& token,double x,double y){
        if(!std::isfinite(x)||!std::isfinite(y)||!walking()||pending_.at("token")!=token)return false;
        const auto item=object(pending_.at("id"));if(item.is_null()||distance(item,x,y)>.025)return false;
        pending_["phase"]="acting";const auto action=pending_.at("action").get<std::string>();pending_["complete_at"]=clock_+(action=="eat"?1.6:action=="plant"?2.2:.8);
        events_.push_back({{"kind","habitat.action_started"},{"action",action},{"token",token}});return true;
    }
    void cancel(const std::string& reason){
        if(!pending())return;events_.push_back({{"kind","habitat.action_cancelled"},{"token",pending_.at("token")},{"id",pending_.at("id")},{"reason",reason}});pending_=nullptr;
    }
    void tick(double elapsed,bool active,double actorX,double actorY,bool actorAvailable){
        if(!active||!std::isfinite(elapsed))return;const double dt=std::clamp(elapsed,0.0,.25);clock_+=dt;
        targetAppetite_=std::clamp(targetAppetite_+dt*.00012,0.0,1.0);appetite_=approach(appetite_,targetAppetite_,dt*.006);comfort_=approach(comfort_,targetComfort_,dt*.0008);
        for(auto it=objects_.begin();it!=objects_.end();){
            if(it->at("expires").get<double>()<=clock_){const auto gone=*it;if(pending()&&pending_.at("id")==gone.at("id"))cancel("object_expired");it=objects_.erase(it);emit("habitat.disappeared",gone);continue;}
            if(it->at("kind")=="sprout"){const auto age=clock_-it->at("born").get<double>();(*it)["growth"]=age>=3600?3:age>=1800?2:age>=600?1:0;}
            ++it;
        }
        if(!pending())return;
        if(!actorAvailable){cancel("character_unavailable");return;}
        if(clock_>=pending_.at("deadline").get<double>()){cancel("action_timeout");return;}
        if(!acting())return;
        auto item=object(pending_.at("id"));if(item.is_null()||distance(item,actorX,actorY)>.03){cancel("character_moved");return;}
        if(clock_<pending_.at("complete_at").get<double>())return;
        const auto action=pending_.at("action").get<std::string>(),token=pending_.at("token").get<std::string>();
        if(action=="eat"){
            targetAppetite_=std::max(0.0,targetAppetite_-.22);targetComfort_=std::min(.85,targetComfort_+.015);
            for(auto it=objects_.begin();it!=objects_.end();++it)if(it->at("id")==item.at("id")){objects_.erase(it);break;}
        }else if(action=="plant"){
            for(auto& value:objects_)if(value.at("id")==item.at("id")){value["kind"]="sprout";value["born"]=clock_;value["expires"]=clock_+43200.0;value["growth"]=0;value["observed_growth"]=0;item=value;break;}
            targetComfort_=std::min(.85,targetComfort_+.01);
        }else{targetComfort_=std::min(.85,targetComfort_+.005);for(auto& value:objects_)if(value.at("id")==item.at("id")){value["observed_growth"]=value.at("growth");item=value;break;}}
        const std::string kind=action=="eat"?"habitat.consumed":action=="plant"?"habitat.planted":"habitat.inspected";
        emit(kind,item,token);lastActions_.push_back({{"kind",kind},{"item",item},{"at",clock_}});while(lastActions_.size()>4)lastActions_.erase(0);pending_=nullptr;
    }
    Json observe()const{
        Json items=Json::array();for(const auto& item:objects_){Json value={{"id",item.at("id")},{"kind",item.at("kind")},{"x",item.at("x")},{"y",item.at("y")},{"growth",item.at("growth")}};if(item.at("kind")=="sprout")value["appearance_stage"]=growthStage(item.at("growth"));items.push_back(std::move(value));}
        return {{"objects",items},{"appetite",std::min(appetite_,targetAppetite_)<.12?"satisfied":appetite_>.6?"hungry":"mild"},{"pending",pending_},{"last_actions",lastActions_},{"guide","物件是程序内的虚拟物件。接近和动作完成有真实回执；accepted不等于已经吃掉或观察。可按自己的意愿选择或忽略，不报告内部数值。"}};
    }
    Json snapshot()const{return {{"version",1},{"clock",clock_},{"objects",objects_},{"pending",pending_},{"appetite",appetite_},{"target_appetite",targetAppetite_},{"comfort",comfort_},{"target_comfort",targetComfort_},{"last_actions",lastActions_}};}
    bool restore(const Json& value){
        try{
            if(!value.is_object()||value.value("version",0)!=1)return false;
            auto bounded=[](const Json& j,const char* key,double lo,double hi){const double x=j.at(key);if(!std::isfinite(x)||x<lo||x>hi)throw 0;return x;};
            const double clock=bounded(value,"clock",0,1e10),appetite=bounded(value,"appetite",0,1),targetAppetite=bounded(value,"target_appetite",0,1),comfort=bounded(value,"comfort",0,1),targetComfort=bounded(value,"target_comfort",0,1);
            const auto objects=value.at("objects"),actions=value.at("last_actions");if(!objects.is_array()||objects.size()>8||!actions.is_array()||actions.size()>4)return false;
            std::vector<std::string> ids;for(const auto& item:objects){const auto id=item.at("id").get<std::string>(),kind=item.at("kind").get<std::string>();if(id.empty()||id.size()>96||!knownKind(kind)||std::find(ids.begin(),ids.end(),id)!=ids.end())return false;ids.push_back(id);bounded(item,"x",0,1);bounded(item,"y",0,1);bounded(item,"born",0,clock);bounded(item,"expires",0,1e10);bounded(item,"growth",0,3);}
            clock_=clock;appetite_=appetite;targetAppetite_=targetAppetite;comfort_=comfort;targetComfort_=targetComfort;objects_=objects;lastActions_=actions;pending_=nullptr;events_.clear();
            if(value.contains("pending")&&!value.at("pending").is_null())events_.push_back({{"kind","habitat.action_cancelled"},{"reason","application_restarted"}});
            return true;
        }catch(...){return false;}
    }
    std::vector<Json> takeEvents(){auto result=std::move(events_);events_.clear();return result;}
private:
    static double approach(double current,double target,double limit){return current+std::clamp(target-current,-limit,limit);}
    static double distance(const Json& item,double x,double y){return std::hypot(item.at("x").get<double>()-x,item.at("y").get<double>()-y);}
    void emit(const std::string& kind,const Json& item,const std::string& token={}){events_.push_back({{"kind",kind},{"item",item},{"token",token}});}
    double clock_=0,appetite_=.4,targetAppetite_=.4,comfort_=.5,targetComfort_=.5,ttl_=600;
    std::size_t maximum_=3;Json objects_=Json::array(),pending_,lastActions_=Json::array();std::vector<Json> events_;
};
}
