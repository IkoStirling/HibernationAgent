#pragma once
#include <Hibernation/DropDefinition.h>
#include <supplies/Backpack.h>
namespace hibernation::supplies {
using Json=nlohmann::json;
inline std::vector<bsimmer::supplies::Resource> catalog(){return {
 {"flour",false,0},{"water",true,2},{"butter",false,0},{"milk",true,8},{"sugar",false,0},{"tea_leaf",false,0},
 {"berry",true,22},{"leaf",true,10},{"seed",true,8},{"apple",true,18},
 {"meal.bread",true,30},{"meal.pudding",true,20},{"meal.tea",true,6}};}
inline bool known(const std::string& id){for(const auto& r:catalog())if(r.id==id)return true;return false;}
inline std::string resource(const Json& item){
 if(item.value("kind",std::string{})=="sprout")return {};
 const auto p=item.value("provenance",Json::object());if(p.is_object()&&p.value("type",std::string{})=="field_resource"){
  const auto id=p.value("ingredient_id",std::string{});return known(id)?id:std::string{};
 }
 const auto kind=item.value("kind",std::string{});if(kind=="berry"||kind=="leaf"||kind=="seed"||kind=="apple")return kind;
 if(kind=="bread"||kind=="pudding"||kind=="tea")return "meal."+kind;return {};
}
inline std::string templateFor(const std::string& id){if(id=="flour")return "bread";if(id=="butter"||id=="sugar")return "pudding";if(id=="water"||id=="milk")return "tea";if(id=="tea_leaf")return "leaf";return id;}
inline std::string name(const std::string& id){const std::map<std::string,std::string> names={{"flour","面粉袋"},{"water","清水"},{"butter","黄油"},{"milk","牛奶"},{"sugar","糖"},{"tea_leaf","茶叶"}};if(const auto it=names.find(id);it!=names.end())return it->second;return drops::name(id);}
inline Json defaultRegions(){return Json::array({
 {{"id","windowsill"},{"name","窗边"},{"left",.05},{"top",.2},{"right",.35},{"bottom",.75},{"resources",Json::array({"berry","leaf","tea_leaf"})},{"capacity",2},{"refresh_seconds",120}},
 {{"id","garden"},{"name","种植角"},{"left",.65},{"top",.25},{"right",.95},{"bottom",.75},{"resources",Json::array({"seed","apple"})},{"capacity",2},{"refresh_seconds",150}},
 {{"id","supplies"},{"name","补给角"},{"left",.35},{"top",.65},{"right",.65},{"bottom",.92},{"resources",Json::array({"flour","water","butter","milk","sugar"})},{"capacity",2},{"refresh_seconds",180}}
 });}
inline std::vector<bsimmer::supplies::Region> regions(const Json& input){
 if(!input.is_array())throw std::runtime_error("regions_array");std::vector<bsimmer::supplies::Region> out;
 for(const auto& v:input){if(!v.at("capacity").is_number_integer()||v.at("capacity").get<int>()<1||v.at("capacity").get<int>()>8)throw std::runtime_error("region_capacity");out.push_back({v.at("id"),v.at("name"),v.at("left"),v.at("top"),v.at("right"),v.at("bottom"),v.at("refresh_seconds"),v.at("capacity"),v.at("resources").get<std::vector<std::string>>()});}
 std::vector<std::string> ids;for(const auto& r:catalog())if(!r.id.starts_with("meal."))ids.push_back(r.id);
 if(!bsimmer::supplies::validRegions(out,ids))throw std::runtime_error("regions_invalid");return out;
}
}
