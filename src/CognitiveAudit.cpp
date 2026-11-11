#include <Hibernation/CognitiveAudit.h>
#include <map>
#include <set>
namespace hibernation::cognitive_audit {
bool internalKind(const std::string& kind){return kind.starts_with("cognition.")||kind.starts_with("audit.");}
std::string sleepReceiptReason(const Json& event){
 const auto found=event.find("reason");if(found==event.end()||!found->is_string())return "plugin_receipt_committed";
 const std::set<std::string> codes={"pose_confirmed","window_ended","clock_window_changed","duration_reached","character_unavailable","application_restarted","disabled","schedule_changed","chosen_wake","administrator_disabled","plugin_disabled","pointer_interaction","message","intent_expired","pose_unavailable","window_selected","input_started","new_conversation","attention_changed","activity_cancelled"};
 const auto reason=found->get<std::string>();return codes.contains(reason)?reason:"plugin_receipt_committed";
}
Json previousClosure(const Json& session){
 if(!session.is_object()||!session.contains("version")||!session.at("version").is_number_integer()||session.at("version")!=1||!session.contains("clean_shutdown")||!session.at("clean_shutdown").is_boolean())return nullptr;
 return session.at("clean_shutdown");
}
Json metadata(const Json& row){
 Json out=Json::object();for(const auto* field:{"id","kind","created_at","correlation","status","reason","source_id","action_id","action_kind","trace_id","request_id","revision","before_revision","late_effect","token","queued_count","candidate_count","private_details_omitted"})if(row.contains(field)&&row.at(field).is_primitive())out[field]=row.at(field);
 return out;
}
Json activityTransitions(const Json& before,const Json& after,std::int64_t now,const std::string& reason){
 Json events=Json::array();std::map<std::string,Json> old,newer,recent;
 auto collect=[](const Json& q,std::map<std::string,Json>& target){for(const auto& c:q.value("entries",Json::array()))target[c.at("id")]=c;const auto a=q.value("active",Json(nullptr));if(a.is_object())target[a.at("id")]=a;};
 collect(before,old);collect(after,newer);for(const auto& r:before.value("recent",Json::array()))recent[r.at("id")]=r;
 auto emit=[&](const char* kind,const Json& command,const std::string& status,bool late=false){Json e={{"kind",kind},{"created_at",now},{"status",status},{"reason",reason},{"action_id",command.at("id")},{"action_kind",command.at("kind")},{"late_effect",late},{"private",command}};for(const auto* key:{"token","source"})if(command.contains(key))e[key==std::string("source")?"correlation":"token"]=command.at(key);events.push_back(std::move(e));};
 for(const auto& c:after.value("entries",Json::array()))if(!old.contains(c.at("id")))emit("cognition.activity.queued",c,"accepted_not_completed");
 const auto active=after.value("active",Json(nullptr));if(active.is_object()&&(!before.value("active",Json(nullptr)).is_object()||before.at("active").at("id")!=active.at("id")))emit("cognition.activity.started",active,"started");
 std::set<std::string> ended;for(const auto& r:after.value("recent",Json::array())){
  const std::string id=r.at("id");ended.insert(id);
  if(!recent.contains(id))emit("cognition.activity.result",r,r.value("status",std::string("unknown")));
  if(r.contains("effect_outcome")&&(!recent.contains(id)||recent.at(id).value("effect_outcome",Json(nullptr))!=r.at("effect_outcome")))emit("cognition.activity.effect",r,r.at("effect_outcome"),true);
 }
 for(const auto& [id,c]:old)if(!newer.contains(id)&&!ended.contains(id))emit("cognition.activity.removed",c,"not_started_or_result_not_retained");
 return events;
}
}
