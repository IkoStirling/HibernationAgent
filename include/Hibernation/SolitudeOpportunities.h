#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <algorithm>
namespace hibernation {
using SolitudeJson=nlohmann::json;
inline bool validActivityConsideration(const SolitudeJson& value){
    if(!value.is_object()||!value.contains("version")||!value.at("version").is_number_integer()||value.at("version")!=1||!value.contains("considered")||!value.at("considered").is_array()||value.at("considered").size()>128||!value.contains("last_ms")||!value.at("last_ms").is_number_integer()||value.at("last_ms").get<std::int64_t>()<0)return false;
    for(const auto& key:value.at("considered"))if(!key.is_string()||key.get_ref<const std::string&>().empty()||key.get_ref<const std::string&>().size()>800)return false;return true;
}
inline SolitudeJson emptyActivityConsideration(){return {{"version",1},{"last_ms",0},{"considered",SolitudeJson::array()}};}
inline std::string activityConsiderationKey(const SolitudeJson& record){
    try{
    if(!record.is_object()||record.value("kind",std::string{})!="tool_observation"||!record.contains("result")||!record.at("result").is_object())return {};
    const auto& result=record.at("result");const auto tool=record.value("tool",std::string{});
    if(tool=="habitat.create"){
        const auto object=result.value("object",SolitudeJson::object());
        if(object.is_object()&&object.value("creator",std::string{})=="model"&&object.contains("id")){const auto key=SolitudeJson::array({"created_desk_object",object.at("id")}).dump();return key.size()<=800?key:std::string{};}
    }
    if(tool=="pocket.press"||tool=="pocket.leave"){
        const auto game=result.value("game",SolitudeJson::object());
        if(result.value("applied",false)&&game.is_object()&&game.contains("id")){
            const auto status=game.value("status",std::string{});if(status=="won"||status=="left"||status=="exhausted"){const auto key=SolitudeJson::array({"puzzle_outcome",game.at("id"),status}).dump();return key.size()<=800?key:std::string{};}
        }
    }
    if(tool=="atelier.write"||tool=="atelier.append"){
        const auto piece=result.value("piece",SolitudeJson::object());if(!result.value("written",false)||!result.value("completed",false)||!piece.contains("id")||!piece.contains("revision"))return {};
        const auto key=SolitudeJson::array({"authored_piece",piece.at("id"),piece.at("revision")}).dump();return key.size()<=800?key:std::string{};
    }
    if(tool=="reading.open"&&result.value("scope",std::string{})=="article_lead_summary"&&result.contains("text")&&result.at("text").is_string()&&!result.at("text").get_ref<const std::string&>().empty()){
        const auto key=SolitudeJson::array({"read_lead",result.value("source_url",std::string{}),result.value("revision",std::string{}),result.value("retrieved_ms",std::int64_t(0))}).dump();return key.size()<=800?key:std::string{};
    }
    return {};
    }catch(...){return {};}
}
inline bool activityConsiderationDue(const SolitudeJson& saved,std::int64_t now,bool forced){
    if(!validActivityConsideration(saved)||now<saved.at("last_ms").get<std::int64_t>())return false;
    return forced||saved.at("last_ms")==0||now-saved.at("last_ms").get<std::int64_t>()>=900000;
}
inline bool activityAlreadyConsidered(const SolitudeJson& saved,const std::string& key){return !validActivityConsideration(saved)||key.empty()||std::find(saved.at("considered").begin(),saved.at("considered").end(),key)!=saved.at("considered").end();}
inline SolitudeJson markActivitiesConsidered(SolitudeJson saved,const SolitudeJson& keys,std::int64_t now){
    if(!validActivityConsideration(saved)||!keys.is_array()||now<saved.at("last_ms").get<std::int64_t>())return saved;
    for(const auto& key:keys)if(key.is_string()&&key.get_ref<const std::string&>().size()<=800&&!activityAlreadyConsidered(saved,key.get<std::string>())){saved["considered"].push_back(key);while(saved["considered"].size()>128)saved["considered"].erase(0);}
    saved["last_ms"]=now;return saved;
}
}
