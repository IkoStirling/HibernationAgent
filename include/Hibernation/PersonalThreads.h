#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace hibernation::personal {
using Json=nlohmann::json;
// Interests are application-managed working dispositions, separate from the
// canonical persona, developmental stages, raw archive, and memory summaries.
// A model supplies a qualitative stance and evidence; it cannot assign scores.
inline double stanceTarget(const std::string& stance){return stance=="like"?.6:stance=="avoid"?-.6:0;}
inline Json projected(Json value,std::int64_t now){
    const auto previous=value.value("updated_ms",now);const double elapsed=std::clamp((now-previous)/1000.0,0.0,120.0);
    // Active time only: restoring an archive after a long absence must not
    // manufacture years of preference consolidation or alter old experiences.
    const double old=value.value("leaning",0.0),target=value.value("target_leaning",0.0);
    value["leaning"]=old+std::clamp(target-old,-elapsed*.0005,elapsed*.0005);value["updated_ms"]=now;
    return value;
}
inline std::string evidenceKey(const Json& values){Json result=Json::array();for(const auto& source:values)result.push_back(Json::array({source.at("scope"),source.at("id"),source.at("revision")}));std::sort(result.begin(),result.end());return result.dump();}
inline bool sameEvidence(const Json& a,const Json& b){return a.size()==b.size()&&evidenceKey(a)==evidenceKey(b);}
inline bool seenEvidence(const Json& value,const Json& sources){
    const auto key=evidenceKey(sources);if(value.contains("seen_evidence"))for(const auto& seen:value.at("seen_evidence"))if(seen==key)return true;
    for(const auto& episode:value.value("episodes",Json::array()))if(sameEvidence(episode.at("sources"),sources))return true;
    return false;
}
inline Json notice(Json before,const Json& proposal,const Json& sources,std::int64_t now){
    const bool fresh=before.is_null()||before.empty();auto result=fresh?Json{{"version",1},{"created_ms",now},{"updated_ms",now},{"leaning",0.0},{"episodes",Json::array()},{"status","active"}}:projected(std::move(before),now);
    const auto stance=proposal.at("stance").get<std::string>();
    if(seenEvidence(result,sources))return result;
    if(!result.contains("seen_evidence")){result["seen_evidence"]=Json::array();for(const auto& episode:result.at("episodes"))result["seen_evidence"].push_back(evidenceKey(episode.at("sources")));}
    result["seen_evidence"].push_back(evidenceKey(sources));
    if(!result.contains("evidence_sources")){result["evidence_sources"]=Json::array();for(const auto& episode:result.at("episodes"))for(const auto& ref:episode.at("sources"))if(std::find(result["evidence_sources"].begin(),result["evidence_sources"].end(),ref)==result["evidence_sources"].end())result["evidence_sources"].push_back(ref);}
    for(const auto& ref:sources)if(std::find(result["evidence_sources"].begin(),result["evidence_sources"].end(),ref)==result["evidence_sources"].end())result["evidence_sources"].push_back(ref);
    for(const auto* field:{"kind","topic","note","stance"})result[field]=proposal.at(field);
    result["target_leaning"]=stanceTarget(stance);result["last_noticed_ms"]=now;
    result["episodes"].push_back({{"sources",sources},{"note",proposal.at("note")},{"stance",stance},{"at_ms",now}});
    // The database retains every prior revision. This is a bounded working view.
    if(result["episodes"].size()>12)result["episodes"].erase(result["episodes"].begin());
    result["evidence_count"]=std::min(1000,result.value("evidence_count",0)+1);
    result["familiarity"]=result.at("evidence_count").get<int>()>=4?"recurring":"tentative";
    return result;
}
inline Json context(const Json& record,std::int64_t revision){
    Json result;for(const auto* field:{"kind","topic","note","stance","status","leaning","familiarity","created_ms","last_noticed_ms"})if(record.contains(field))result[field]=record.at(field);
    result["revision"]=revision;return result;
}
}
