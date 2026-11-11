#include <Hibernation/CognitiveAudit.h>
#include <Hibernation/Application.h>
#include <AYTestFixtures.h>
using hibernation::cognitive_audit::Json;
using namespace hibernation::cognitive_audit;
namespace {Json emptyQueue(){return {{"entries",Json::array()},{"active",nullptr},{"recent",Json::array()}};}Json command(){return {{"id","action-1"},{"kind","wait"},{"seconds",2},{"purpose","PRIVATE_PURPOSE"},{"source","trace-1"}};}}
TEST_SUITE(CognitiveAudit)
TEST_CASE(sleep_receipt_codes_and_session_closure_do_not_promote_unknown_private_fields){
 CHECK(sleepReceiptReason({{"reason","window_ended"}})=="window_ended");CHECK(sleepReceiptReason({{"reason","PRIVATE_REASON_TEXT"}})=="plugin_receipt_committed");CHECK(sleepReceiptReason({{"reason",5}})=="plugin_receipt_committed");
 CHECK(previousClosure({{"version",1},{"clean_shutdown",true}})==true);CHECK(previousClosure({{"version",1},{"clean_shutdown",false}})==false);CHECK(previousClosure({{"version",2},{"clean_shutdown",true}}).is_null());CHECK(previousClosure({{"version",1},{"clean_shutdown","true"}}).is_null());
}
TEST_CASE(acceptance_start_and_completion_have_distinct_durable_transitions){
 auto old=emptyQueue(),queued=old;queued["entries"].push_back(command());auto rows=activityTransitions(old,queued,1,"enqueue");CHECK(rows.size()==1&&rows[0]["kind"]=="cognition.activity.queued");CHECK(rows[0]["status"]=="accepted_not_completed");
 auto started=queued;started["active"]=command();started["entries"]=Json::array();rows=activityTransitions(queued,started,2,"start");CHECK(rows.size()==1&&rows[0]["kind"]=="cognition.activity.started");
 auto done=emptyQueue();auto result=command();result["status"]="completed";done["recent"].push_back(result);rows=activityTransitions(started,done,3,"actual_receipt");CHECK(rows.size()==1&&rows[0]["kind"]=="cognition.activity.result"&&rows[0]["status"]=="completed");CHECK(activityTransitions(done,done,4,"tick").empty());
}
TEST_CASE(late_effect_keeps_cancelled_intent_separate){
 auto old=emptyQueue();auto result=command();result["status"]="new_input";old["recent"].push_back(result);auto after=old;after["recent"][0]["effect_outcome"]="completed";
 const auto rows=activityTransitions(old,after,1,"late_local_effect");CHECK(rows.size()==1&&rows[0]["kind"]=="cognition.activity.effect"&&rows[0]["late_effect"]==true);CHECK(rows[0]["private"]["status"]=="new_input");
}
TEST_CASE(metadata_never_contains_private_payload_or_invents_reasoning){
 const Json row={{"id","trace"},{"kind","cognition.interpretation.changed"},{"private",{{"text","PRIVATE_CONTENT"},{"source_ids",Json::array({"raw-source"})}}},{"api_key","SECRET"},{"reasoning","DO_NOT_STORE"},{"status","tentative"}};
 const auto view=metadata(row).dump();CHECK(view.find("PRIVATE_CONTENT")==std::string::npos);CHECK(view.find("SECRET")==std::string::npos);CHECK(view.find("DO_NOT_STORE")==std::string::npos);CHECK(internalKind("cognition.attention.changed"));
}
TEST_CASE(removed_unstarted_command_is_not_completed){auto before=emptyQueue();before["entries"].push_back(command());const auto rows=activityTransitions(before,emptyQueue(),2,"new_input");CHECK(rows.size()==1&&rows[0]["kind"]=="cognition.activity.removed");CHECK(rows[0]["status"]!="completed");}
TEST_CASE(public_trace_rejects_invalid_pages_without_disclosing_or_damaging_state){
 using namespace hibernation;ayt::test::ScratchDirectory scratch("cognitive-trace-api");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}}};cfg.cognition={{"enabled",true}};Application app(cfg);CHECK(app.initialize());app.tick();
 for(const Json bad:{Json{{"limit",0}},Json{{"limit",101}},Json{{"limit",1.5}},Json{{"limit",true}},Json{{"before_id",8}},Json{{"include_private",true}},Json::array()})CHECK(app.request({"POST","/api/mind/trace",bad.dump()}).status==400);
 const auto page=Json::parse(app.request({"POST","/api/mind/trace",Json{{"limit",1}}.dump()}).body);CHECK(page.at("private_content_included")==false);CHECK(page.at("events").size()<=1);CHECK(Json::parse(app.request({"GET","/api/dashboard",""}).body).at("health")=="ok");
 const auto trace=Json::parse(app.request({"GET","/api/mind/trace",""}).body),events=Json::parse(app.request({"POST","/api/events",Json{{"channel","agent.cognition"}}.dump()}).body);int linked=0;
 for(const auto& row:trace.at("events"))if(!row.value("correlation",std::string{}).empty()){const auto found=std::find_if(events.at("events").begin(),events.at("events").end(),[&](const auto& event){return event.at("event_id")==row.at("id");});CHECK(found!=events.at("events").end());if(found!=events.at("events").end())CHECK(found->at("correlation")==row.at("correlation"));++linked;}CHECK(linked>0);
}
TEST_SUITE_END
