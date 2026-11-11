#include <Hibernation/CircadianRhythm.h>
#include <Hibernation/Application.h>
#include <Hibernation/ActivityPlan.h>
#include <Hibernation/CircadianProtocol.h>
#include <Hibernation/PluginRuntime.h>
#include <AYTestFixtures.h>
using hibernation::CircadianRhythm;
namespace {
using J=nlohmann::json;
J preference(double drag=.2,double message=.65){return {{"enabled",true},{"start_minute",1380},{"end_minute",420},{"drag_wake_probability",drag},{"message_wake_probability",message},{"inertia_seconds",600}};}
void seconds(CircadianRhythm& r,double n,bool advance=true){for(int i=0;i<int(n*4);++i)r.tick(.25,advance,true,false,"idle");}
bool start(CircadianRhythm& r,int minute=1381){seconds(r,6);if(!r.request("sleep-test",minute,"20261029","20261028"))return false;r.tick(.25,true,true,false,"idle");return r.acknowledge("sleep-test");}
}
TEST_SUITE(CircadianRhythm)
TEST_CASE(reaction_cache_uses_coarse_rest_bands_without_clock_churn){
 using hibernation::circadianReactionBand;
 CHECK(std::string(circadianReactionBand(J::object()))=="settled");
 CHECK(std::string(circadianReactionBand({{"phase","sleeping"},{"sleep_inertia",0}}))=="sleeping");
 for(double inertia:{.12,.2,.5,.8})CHECK(std::string(circadianReactionBand({{"phase","idle"},{"sleep_inertia",inertia},{"elapsed_seconds",inertia*100}}))=="waking");
 CHECK(std::string(circadianReactionBand({{"phase","idle"},{"sleep_inertia",.119}}))=="settled");
 CHECK(std::string(circadianReactionBand({{"sleep_inertia",nullptr}}))=="settled");
}
TEST_CASE(unconfigured_routine_never_creates_a_sleep_or_forced_persona){CircadianRhythm r;seconds(r,900);CHECK(!r.mayStart(1390,"today","yesterday"));CHECK(r.takeEvents().empty());CHECK(r.inertia()==0);}
TEST_CASE(schedule_is_versioned_bounded_and_strict){CircadianRhythm r;CHECK(r.configure(preference(),0));CHECK(!r.configure(preference(),0));for(auto v:{J{{"enabled",true},{"start_minute",1},{"end_minute",1}},J{{"enabled",true},{"start_minute",0},{"end_minute",800}},J{{"enabled",true},{"start_minute",1380},{"end_minute",420},{"inertia_seconds",0}}}){bool rejected=false;try{r.configure(v,1);}catch(...){rejected=true;}CHECK(rejected);}CHECK(r.observe()["preferences_revision"]==1);}
TEST_CASE(overnight_window_is_one_occurrence_across_midnight){CircadianRhythm r;r.configure(preference(1,1),0);CHECK(start(r));CHECK(r.wake("administrator",0,true));seconds(r,301);CHECK(!r.mayStart(30,"20261030","20261029"));CHECK(r.mayStart(1381,"20261030","20261029"));CHECK(r.remainingMinutes(420)==0);}
TEST_CASE(calendar_reconciliation_ends_old_windows_without_inventing_offline_rest){
 CircadianRhythm r;r.configure(preference(),0);CHECK(start(r));seconds(r,20);const auto total=r.observe()["active_rest_seconds"],inertia=r.observe()["sleep_inertia"];r.takeEvents();
 r.reconcileWindow(30,"20261030","20261029");CHECK(r.sleeping());
 r.reconcileWindow(420,"20261030","20261029");CHECK(!r.pending());CHECK(r.observe()["active_rest_seconds"]==total);CHECK(r.observe()["sleep_inertia"]==inertia);
 bool ended=false;for(const auto& event:r.takeEvents())if(event["kind"]=="rhythm.sleep_ended"){CHECK(event["reason"]=="window_ended");CHECK(event["elapsed_seconds"]==20);ended=true;}CHECK(ended);
 CircadianRhythm jumped;jumped.configure(preference(),0);CHECK(start(jumped));jumped.takeEvents();jumped.reconcileWindow(1381,"20261030","20261029");CHECK(!jumped.pending());bool changed=false;for(const auto& event:jumped.takeEvents())if(event["reason"]=="clock_window_changed")changed=true;CHECK(changed);
}
TEST_CASE(acceptance_is_not_sleep_until_matching_pose_receipt){CircadianRhythm r;r.configure(preference(),0);seconds(r,6);CHECK(r.request("x",1381,"a","b"));CHECK(!r.sleeping());CHECK(!r.acknowledge("x"));r.tick(.25,true,true,false,"idle");CHECK(!r.acknowledge("wrong"));CHECK(r.acknowledge("x"));CHECK(r.sleeping());}
TEST_CASE(failed_wake_consumes_no_model_and_preserves_sleep_with_cooldown){CircadianRhythm r;r.configure(preference(),0);CHECK(start(r));CHECK(!r.wake("pointer_interaction",.9));CHECK(r.sleeping());CHECK(!r.wake("pointer_interaction",0));seconds(r,11);CHECK(r.wake("pointer_interaction",.1));CHECK(!r.sleeping());CHECK(r.inertia()>.29);}
TEST_CASE(administrator_override_always_wakes_despite_zero_probability){CircadianRhythm r;r.configure(preference(0,0),0);CHECK(start(r));CHECK(!r.wake("message",.1));CHECK(r.wake("administrator",.9,true));CHECK(!r.pending());}
TEST_CASE(inertia_changes_continuously_and_pause_freezes_active_time){CircadianRhythm r;r.configure(preference(1),0);CHECK(start(r));seconds(r,120);r.wake("pointer_interaction",0);const double initial=r.inertia();seconds(r,120,false);CHECK(r.inertia()==initial);seconds(r,60);CHECK(r.inertia()<initial&&r.inertia()>initial*.8);}
TEST_CASE(restart_records_interruption_without_fabricating_offline_rest){CircadianRhythm r;r.configure(preference(),0);CHECK(start(r));seconds(r,12);const auto saved=r.snapshot();CircadianRhythm restored;CHECK(restored.restore(saved));CHECK(!restored.pending());CHECK(restored.observe()["active_rest_seconds"]==saved["active_rest_seconds"]);bool interrupted=false;for(const auto& e:restored.takeEvents())if(e.value("reason","")=="application_restarted"){interrupted=true;CHECK(e["elapsed_seconds"]==saved["elapsed_seconds"]);}CHECK(interrupted);}
TEST_CASE(pose_timeout_is_not_a_completed_rest){CircadianRhythm r;r.configure(preference(),0);seconds(r,6);r.request("x",1381,"a","b");seconds(r,3);CHECK(!r.pending());CHECK(r.observe()["active_rest_seconds"]==0);bool cancelled=false;for(const auto& e:r.takeEvents())if(e["kind"]=="rhythm.sleep_cancelled")cancelled=true;CHECK(cancelled);}
TEST_CASE(background_routine_proposal_is_optional_strict_and_cannot_set_current_inertia){
 using namespace hibernation;J batch={{"version",1},{"commands",J::array()}};CHECK(validateActivities(batch.dump()).ok);
 batch["circadian_routine"]={{"expected_revision",0},{"preferences",preference()}};CHECK(validateActivities(batch.dump()).ok);
 batch["circadian_routine"]["sleep_inertia"]=1;CHECK(!validateActivities(batch.dump()).ok);batch["circadian_routine"].erase("sleep_inertia");batch["circadian_routine"]["preferences"]["end_minute"]=1380;CHECK(!validateActivities(batch.dump()).ok);
}
TEST_CASE(wake_keeps_readiness_continuous_and_restart_can_reenter_the_same_window){
 CircadianRhythm r;r.configure(preference(1),0);CHECK(start(r));double previous=r.inertia();for(int i=0;i<80;++i){r.tick(.25,true,true,false,"resting");CHECK(std::abs(r.inertia()-previous)<=.007501);previous=r.inertia();}const auto saved=r.snapshot();r.wake("pointer_interaction",0);CHECK(r.inertia()==previous);
 CircadianRhythm resumed;CHECK(resumed.restore(saved));seconds(resumed,6);CHECK(resumed.mayStart(1382,"20261029","20261028"));CHECK(resumed.observe()["active_rest_seconds"]==saved["active_rest_seconds"]);
 CircadianRhythm ended;CHECK(ended.restore(r.snapshot()));seconds(ended,301);CHECK(!ended.mayStart(1382,"20261029","20261028"));
}
TEST_CASE(ordinary_wake_may_return_to_sleep_after_a_real_active_time_buffer){
 CircadianRhythm ordinary;ordinary.configure(preference(1),0);CHECK(start(ordinary));seconds(ordinary,20);CHECK(ordinary.wake("pointer_interaction",0));seconds(ordinary,599);CHECK(!ordinary.mayStart(1390,"20261029","20261028"));seconds(ordinary,2);CHECK(ordinary.mayStart(1390,"20261029","20261028"));
 CircadianRhythm forced;forced.configure(preference(1),0);CHECK(start(forced));forced.wake("administrator",0,true);seconds(forced,1200);CHECK(!forced.mayStart(1400,"20261029","20261028"));
}
TEST_CASE(repeated_identical_configuration_keeps_the_current_sleep_and_revision){
 CircadianRhythm r;r.configure(preference(),0);CHECK(start(r));seconds(r,20);const auto before=r.snapshot();r.takeEvents();CHECK(r.configure(preference(),1));CHECK(r.sleeping());CHECK(r.observe()["preferences_revision"]==1);CHECK(r.snapshot()["token"]==before["token"]);CHECK(r.snapshot()["elapsed_seconds"]==before["elapsed_seconds"]);CHECK(r.takeEvents().empty());
}
TEST_CASE(changing_a_window_or_reenabling_it_creates_a_new_explicit_opportunity){
 CircadianRhythm r;r.configure(preference(1,1),0);CHECK(start(r));CHECK(r.wake("administrator",0,true));seconds(r,301);CHECK(!r.mayStart(1400,"20261029","20261028"));
 auto changed=preference(1,1);changed["message_wake_probability"]=.7;CHECK(r.configure(changed,1));seconds(r,6);CHECK(!r.mayStart(1400,"20261029","20261028"));
 changed["start_minute"]=1370;CHECK(r.configure(changed,2));seconds(r,6);CHECK(r.mayStart(1400,"20261029","20261028"));
 CHECK(r.request("changed-window",1400,"20261029","20261028"));r.tick(.25,true,true,false,"idle");CHECK(r.acknowledge("changed-window"));r.wake("administrator",0,true);
 changed["enabled"]=false;CHECK(r.configure(changed,3));changed["enabled"]=true;CHECK(r.configure(changed,4));seconds(r,6);CHECK(r.mayStart(1400,"20261029","20261028"));
}
#ifdef HA_TEST_PLUGIN_ROOT
TEST_CASE(sleeping_widget_does_not_show_receiving_cues_until_released){
 using namespace hibernation;ayt::test::ScratchDirectory scratch("circadian-widget-attention");PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{scratch.path(),HA_PROJECT_ROOT,false,{}});std::string issue;CHECK(runtime.configure({{"widget",{{"enabled",true}}},{"rhythm",{{"enabled",true},{"automaticRest",false}}}},issue));
 runtime.event({{"kind","rhythm.pose.request"},{"plugin","rhythm"},{"token","sleep-pose-fixture"},{"seconds",3600},{"circadian",true}});runtime.event({{"kind","runtime.attention"},{"busy",true},{"input_active",true},{"epoch",1}});runtime.event({{"kind","conversation.received"}});auto widget=runtime.liveSnapshot("widget");CHECK(widget.at("state")=="resting");CHECK(widget.at("dialogue_phase")=="idle");
 runtime.event({{"kind","rhythm.pose.release"},{"plugin","rhythm"},{"token","sleep-pose-fixture"}});runtime.event({{"kind","runtime.attention"},{"busy",true},{"input_active",true},{"epoch",2}});widget=runtime.liveSnapshot("widget");CHECK(widget.at("dialogue_phase")=="receiving");CHECK(widget.at("state")=="listening");
}
#endif
TEST_CASE(qq_sleep_gate_retains_input_and_cannot_be_overridden_by_message_json){
 using namespace hibernation;ayt::test::ScratchDirectory scratch("circadian-channel");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}}};
 auto policy=std::make_shared<ChannelPolicy>();policy->circadianHold=true;policy->sharedOwner=true;policy->selectedProfile="mock";policy->identity={{"channel","qq"},{"sender_user_id","20001"}};policy->stage={{"id",Json::parse(readFile(cfg.characterRoot/"stages.json")).at("initial")},{"changed_at",0}};policy->body={{"rhythm",{{"circadian",{{"phase","sleeping"}}}}}};cfg.channel=policy;
 Application app(cfg);CHECK(app.initialize());const Json input={{"request_id","sleep-channel-turn"},{"text","SYNTHETIC_RETAINED_SLEEP_INPUT"},{"circadian_held",false}};CHECK(app.request({"POST","/api/message",input.dump()}).status==202);
 CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));
 const auto request=Json::parse(app.request({"POST","/api/message",input.dump()}).body).at("request");CHECK(request.at("status")=="completed");CHECK(request.at("input").at("circadian_held")==true);CHECK(request.at("input").at("text")=="SYNTHETIC_RETAINED_SLEEP_INPUT");
 const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id",request.at("trace_id")}}.dump()}).body);CHECK(trace.at("model_called")==false);CHECK(trace.at("silent")==true);
 const auto result=Json::parse(app.request({"POST","/api/channel/result",Json{{"request_id","sleep-channel-turn"}}.dump()}).body);CHECK(result.at("status")=="completed"&&result.at("text")=="");
}
TEST_SUITE_END
