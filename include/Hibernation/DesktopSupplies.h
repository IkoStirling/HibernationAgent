#pragma once
#include <Hibernation/DropDefinition.h>
#include <supplies/Backpack.h>
#include <cuisine/Catalog.h>
namespace hibernation::supplies {
using Json=nlohmann::json;
inline std::vector<bsimmer::supplies::Resource> catalog(){
 std::vector<bsimmer::supplies::Resource> out;for(const auto& f:bsimmer::cuisine::catalog().foods())out.push_back({f.id,f.edible,f.nutrition});return out;
}
inline bool known(const std::string& id){return bsimmer::cuisine::catalog().food(id)!=nullptr;}
inline std::string resource(const Json& item){
 if(item.value("kind",std::string{})=="sprout")return {};
 const auto p=item.value("provenance",Json::object());if(p.is_object()&&p.value("type",std::string{})=="field_resource"){
  const auto id=p.value("ingredient_id",std::string{});return known(id)?id:std::string{};
 }
 if(p.is_object()&&p.value("type",std::string{})=="canteen_meal"){if(const auto* r=bsimmer::cuisine::catalog().dish(p.value("recipe_id",std::string{})))return r->output;}
 const auto kind=item.value("kind",std::string{});if(kind=="berry"||kind=="leaf"||kind=="seed"||kind=="apple")return kind;
 if(kind=="bread"||kind=="pudding"||kind=="tea")return "meal."+kind;return {};
}
inline Json objectRules(const Json& item){
 const auto kind=item.at("kind").get<std::string>();const int quality=item.at("rules").value("quality",0);auto result=drops::rules(kind,quality);
 const auto identity=resource(item);if(const auto* f=bsimmer::cuisine::catalog().food(identity)){
  result["edible"]=f->edible;result["nutrition"]=static_cast<int>(std::round(f->nutrition*(1+quality*.1)));
 }return result;
}
inline std::string templateFor(const std::string& id){const auto* f=bsimmer::cuisine::catalog().food(id);return f?f->presentation:std::string("bread");}
inline std::string name(const std::string& id){const auto* f=bsimmer::cuisine::catalog().food(id);return f?f->name:drops::name(id);}
inline Json defaultRegions(){return Json::array({
 {{"id","windowsill"},{"name","窗边"},{"left",.05},{"top",.2},{"right",.35},{"bottom",.75},{"resources",Json::array({"berry","leaf","tea_leaf"})},{"capacity",2},{"refresh_seconds",120}},
 {{"id","garden"},{"name","种植角"},{"left",.65},{"top",.25},{"right",.95},{"bottom",.75},{"resources",Json::array({"seed","apple"})},{"capacity",2},{"refresh_seconds",150}},
 {{"id","supplies"},{"name","补给角"},{"left",.35},{"top",.65},{"right",.65},{"bottom",.92},{"resources",Json::array({"flour","water","butter","milk","sugar"})},{"capacity",2},{"refresh_seconds",180}}
 });}
inline std::vector<bsimmer::supplies::Region> regions(const Json& input){
 if(!input.is_array())throw std::runtime_error("regions_array");std::vector<bsimmer::supplies::Region> out;
 for(const auto& v:input){if(!v.at("capacity").is_number_integer()||v.at("capacity").get<int>()<1||v.at("capacity").get<int>()>8)throw std::runtime_error("region_capacity");out.push_back({v.at("id"),v.at("name"),v.at("left"),v.at("top"),v.at("right"),v.at("bottom"),v.at("refresh_seconds"),v.at("capacity"),v.at("resources").get<std::vector<std::string>>()});}
 std::vector<std::string> ids;for(const auto& r:bsimmer::cuisine::catalog().foods())if(r.raw)ids.push_back(r.id);
 if(!bsimmer::supplies::validRegions(out,ids))throw std::runtime_error("regions_invalid");return out;
}
}
