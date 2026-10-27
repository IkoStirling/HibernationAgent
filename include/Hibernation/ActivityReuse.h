#pragma once
#include <Hibernation/ActivityPlan.h>
#include <Hibernation/DropDefinition.h>
#include <algorithm>
#include <optional>
#include <set>
namespace hibernation {
// Reusable intentions, never replayed receipts or external side effects.
inline Json emptyActivityLibrary(){return {{"version",1},{"items",Json::array()},{"last_used",std::string{}},{"next_ms",0}};}
inline std::optional<Json> reusableActivity(const Json& value){
    if(!value.is_object())return {};
    const auto kind=value.value("kind",std::string{});
    if(kind!="wait"&&kind!="pose"&&kind!="move_to"&&kind!="eat_nearest"&&kind!="look_nearest"&&kind!="rest")return {};
    Json command={{"kind",kind},{"seconds",value.value("seconds",0.0)}};
    if(kind=="pose")command["state"]=value.value("state",std::string{});
    if(kind=="move_to"){if(!value.contains("x")||!value.contains("y"))return {};command["x"]=value.at("x");command["y"]=value.at("y");}
    if(!validateActivities(Json{{"version",1},{"commands",Json::array({command})}}.dump()).ok)return {};
    return command;
}
inline Json retainActivity(Json library,const Json& completed,const std::string& signature,std::int64_t now){
    if(!library.is_object()||library.value("version",0)!=1||!library.value("items",Json()).is_array())library=emptyActivityLibrary();
    auto& items=library["items"];
    std::erase_if(items.get_ref<Json::array_t&>(),[&](const auto& item){return now-item.value("completed_ms",std::int64_t(0))>7LL*86400000||item.value("signature",std::string{})!=signature;});
    const auto command=reusableActivity(completed);
    if(completed.value("status",std::string{})!="completed"||!command)return library;
    const auto key=command->dump();
    std::erase_if(items.get_ref<Json::array_t&>(),[&](const auto& item){return item.at("command")==*command;});
    items.push_back({{"key",key},{"command",*command},{"signature",signature},{"from_state",completed.value("from_state",std::string("idle"))},{"source_activity",completed.value("id",std::string{})},{"completed_ms",completed.value("ended_ms",now)},{"uses",0}});
    while(items.size()>48)items.erase(0);return library;
}
inline Json reusableCandidates(const Json& library,const Json& widget,const Json& habitat,const Json& available,const std::string& signature,std::int64_t now,double energy){
    Json out=Json::array();
    if(now<library.value("next_ms",std::int64_t(0))||!widget.value("visible",false)||widget.value("moving",false)||widget.value("dragging",false)||widget.value("dropping",false)||!widget.value("pending_pose",std::string{}).empty()||!widget.value("held_pose",std::string{}).empty())return out;
    const auto state=widget.value("state",std::string("idle"));
    const auto stable=[](const std::string& s){return s=="idle"||s=="curious"||s=="listening"||s=="blink";};
    const auto animation=widget.value("animation",Json::object());if(!stable(state)||(animation.value("backend",std::string{})=="png_sequence"&&animation.value("phase",std::string{})!="loop"))return out;
    for(const auto& item:library.value("items",Json::array())){
        if(item.value("signature",std::string{})!=signature||now-item.value("completed_ms",std::int64_t(0))>7LL*86400000||(item.value("key",std::string{})==library.value("last_used",std::string{})&&now-library.value("last_used_ms",std::int64_t(0))<180000))continue;
        const auto command=reusableActivity(item.value("command",Json()));if(!command)continue;
        const auto kind=command->at("kind").get<std::string>();if(std::find(available.begin(),available.end(),kind)==available.end())continue;
        const auto from=item.value("from_state",std::string("idle"));if(from!=state&&!(stable(from)&&stable(state)))continue;
        if(kind=="rest"&&energy>.65)continue;
        if(kind=="pose"){
            const auto pose=command->at("state").get<std::string>();if((pose=="breathing"&&energy>.4)||(pose=="resting"&&energy>.65))continue;
            const auto poses=widget.value("animation_catalog",Json::array());if(std::none_of(poses.begin(),poses.end(),[&](const auto& p){return p.at("state")==pose;}))continue;
        }
        if(kind=="eat_nearest"||kind=="look_nearest"){
            if(!habitat.value("pending",Json()).is_null())continue;
            if(kind=="eat_nearest"&&std::min(habitat.value("appetite",0.0),habitat.value("target_appetite",0.0))<.48)continue;
            bool found=false;for(const auto& object:habitat.value("objects",Json::array()))if(kind=="look_nearest"||drops::edible(object.value("kind",std::string{}))){found=true;break;}if(!found)continue;
        }
        out.push_back(item);
    }return out;
}
}
