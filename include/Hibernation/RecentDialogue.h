#pragma once
#include <Hibernation/ContextControl.h>
#include <map>
namespace hibernation {
// Input is chronological. Protect the most recent completed request/reply pair
// before older, same-priority history can consume a limited context window.
inline std::set<std::string> latestDialoguePair(const nlohmann::json& rows,const std::string& current){
    std::string request,assistant;std::set<std::string> pair;
    for(auto it=rows.rbegin();it!=rows.rend();++it){
        if(it->at("id")==current)continue;const auto& data=it->at("data");const auto kind=data.value("kind",std::string{});
        if(assistant.empty()&&kind=="assistant_message"){request=data.value("request_id",std::string{});assistant=it->at("id");}
        else if(!assistant.empty()&&kind=="user_message"&&!request.empty()&&data.value("request_id",std::string{})==request){pair.insert(assistant);pair.insert(it->at("id").get<std::string>());break;}
    }return pair;
}
inline nlohmann::json boundedCausalDialogue(nlohmann::json data){
    if(data.contains("text")&&data.at("text").is_string()&&data.at("text").get_ref<const std::string&>().size()>2400){data["text"]=contextExcerpt(data.at("text"),2400);data["context_excerpt"]=true;}
    return data;
}
inline void chronologicalRecentContext(std::vector<ayt::inference::ContextFragment>& fragments,const nlohmann::json& ids){
    std::map<std::string,std::size_t> positions;for(std::size_t i=0;i<ids.size();++i)positions[ids[i].get<std::string>()]=i;
    std::stable_sort(fragments.begin(),fragments.end(),[&](const auto& a,const auto& b){const auto ai=positions.find(a.id),bi=positions.find(b.id);if(ai==positions.end()||bi==positions.end())return ai!=positions.end()&&bi==positions.end();return ai->second<bi->second;});
}
inline bool asksRecentExperience(const std::string& query){
    for(const auto* word:{"刚才","刚刚","发生","最近做","刚吃","吃了","刚看"})if(query.find(word)!=std::string::npos)return true;return false;
}
}
