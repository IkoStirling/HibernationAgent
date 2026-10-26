#pragma once
#include <Hibernation/TextPlayback.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <string>
#include <cctype>
namespace hibernation::drops {
using Json=nlohmann::json;
inline Json kinds(){return Json::array({"berry","leaf","seed","paper_star","apple","bread","pudding","tea","crystal"});}
inline bool known(const std::string& kind){const auto values=kinds();return kind=="sprout"||std::find(values.begin(),values.end(),kind)!=values.end();}
inline std::string name(const std::string& kind){
    const std::map<std::string,std::string> names={{"berry","浆果"},{"leaf","叶片"},{"seed","种子"},{"paper_star","纸星"},{"apple","小苹果"},{"bread","小面包"},{"pudding","布丁"},{"tea","一杯清茶"},{"crystal","小晶石"},{"sprout","芽苗"}};
    const auto found=names.find(kind);return found==names.end()?"小物":found->second;
}
inline Json rules(const std::string& kind,int quality=0){
    int nutrition=0;double comfort=.005,weight=.2;
    if(kind=="berry"){nutrition=22;comfort=.015;weight=.15;}
    if(kind=="leaf"){nutrition=10;comfort=.01;weight=.05;}
    if(kind=="seed"){nutrition=8;comfort=.005;weight=.04;}
    if(kind=="apple"){nutrition=18;comfort=.015;weight=.6;}
    if(kind=="bread"){nutrition=30;comfort=.012;weight=.8;}
    if(kind=="pudding"){nutrition=20;comfort=.02;weight=.5;}
    if(kind=="tea"){nutrition=6;comfort=.018;weight=.4;}
    const double multiplier=1+std::clamp(quality,0,2)*.1;
    return {{"edible",nutrition>0},{"nutrition",static_cast<int>(std::round(nutrition*multiplier))},{"comfort",std::min(.025,comfort*multiplier)},{"weight",weight},{"quality",quality},{"rarity",quality==2?"rare":quality==1?"uncommon":"common"}};
}
inline bool edible(const std::string& kind){return rules(kind).at("edible");}
inline Json definitionSchema(){
    const Json shortText={{"type","string"},{"minLength",1},{"maxLength",24}};
    const Json traitValue={{"anyOf",Json::array({Json{{"type","string"},{"maxLength",80}},Json{{"type","number"},{"minimum",-1000},{"maximum",1000}},Json{{"type","boolean"}}})}};
    const Json trait={{"type","object"},{"additionalProperties",false},{"required",Json::array({"key","value"})},{"properties",{{"key",shortText},{"value",traitValue}}}};
    const Json properties={
        {"name",{{"type","string"},{"minLength",1},{"maxLength",40}}},
        {"description",{{"type","string"},{"maxLength",240}}},
        {"tint",{{"type","string"},{"enum",Json::array({"original","rose","mint","amber","sky","violet"})}}},
        {"tags",{{"type","array"},{"maxItems",6},{"items",shortText}}},
        {"traits",{{"type","array"},{"maxItems",6},{"items",trait}}}
    };
    return {{"type","object"},{"additionalProperties",false},{"properties",properties}};
}
inline bool validText(const Json& value,std::size_t maximum,bool empty=false){if(!value.is_string())return false;const auto& text=value.get_ref<const std::string&>();return (empty||text.find_first_not_of(" \r\n\t")!=std::string::npos)&&textOffsets(text).size()<=maximum+1&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<32;});}
inline Json definition(const std::string& kind,Json input=Json::object()){
    if(!input.contains("name"))input["name"]=name(kind);if(!input.contains("description"))input["description"]="";if(!input.contains("tags"))input["tags"]=Json::array();if(!input.contains("traits"))input["traits"]=Json::array();if(!input.contains("tint"))input["tint"]="original";return input;
}
inline bool validDefinition(const Json& value){
    if(!value.is_object()||value.size()!=5||!value.contains("name")||!value.contains("description")||!value.contains("tint")||!value.contains("tags")||!value.contains("traits"))return false;
    if(!validText(value.at("name"),40)||!validText(value.at("description"),240,true)||!value.at("tint").is_string())return false;
    const std::set<std::string> tints={"original","rose","mint","amber","sky","violet"};if(!tints.contains(value.at("tint").get<std::string>()))return false;
    const auto& tags=value.at("tags");const auto& traits=value.at("traits");if(!tags.is_array()||tags.size()>6||!traits.is_array()||traits.size()>6)return false;
    std::set<std::string> seen;for(const auto& tag:tags)if(!validText(tag,24)||!seen.insert(tag.get<std::string>()).second)return false;seen.clear();
    const std::set<std::string> reserved={"id","kind","edible","nutrition","comfort","quality","rarity","weight","born","expires","lifetime","permissions","scope","sources","饱腹值","寿命","稀有度","权限","可食用"};
    for(const auto& trait:traits){if(!trait.is_object()||trait.size()!=2||!trait.contains("key")||!trait.contains("value")||!validText(trait.at("key"),24))return false;auto key=trait.at("key").get<std::string>();for(auto& c:key)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(reserved.contains(key)||!seen.insert(key).second)return false;const auto& item=trait.at("value");if(item.is_string()){if(!validText(item,80,true))return false;}else if(item.is_number()){const double number=item;if(!std::isfinite(number)||number<-1000||number>1000)return false;}else if(!item.is_boolean())return false;}
    return value.dump().size()<=4096;
}
inline double opacity(const Json& item,double clock){
    const double born=item.at("born"),expires=item.at("expires"),life=std::max(.01,expires-born),age=clock-born,remaining=expires-clock;
    auto smooth=[](double x){x=std::clamp(x,0.0,1.0);return x*x*(3-2*x);};return std::min(smooth(age/std::min(.9,life*.15)),smooth(remaining/std::min(3.0,life*.25)));
}
inline bool fading(const Json& item,double clock){return item.at("expires").get<double>()-clock<=std::min(3.0,(item.at("expires").get<double>()-item.at("born").get<double>())*.25);}
}
