#pragma once
#include <Hibernation/DesktopSupplies.h>
#include <cmath>
#include <optional>
#include <map>
#include <set>

namespace hibernation {
using Json=nlohmann::json;
// An opportunity, not an intention or a completed experience. The normal queue
// and shared inventory remain responsible for execution and its receipt.
inline std::optional<Json> localForagingOpportunity(const Json& snapshots,const Json& library,double energy,double roll,double probability=.25){
    const auto widget=snapshots.value("widget",Json::object());
    const auto habitat=snapshots.value("habitat",Json::object());
    const auto kitchen=snapshots.value("canteen",Json::object()).value("current",Json::object());
    const auto inventory=kitchen.value("inventory",Json::object());
    const auto state=widget.value("state",std::string{});
    if(energy<.35||!widget.value("visible",false)||widget.value("moving",false)||widget.value("dragging",false)||widget.value("dropping",false)
        ||(state!="idle"&&state!="curious"&&state!="blink")||!widget.value("pending_pose",std::string{}).empty()||!widget.value("held_pose",std::string{}).empty()
        ||!habitat.value("inventory_available",false)||!habitat.value("canteen_supplies",false)||!habitat.value("pending",Json()).is_null()||!habitat.value("gather",Json()).is_null())return {};
    const auto animation=widget.value("animation",Json::object());
    if(animation.value("backend",std::string{})=="png_sequence"&&animation.value("phase",std::string{})!="loop")return {};
    const auto capacity=inventory.value("capacity",0),used=inventory.value("used",0);
    // Leave a slot for a deliberate user/model action. Missing inventory fails closed.
    if(capacity<=1||used>=capacity-1)return {};
    std::map<std::string,int> stock;
    for(const auto& item:inventory.value("items",Json::array())){
        if(item.value("remaining_seconds",1.0)<=0)continue;
        stock[item.value("ingredient_id",std::string{})]+=item.value("quantity",0);
    }
    std::set<std::string> familiar;
    for(const auto& item:library.value("items",Json::array())){
        const auto command=item.value("command",Json::object());
        if(command.value("kind",std::string{})=="gather")familiar.insert(command.value("ingredient_id",std::string{}));
    }
    const double clock=habitat.value("clock",0.0),x=widget.value("x",.5),y=widget.value("y",.5);
    std::optional<Json> best;double bestScore=-1;
    for(const auto& object:habitat.value("objects",Json::array())){
        const auto ingredient=supplies::resource(object);
        if(!object.value("collectible",false)||ingredient.empty()||object.value("expires",clock)-clock<30||stock[ingredient]>=2)continue;
        const bool learned=familiar.contains(ingredient);
        // Missing material > a previously chosen collection > incidental curiosity.
        // This is bounded game behavior, not a fabricated personal preference.
        bool cookingMaterial=false;
        for(const auto& row:kitchen.value("pantry",Json::array()))if(row.value("id",std::string{})==ingredient)cookingMaterial=true;
        const double chance=std::min(1.0,probability*(cookingMaterial&&stock[ingredient]==0?2.6:learned?1.8:1.0));
        if(roll>=chance)continue;
        const double score=(stock[ingredient]==0?3.0:0.0)+(learned?1.0:0.0)+(cookingMaterial?.5:0.0)-std::hypot(x-object.value("x",.5),y-object.value("y",.5));
        if(score<=bestScore)continue;
        bestScore=score;best=Json{{"command",{{"kind","gather"},{"seconds",0},{"ingredient_id",ingredient},{"count",1},{"destination","backpack"}}},{"reason",cookingMaterial&&stock[ingredient]==0?"low_stock":learned?"prior_collection":"incidental_discovery"}};
    }
    return best;
}
}
