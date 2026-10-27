#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <set>
#include <Hibernation/PersonalEvidence.h>

namespace hibernation::personal {
using Json=nlohmann::json;
// Interests are application-managed working dispositions, separate from the
// canonical persona, developmental stages, raw archive, and memory summaries.
// A model supplies a qualitative stance and evidence; it cannot assign scores.
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
inline bool hasNewSources(const Json& value,const Json& sources){
    auto known=value.value("evidence_sources",Json::array());if(known.empty())for(const auto& episode:value.value("episodes",Json::array()))for(const auto& ref:episode.at("sources"))known.push_back(ref);
    for(const auto& ref:sources)if(std::find(known.begin(),known.end(),ref)==known.end())return true;return false;
}
inline bool sameMeaning(const Json& value,const Json& proposal){for(const auto* field:{"kind","topic","note","stance"})if(!value.contains(field)||value.at(field)!=proposal.at(field))return false;return true;}
inline Json fallbackAnchors(const Json& sources){Json values=Json::array();for(const auto& ref:sources)values.push_back(anchor("source",ref.at("scope").get<std::string>()+":"+ref.at("id").get<std::string>(),1));return values;}
inline void mergeAnchors(Json& ledger,const Json& anchors,const std::string& stance){
    for(const auto& item:anchors){if(item.is_null())continue;auto found=std::find_if(ledger.begin(),ledger.end(),[&](const auto& old){return old.at("key")==item.at("key")||(old.value("type",std::string{})=="conversation"&&item.value("type",std::string{})=="conversation"&&std::abs(old.value("observed_ms",0.0)-item.value("observed_ms",0.0))<900000);});
        auto value=item;value["stance"]=stance;if(found==ledger.end()){if(ledger.size()<128)ledger.push_back(std::move(value));}else{value["weight"]=std::max(found->value("weight",0.0),item.value("weight",0.0));if(value.value("type",std::string{})=="conversation"){value["key"]=found->at("key");value["observed_ms"]=found->at("observed_ms");}*found=std::move(value);}
    }
}
inline Json notice(Json before,const Json& proposal,const Json& sources,std::int64_t now,const Json& resolvedAnchors=Json()){
    const bool fresh=before.is_null()||before.empty();auto result=fresh?Json{{"version",2},{"created_ms",now},{"updated_ms",now},{"leaning",0.0},{"episodes",Json::array()},{"status","active"}}:projected(std::move(before),now);
    const auto stance=proposal.at("stance").get<std::string>();
    const bool newSources=fresh||hasNewSources(result,sources),newMeaning=fresh||!sameMeaning(result,proposal);
    if(!newSources&&!newMeaning)return result;
    if(!result.contains("seen_evidence")){result["seen_evidence"]=Json::array();for(const auto& episode:result.at("episodes"))result["seen_evidence"].push_back(evidenceKey(episode.at("sources")));}
    if(!seenEvidence(result,sources))result["seen_evidence"].push_back(evidenceKey(sources));
    if(!result.contains("evidence_sources")){result["evidence_sources"]=Json::array();for(const auto& episode:result.at("episodes"))for(const auto& ref:episode.at("sources"))if(std::find(result["evidence_sources"].begin(),result["evidence_sources"].end(),ref)==result["evidence_sources"].end())result["evidence_sources"].push_back(ref);}
    for(const auto& ref:sources)if(std::find(result["evidence_sources"].begin(),result["evidence_sources"].end(),ref)==result["evidence_sources"].end())result["evidence_sources"].push_back(ref);
    Json ledger=result.value("experience_evidence",Json::array());
    if(!result.contains("experience_evidence"))for(const auto& episode:result.at("episodes"))mergeAnchors(ledger,episode.value("experience_anchors",fallbackAnchors(episode.at("sources"))),episode.at("stance"));
    const auto anchors=resolvedAnchors.is_null()?fallbackAnchors(sources):resolvedAnchors;
    const auto independentBefore=ledger.size();mergeAnchors(ledger,anchors,stance);
    double balance=0,weight=0;int positive=0,negative=0;for(const auto& item:ledger){const auto opinion=item.at("stance").get<std::string>();if(opinion=="like"){balance+=item.value("weight",0.0);weight+=item.value("weight",0.0);++positive;}else if(opinion=="avoid"){balance-=item.value("weight",0.0);weight+=item.value("weight",0.0);++negative;}}
    result["version"]=2;result["experience_evidence"]=ledger;
    for(const auto* field:{"kind","topic","note","stance"})result[field]=proposal.at(field);
    result["target_leaning"]=weight>0?std::min(.6,weight*.12)*balance/weight:0;result["last_noticed_ms"]=now;
    if(newMeaning||ledger.size()!=independentBefore||fresh)result["episodes"].push_back({{"sources",sources},{"note",proposal.at("note")},{"stance",stance},{"at_ms",now},{"experience_anchors",anchors}});
    // The database retains every prior revision. This is a bounded working view.
    if(result["episodes"].size()>12)result["episodes"].erase(result["episodes"].begin());
    result["evidence_count"]=result.value("evidence_count",0)+(newSources?1:0);result["change_count"]=result.value("change_count",0)+1;
    result["familiarity"]=ledger.size()>=4?"recurring":"tentative";
    result["basis"]={{"independent_experiences",ledger.size()},{"positive",positive},{"negative",negative},{"mixed",positive>0&&negative>0}};
    return result;
}
inline Json context(const Json& record,std::int64_t revision){
    Json result;for(const auto* field:{"kind","topic","note","stance","status","leaning","familiarity","created_ms","last_noticed_ms","basis"})if(record.contains(field))result[field]=record.at(field);
    // Program-resolved provenance identities, never inferred from model prose.
    result["experience_contexts"]=Json::array();std::set<std::string> seen;
    const auto ledger=record.value("experience_evidence",Json::array());
    for(auto it=ledger.rbegin();it!=ledger.rend()&&result["experience_contexts"].size()<6;++it){
        const auto type=it->value("type",std::string{}),key=it->value("key",std::string{}),prefix=type+":";
        if(type!="puzzle"&&type!="authored_piece"&&type!="desk_object"&&type!="reading"&&type!="draft")continue;
        if(!key.starts_with(prefix)||key.size()<=prefix.size()||key.size()>2048||!seen.insert(key).second)continue;
        result["experience_contexts"].push_back({{"kind",type},{"id",key.substr(prefix.size())}});
    }
    result["recent_changes"]=Json::array();const auto episodes=record.value("episodes",Json::array());const auto begin=episodes.size()>3?episodes.size()-3:0;
    for(std::size_t i=begin;i<episodes.size();++i)result["recent_changes"].push_back({{"note",episodes[i].at("note")},{"stance",episodes[i].at("stance")},{"at_ms",episodes[i].at("at_ms")}});
    result["revision"]=revision;return result;
}
inline bool pursuedBy(const Json& thread,const Json& queue){
    const auto matches=[&](const Json& step){if(!step.is_object())return false;const auto link=step.value("intention",Json::object());return link.is_object()&&link.value("id",std::string{})==thread.value("id",std::string{})&&link.contains("revision")&&thread.contains("revision")&&link.at("revision")==thread.at("revision");};
    if(matches(queue.value("active",Json(nullptr))))return true;
    for(const auto* field:{"entries","next"})for(const auto& step:queue.value(field,Json::array()))if(matches(step))return true;
    return false; // Completed/recent attempts are history, not current pursuit.
}
inline void backgroundIndex(Json& thread){
    for(const auto* field:{"note","recent_changes","source_ids","created_ms","last_noticed_ms","attempts","basis"})thread.erase(field);
    thread["context_detail"]="background_index";thread["details_tool"]="self.observe";
}
}
