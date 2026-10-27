#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace hibernation {
// Select only from caller-verified original records. An offered encounter is
// neither an assigned task nor a new memory, preference, or experience.
inline nlohmann::json conversationOpportunities(const nlohmann::json& records,const nlohmann::json& offered){
    using Json=nlohmann::json;std::vector<const Json*> users;
    for(const auto& row:records)if(row.value("kind",std::string{})=="user_message"&&row.contains("id")&&row.contains("revision"))users.push_back(&row);
    std::stable_sort(users.begin(),users.end(),[](const auto* a,const auto* b){return a->value("created_at",std::int64_t(0))<b->value("created_at",std::int64_t(0));});
    Json result=Json::array();if(users.empty())return result;
    const auto append=[&](const Json& user,const char* role){
        const auto entry=[&](const Json& row){result.push_back({{"id",row.at("id")},{"revision",row.at("revision")},{"encounter_id",user.at("id")},{"context_role",role},{"not_a_current_request",true}});};
        entry(user);const auto request=user.value("request_id",std::string{});if(request.empty())return;
        const Json* answer=nullptr;for(const auto& row:records)if(row.value("kind",std::string{})=="assistant_message"&&row.value("request_id",std::string{})==request&&row.value("created_at",std::int64_t(0))>=user.value("created_at",std::int64_t(0))&&(!answer||row.value("created_at",std::int64_t(0))>answer->value("created_at",std::int64_t(0))))answer=&row;
        if(answer)entry(*answer);
    };
    // A bounded recent window prevents planning from trawling the whole life.
    const auto begin=users.size()>24?users.size()-24:0;
    for(std::size_t i=users.size()-1;i>begin;){const auto& user=*users[--i];const Json ref={{"id",user.at("id")},{"revision",user.at("revision")}};
        if(std::find(offered.begin(),offered.end(),ref)!=offered.end())continue;append(user,"earlier_encounter");break;
    }
    // Keep the newest correction last, after the older encounter it may qualify.
    append(*users.back(),"latest_encounter");return result;
}
inline nlohmann::json rememberOfferedEncounters(nlohmann::json before,const nlohmann::json& evidence){
    using Json=nlohmann::json;if(!before.is_array())before=Json::array();
    for(const auto& item:evidence){const auto role=item.value("context_role",std::string{});if(item.value("kind",std::string{})!="user_message"||(role!="latest_encounter"&&role!="earlier_encounter"))continue;
        const Json ref={{"id",item.at("id")},{"revision",item.at("revision")}};if(std::find(before.begin(),before.end(),ref)==before.end())before.push_back(ref);
    }while(before.size()>256)before.erase(before.begin());return before;
}
}
