#include <Hibernation/ContinuityMind.h>
#include <Hibernation/ContextControl.h>
#include <Hibernation/OwnerContinuity.h>
#include <Hibernation/ObservationContext.h>
#include <Hibernation/PersonalEvidence.h>
#include <Hibernation/Application.h>
#include <AYTestFixtures.h>
#include <AYSerializer/JsonSchema.h>
using namespace hibernation;
namespace {
Json candidate(const char* id,double score,bool foreground=false){return {{"id",id},{"kind","synthetic"},{"score",score},{"foreground",foreground}};}
Json queue(){return {{"entries",Json::array({{{"id","not-started"},{"kind","wait"},{"seconds",3},{"expires_ms",1}}})},{"active",{{"id","writing"},{"kind","creative_note"},{"content","synthetic"}}}};}
}
TEST_SUITE(ContinuityMind)
TEST_CASE(configuration_is_opt_in_and_strict){
 CHECK(!mind::Settings::read(Json::object()).enabled);
 for(const Json bad:{Json{{"enabled",1}},Json{{"dwellMs",0}},Json{{"resumeMinutes",61}},Json{{"unknown",true}},Json{{"observationCooldownMs",-1}}}){bool rejected=false;try{mind::Settings::read(bad);}catch(...){rejected=true;}CHECK(rejected);}
}
TEST_CASE(attention_dwell_and_margin_reduce_switches_against_ablation){
 mind::State normal,ablated;mind::Settings s,off;off.attention=false;
 for(int i=0;i<8;++i){const auto candidates=Json::array({candidate("a",i%2?.40:.65),candidate("b",i%2?.65:.40)});normal.select(candidates,s);ablated.select(candidates,off);normal.advance(200,false);ablated.advance(200,false);}
 CHECK(normal.status().at("metrics").at("switches").get<int>()<ablated.status().at("metrics").at("switches").get<int>());
}
TEST_CASE(foreground_preempts_dwell_and_disallows_model_override){
 mind::State state;mind::Settings s;state.select(Json::array({candidate("food",.9)}),s);state.select(Json::array({candidate("food",.9),candidate("input",1.,true)}),s);
 CHECK(state.view().at("focus")=="input");CHECK(!state.attend("food",state.view().at("revision"),s));CHECK(!state.attend("missing",state.view().at("revision"),s));
}
TEST_CASE(attention_uses_current_version_and_does_not_create_candidate){
 mind::State state;mind::Settings s;state.select(Json::array({candidate("a",.4),candidate("b",.5)}),s);const auto revision=state.view().at("revision").get<std::int64_t>();
 CHECK(!state.attend("a",revision-1,s));CHECK(state.attend("a",revision,s));CHECK(state.view().at("focus")=="a");
}
TEST_CASE(pause_and_restart_do_not_add_offline_experiences){
 mind::State state;state.advance(100,false);state.advance(999999,true);const auto before=state.snapshot();mind::State restored;CHECK(restored.load(before));CHECK(restored.snapshot().at("clock_ms")==100);state.advance(999999,false);CHECK(state.snapshot().at("clock_ms")==1100);
}
TEST_CASE(suspension_keeps_not_started_commands_but_never_replays_active_write){
 mind::State state;mind::Settings s;state.suspend(queue(),"new_input",s);const auto r=state.view().at("suspended").at(0);
 CHECK(r.at("commands").size()==1);CHECK(r.at("commands").at(0).at("id")=="not-started");CHECK(r.at("interrupted").at("id")=="writing");CHECK(r.at("requires_new_plan")==true);
 state.suspend({{"entries",Json::array()},{"active",nullptr}},"new_input",s);CHECK(state.view().at("suspended").size()==1);
}
TEST_CASE(resume_is_versioned_consumed_once_and_bounded){
 mind::State state;mind::Settings s;for(int i=0;i<6;++i){auto q=queue();q["entries"][0]["id"]="pending-"+std::to_string(i);state.suspend(q,"qq_input",s);}CHECK(state.view().at("suspended").size()==3);
 const auto r=state.view().at("suspended").back();CHECK(state.resumable(r.at("id"),0).is_null());CHECK(state.settle(r.at("id"),1,"resumed"));CHECK(state.resumable(r.at("id"),1).is_null());CHECK(!state.settle(r.at("id"),1,"resumed"));CHECK(state.status().at("metrics").at("resumed")==1);
}
TEST_CASE(expiry_counts_active_time_and_manual_cancel_is_not_resumable){
 mind::State state;mind::Settings s;s.resumeMinutes=1;state.suspend(queue(),"configuration_changed",s);CHECK(state.view().at("suspended").empty());state.suspend(queue(),"new_input",s);
 for(int i=0;i<61;++i)state.advance(1000,true);CHECK(state.status().at("pending_count")==1);for(int i=0;i<61;++i)state.advance(1000,false);CHECK(state.status().at("pending_count")==0);
}
TEST_CASE(restart_preserves_pending_but_changes_inflight_to_unconfirmed){
 mind::State state;mind::Settings s;state.suspend(queue(),"input_active",s);Json command={{"id","write"},{"kind","creative_note"}};state.queued(command);state.outcome(command,"running");mind::State restored;CHECK(restored.load(state.snapshot()));CHECK(restored.status().at("pending_count")==1);CHECK(restored.view().at("predictions").at(0).at("status")=="restart_unconfirmed");CHECK(restored.status().at("metrics").at("completed")==0);
}
TEST_CASE(late_effect_does_not_rewrite_cancelled_intention_or_double_count){
 mind::State state;Json command={{"id","effect"},{"kind","creative_note"}};state.queued(command);state.outcome(command,"running");state.outcome(command,"input_active");state.outcome(command,"completed",true);state.outcome(command,"completed",true);
 const auto r=state.view().at("predictions").at(0);CHECK(r.at("status")=="input_active");CHECK(r.at("effect_outcome")=="completed");CHECK(state.status().at("metrics").at("late_effects")==1);CHECK(state.status().at("metrics").at("completed")==0);
}
TEST_CASE(completion_and_unknown_require_explicit_outcome){
 mind::State state;Json c={{"id","eat"},{"kind","eat_nearest"}};state.queued(c);CHECK(state.status().at("metrics").at("completed")==0);state.outcome(c,"timeout");state.outcome(c,"timeout");CHECK(state.status().at("metrics").at("unknown")==1);
}
TEST_CASE(observation_cooldown_is_keyed_by_target_version_not_query_count){
 mind::State state;mind::Settings s;CHECK(state.observationDue("apple:v1",s));state.observed("apple:v1");for(int i=0;i<20;++i)CHECK(!state.observationDue("apple:v1",s));CHECK(state.observationDue("apple:v2",s));
 for(int i=0;i<120;++i)state.advance(1000,false);CHECK(state.observationDue("apple:v1",s));s.observations=false;CHECK(!state.observationDue("apple:v2",s));
}
TEST_CASE(malformed_persistence_is_rejected_without_partial_load){
 mind::State state;const auto before=state.snapshot();auto bad=before;bad["version"]=999;CHECK(!state.load(bad));CHECK(state.snapshot()==before);bad=before;bad["suspended"]=Json::array({{{"id","bad"}}});CHECK(!state.load(bad));CHECK(state.snapshot()==before);
}
TEST_CASE(private_tools_and_derived_views_never_become_factual_experience){
 CHECK(continuity::roleTool("mind.observe"));CHECK(continuity::roleTool("activity.resume"));CHECK(readOnlyModelTool("mind.observe"));CHECK(!readOnlyModelTool("mind.interpret"));
 for(const char* tool:{"mind.observe","mind.interpret","mind.attend","activity.resume"})CHECK(!appraisalExperienceRecord(Json{{"kind","tool_observation"},{"tool",tool}}));
 for(const auto& tool:mind::tools())CHECK(tool.at("parameters").at("additionalProperties")==false);
 const auto definitions=mind::tools();const Json args={{"id","hypothesis"},{"expected_revision",0},{"text","maybe"},{"confidence","tentative"},{"state","active"},{"source_ids",Json::array({"actual"})}};CHECK(ayt::serializer::validateJsonDocument(args.dump(),definitions.at(2).at("parameters").dump()));
 auto shared=args;shared["source_ids"]=Json::array({"shared-0123456789abcdef:evt-1234567890123-0123456789abcdef-12345678901234567890"});CHECK(ayt::serializer::validateJsonDocument(shared.dump(),definitions.at(2).at("parameters").dump()));
}
TEST_CASE(qq_native_owner_shares_working_state_and_other_channels_do_not){
 for(const bool owner:{false,true}){
  ayt::test::ScratchDirectory scratch(owner?"mind-owner-channel":"mind-group-channel");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}}};
  auto policy=std::make_shared<ChannelPolicy>();policy->sharedOwner=owner;policy->selectedProfile="mock";policy->identity={{"channel","qq"},{"sender_user_id","20001"}};policy->stage={{"id",Json::parse(readFile(cfg.characterRoot/"stages.json")).at("initial")},{"changed_at",0}};
  policy->sharedMind={{"interpretations",Json::array({{{"id","synthetic"},{"text","PRIVATE_CHANNEL_MIND"}}})}};cfg.channel=policy;
  Application app(cfg);CHECK(app.initialize());CHECK(app.request({"POST","/api/message",R"({"request_id":"channel-mind-turn","text":"合成渠道测试"})"}).status==202);
  CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));
  const auto state=Json::parse(app.request({"GET","/api/state",""}).body);const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id",state.at("state").at("lastTraceId")}}.dump()}).body);
  CHECK((trace.at("mind_context").dump().find("PRIVATE_CHANNEL_MIND")!=std::string::npos)==owner);
 }
}
TEST_CASE(observation_ablation_reduces_redundant_requests_and_retains_new_versions){
 mind::State state;mind::Settings s;int proposed=0,accepted=0;
 for(int i=0;i<20;++i){++proposed;if(state.observationDue("same-target:v1",s)){++accepted;state.observed("same-target:v1");}state.advance(100,false);}
 CHECK(proposed==20&&accepted==1);CHECK(state.observationDue("same-target:v2",s));CHECK(state.status().at("metrics").at("completed")==0);
}
TEST_SUITE_END
#ifdef HA_MIND_TEST_MAIN
int main(int argc,char** argv){return ayt::test::runTests("ContinuityMind",argc,argv);}
#endif
