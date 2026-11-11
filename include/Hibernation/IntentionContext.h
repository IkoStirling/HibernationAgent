#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
namespace hibernation::intention_life {
using Json=nlohmann::json;
// This selects content to assemble, never rights. The host separately checks
// the entire original evidence and decision history before every execution.
inline Json contentBasis(const Json& thread){
    Json result=Json::array();const auto append=[&](const Json& refs,std::size_t cap){std::size_t added=0;for(auto it=refs.rbegin();it!=refs.rend()&&added<cap;++it){if(std::find(result.begin(),result.end(),*it)==result.end()){result.push_back(*it);++added;}}};
    const auto episodes=thread.value("episodes",Json::array());if(!episodes.empty())append(episodes.back().value("sources",Json::array()),2);
    else append(thread.value("sources",thread.value("evidence_sources",Json::array())),2);
    append(thread.value("intention_pause",Json::object()).value("sources",Json::array()),2);return result;
}
inline Json briefPursuit(const Json& value){
    if(!value.is_object())return nullptr;Json result=Json::object();for(const auto* field:{"id","intention","domain","admitted_ms","expires_ms"})if(value.contains(field))result[field]=value.at(field);return result;
}
inline Json focusView(Json view,const std::string& id,std::size_t limit=8){
    if(!limit){view["threads"]=Json::array();return view;}const auto rows=view.value("threads",Json::array());Json chosen=Json::array();
    for(const auto& row:rows)if(row.value("id",std::string{})==id){chosen.push_back(row);break;}
    for(const auto& row:rows){if(chosen.size()>=limit)break;if(row.value("id",std::string{})!=id)chosen.push_back(row);}
    view["threads"]=chosen;view["omitted_count"]=view.value("matching_count",rows.size())-chosen.size();return view;
}
inline Json planningView(Json view,const Json& offered,std::size_t limit=8){
    auto rows=view.value("threads",Json::array());if(rows.size()<=limit)return view;
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.value("last_noticed_ms",std::int64_t(0))>b.value("last_noticed_ms",std::int64_t(0));});
    Json chosen=Json::array();std::set<std::string> selected;const auto add=[&](const Json& row){const auto id=row.at("id").template get<std::string>();if(chosen.size()<limit&&selected.insert(id).second)chosen.push_back(row);};
    const auto recent=limit>2?limit-2:limit;for(std::size_t i=0;i<recent&&i<rows.size();++i)add(rows[i]);
    std::vector<Json> returning;for(const auto& row:rows)if(!selected.contains(row.at("id").template get<std::string>())&&row.value("kind",std::string{})=="intention"&&row.value("availability",std::string{})=="available")returning.push_back(row);
    const auto last=[&](const Json& row){const auto value=offered.value(row.at("id").template get<std::string>(),Json::object());return value.value("revision",std::int64_t(0))==row.value("revision",std::int64_t(0))?value.value("at_ms",std::int64_t(0)):std::int64_t(0);};
    std::stable_sort(returning.begin(),returning.end(),[&](const auto& a,const auto& b){return last(a)<last(b);});for(const auto& row:returning)add(row);for(const auto& row:rows)add(row);
    view["threads"]=chosen;view["omitted_count"]=view.value("matching_count",rows.size())-chosen.size();return view;
}
}
