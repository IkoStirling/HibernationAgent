#pragma once
#include <nlohmann/json.hpp>
#include <functional>
namespace hibernation::background_projection {
using Json=nlohmann::json;
// The model never supplies these envelopes. Caller verifies the current policy
// fence AND every retained source before loading either input-only stream.
inline bool reusable(const Json& cache,const std::string& key,const Json& fence,const std::function<bool(const Json&)>& allowed)try{
    if(cache.at("version")!=1||cache.at("key")!=key||cache.at("fence")!=fence||!cache.at("state").is_object())return false;
    const auto& refs=cache.at("sources");if(!refs.is_array()||refs.size()>128)return false;
    for(const auto& ref:refs)if(!ref.at("id").is_string()||!ref.at("scope").is_string()||!ref.at("revision").is_number_integer()||ref.at("revision").get<std::int64_t>()<1||!allowed(ref))return false;
    return cache.at("state").value("enabled",false);
}catch(...){return false;}
inline Json bind(const Json& state,const std::string& key,const Json& fence,const Json& refs){
    if(!state.is_object()||!state.value("enabled",false)||!refs.is_array()||refs.size()>128)return Json::object();
    return {{"version",1},{"key",key},{"fence",fence},{"sources",refs},{"state",state}};
}
}
