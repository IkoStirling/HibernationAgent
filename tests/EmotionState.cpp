#include <Hibernation/EmotionState.h>
#include <Hibernation/RoleState.h>
#include <Hibernation/ActivityPlan.h>
#include <AYTestFixtures.h>
#include <cmath>
#include <limits>
using namespace hibernation;
using Json=nlohmann::json;
namespace {
void advance(EmotionState& state,double seconds,const EmotionEnvironment& environment={}){for(int i=0;i<static_cast<int>(seconds*4);++i)state.tick(.25,environment);}
bool rejectsRestore(EmotionState& state,const Json& value){try{state.restore(value);return false;}catch(...){return true;}}
}
TEST_SUITE(EmotionState)
TEST_CASE(legacy_versions_keep_all_existing_values_and_start_without_invented_emotions){
    for(int version:{1,2,3}){RoleState state;Json legacy={{"version",version},{"energy",.31},{"mood",.43},{"openness",.42},{"attention",.17},{"willingness",.29},{"social_load",.4},{"activity","resting"},{"desired_activity","reflecting"},{"updated_ms",10000},{"entered_ms",5000}};state.restore(legacy,86410000);auto snapshot=state.snapshot();for(const auto* field:{"energy","mood","openness","attention","willingness","social_load","activity","desired_activity"})CHECK(snapshot.at(field)==legacy.at(field));CHECK(snapshot.at("version")==4);for(const auto& emotion:snapshot.at("affect").at("emotions"))CHECK(emotion==0);CHECK(snapshot.at("affect").at("sources").empty());}
}
TEST_CASE(stimulus_changes_only_private_drive_and_never_sets_current_values_or_body){
    RoleState state;state.restore(Json::object(),1000);const auto before=state.snapshot();std::string issue;
    CHECK(state.stimulate({{"emotions",{{"anger",1.0}}},{"desires",{{"company",-.5},{"express",.5}}}},"actual-boundary-event",1000,issue));const auto after=state.snapshot();
    for(const auto* field:{"mood","willingness","energy","activity","desired_activity","target_valence","appraisal_source_id"})CHECK(after.at(field)==before.at(field));CHECK(after.at("affect").at("emotions")==before.at("affect").at("emotions"));CHECK(after.at("affect").at("desires")==before.at("affect").at("desires"));CHECK(after.at("affect").at("emotion_drive").at("anger")>0);
    const auto visible=state.emotionalContext();for(const auto* field:{"emotion_drive","desire_drive","sources","clock_seconds","last_stimulus_seconds"})CHECK(!visible.contains(field));CHECK(visible.at("behavior").at("may_decline")==true);CHECK(visible.at("behavior").at("may_remain_silent")==true);CHECK(!visible.at("behavior").at("may_use_dry_irony").get<bool>());
}
TEST_CASE(invalid_or_direct_state_assignment_proposals_are_atomically_rejected){
    EmotionState state;const auto before=state.snapshot();std::string issue;
    const std::vector<Json> invalid={Json::array(),Json::object(),{{"emotions",{{"anger",2}}}},{{"emotions",{{"anger",true}}}},{{"emotions",{{"anger",0}}}},{{"target",{{"anger",1}}}},{{"desires",{{"dependency",1}}}},{{"emotions",{{"joy",1},{"anger",1},{"sadness",1},{"worry",1}}}},{{"emotions",{{"anger",1},{"joy",1}}},{"desires",{{"rest",1},{"express",1}}}},{{"emotions",{{"anger",std::numeric_limits<double>::infinity()}}}},{{"emotions",{{"anger",std::numeric_limits<double>::quiet_NaN()}}}}};
    for(const auto& proposal:invalid){CHECK_FALSE(state.stimulate(proposal,"source",1000,issue));CHECK(issue=="invalid_emotion_stimulus");CHECK(state.snapshot()==before);}
    CHECK_FALSE(state.stimulate({{"emotions",{{"joy",.5}}}},"",1000,issue));CHECK_FALSE(state.stimulate({{"emotions",{{"joy",.5}}}},std::string(1,static_cast<char>(0xff)),1000,issue));CHECK_FALSE(state.stimulate({{"emotions",{{"joy",.5}}}},"source",-1,issue));CHECK(state.snapshot()==before);
}
TEST_CASE(source_dedup_and_cooldown_survive_restart_and_do_not_use_offline_time){
    EmotionState state;std::string issue;CHECK(state.stimulate({{"emotions",{{"joy",.8}}}},"source-a",1000,issue));const auto first=state.snapshot();CHECK_FALSE(state.stimulate({{"emotions",{{"anger",.9}}}},"source-a",999999999,issue));CHECK(issue=="emotion_source_already_used");CHECK(state.snapshot()==first);
    CHECK_FALSE(state.stimulate({{"emotions",{{"joy",.8}}}},"source-b",999999999,issue));CHECK(issue=="emotion_stimulus_cooldown");advance(state,5);CHECK(state.stimulate({{"emotions",{{"anger",.3}}}},"source-b",999999999,issue));EmotionState restored;restored.restore(state.snapshot());CHECK_FALSE(restored.stimulate({{"desires",{{"rest",.5}}}},"source-a",2000000000,issue));CHECK(issue=="emotion_source_already_used");CHECK_FALSE(restored.stimulate({{"desires",{{"rest",.5}}}},"source-c",2000000000,issue));CHECK(issue=="emotion_stimulus_cooldown");
}
TEST_CASE(actual_emotions_and_desires_remain_bounded_and_have_a_per_tick_slew_limit){
    EmotionState state;std::string issue;CHECK(state.stimulate({{"emotions",{{"anger",1},{"sadness",.7},{"surprise",.6}}},{"desires",{{"rest",.4},{"company",-.2}}}},"stress-event",1000,issue));auto previous=state.snapshot();
    for(int step=0;step<2400;++step){state.tick(.25,{.15,.4,.7,.8,false});const auto current=state.snapshot();for(auto it=current.at("emotions").begin();it!=current.at("emotions").end();++it){const double x=it.value(),old=previous.at("emotions").at(it.key());CHECK(x>=0&&x<=1);CHECK(std::abs(x-old)<=(it.key()=="surprise"?.006251:.002501));}for(auto it=current.at("desires").begin();it!=current.at("desires").end();++it){const double x=it.value(),old=previous.at("desires").at(it.key());CHECK(x>=0&&x<=1);CHECK(std::abs(x-old)<=.001001);}previous=current;}
    const auto before=state.snapshot();state.tick(std::numeric_limits<double>::infinity(),{});state.tick(-1,{});state.tick(.25,{std::numeric_limits<double>::quiet_NaN(),.5,.5,.5,false});CHECK(state.snapshot()==before);
}
TEST_CASE(surprise_fades_faster_and_emotions_decay_without_repeated_stimuli){
    EmotionState state;std::string issue;CHECK(state.stimulate({{"emotions",{{"joy",1},{"anger",1},{"surprise",1}}}},"one-event",1000,issue));advance(state,60);auto peak=state.context();CHECK(peak.at("emotions").at("anger")>.2);CHECK(peak.at("emotions").at("surprise").get<double>()<peak.at("emotions").at("anger").get<double>()*.2);CHECK(peak.at("behavior").at("may_use_dry_irony")==true);advance(state,3600);const auto faded=state.context();for(const auto& value:faded.at("emotions"))CHECK(value.get<double>()<.001);CHECK(faded.at("dominant_emotion")=="");
}
TEST_CASE(pause_and_offline_restore_do_not_advance_affect_or_replay_desire){
    RoleState state;state.restore(Json::object(),1000);std::string issue;CHECK(state.stimulate({{"emotions",{{"sadness",.8}}},{"desires",{{"rest",.8}}}},"real-event",1000,issue));for(int i=1;i<=40;++i)state.tick(1000+i*250);const auto before=state.snapshot();state.tick(86401000,false);CHECK(state.snapshot().at("affect")==before.at("affect"));RoleState restored;restored.restore(state.snapshot(),172801000);CHECK(restored.snapshot().at("affect")==before.at("affect"));CHECK(restored.snapshot().at("mood")==before.at("mood"));CHECK_FALSE(restored.stimulate({{"emotions",{{"joy",.7}}}},"real-event",172801001,issue));CHECK(issue=="emotion_source_already_used");
}
TEST_CASE(anger_affects_existing_targets_slowly_without_forcing_an_activity_or_reply){
    RoleState angry,neutral;angry.restore(Json::object(),1000);neutral.restore(Json::object(),1000);std::string issue;CHECK(angry.stimulate({{"emotions",{{"anger",1}}}},"rude-event",1000,issue));for(int i=1;i<=240;++i){angry.tick(1000+i*250);neutral.tick(1000+i*250);}const auto actual=angry.snapshot(),control=neutral.snapshot();CHECK(actual.at("mood").get<double>()<control.at("mood").get<double>());CHECK(actual.at("willingness").get<double>()<control.at("willingness").get<double>());CHECK(actual.at("activity")==control.at("activity"));CHECK(actual.at("desired_activity")==control.at("desired_activity"));CHECK(angry.emotionalContext().at("behavior").at("may_use_dry_irony")==true);CHECK(angry.emotionalContext().at("behavior").at("may_remain_silent")==true);
}
TEST_CASE(invalid_persistence_cannot_reset_values_or_source_protection){
    EmotionState state;std::string issue;CHECK(state.stimulate({{"emotions",{{"affection",.4}}}},"source",1000,issue));advance(state,8);const auto before=state.snapshot();auto invalid=before;invalid["emotions"]["sadness"]=2;CHECK(rejectsRestore(state,invalid));CHECK(state.snapshot()==before);invalid=before;invalid["sources"].push_back(invalid["sources"][0]);CHECK(rejectsRestore(state,invalid));CHECK(state.snapshot()==before);invalid=before;invalid["clock_seconds"]=-2;CHECK(rejectsRestore(state,invalid));CHECK(state.snapshot()==before);invalid=before;invalid["version"]=99;CHECK(rejectsRestore(state,invalid));CHECK(state.snapshot()==before);invalid=before;invalid["last_stimulus_seconds"]=-.5;CHECK(rejectsRestore(state,invalid));CHECK(state.snapshot()==before);invalid=before;invalid["sources"]=Json::array();CHECK(rejectsRestore(state,invalid));CHECK(state.snapshot()==before);
}
TEST_CASE(source_ledger_is_bounded_and_company_is_not_an_absence_punishment){
    EmotionState state;std::string issue;for(int i=0;i<140;++i){advance(state,5);CHECK(state.stimulate({{"emotions",{{"joy",.1}}}},"source-"+std::to_string(i),1000+i,issue));}CHECK(state.snapshot().at("sources").size()==128);CHECK_FALSE(state.stimulate({{"emotions",{{"joy",.1}}}},"source-139",9999,issue));CHECK(issue=="emotion_source_already_used");EmotionState alone;advance(alone,3600,{.7,.55,.2,0,false});CHECK(alone.context().at("desires").at("company").get<double>()<=.5);CHECK(alone.context().at("emotions").at("anger")==0);CHECK(alone.context().at("emotions").at("sadness")==0);
}
TEST_CASE(role_restore_requires_v4_affect_and_rejects_late_corruption_atomically){
    RoleState state;state.restore(Json::object(),1000);std::string issue;CHECK(state.stimulate({{"emotions",{{"anger",.8}}}},"kept-source",1000,issue));for(int i=1;i<=40;++i)state.tick(1000+i*250);const auto before=state.snapshot();
    const auto rejected=[&](const Json& saved){bool failed=false;try{state.restore(saved,50000);}catch(...){failed=true;}CHECK(failed);CHECK(state.snapshot()==before);};
    auto invalid=before;invalid.erase("affect");rejected(invalid);invalid=before;invalid["affect"]=Json::object();rejected(invalid);invalid=before;invalid["energy"]=.11;invalid["focus"]=std::string(1025,'x');rejected(invalid);invalid=before;invalid["energy"]=.11;invalid["updated_ms"]=-1;rejected(invalid);invalid=before;invalid["updated_ms"]="bad-clock";rejected(invalid);
    CHECK_FALSE(state.stimulate({{"emotions",{{"joy",.8}}}},"kept-source",50001,issue));CHECK(issue=="emotion_source_already_used");
    RoleState restored;restored.restore(before,50000);const auto after=restored.snapshot();for(const auto* field:{"affect","energy","openness","mood","attention","willingness","social_load","target_social_load","target_valence","appraisal_source_id","appraisal_valence","appraisal_age_ms","weather_mood","focus","conversation_mode","target_mood","target_openness","target_attention","activity","desired_activity","evidence","stimulus_age_ms","weather_code","weather_observed_ms","weather_comfort"})CHECK(after.at(field)==before.at(field));
}
TEST_CASE(public_affect_bands_change_only_after_current_values_cross_coarse_thresholds){
    EmotionState state;const auto initial=state.context();std::string issue;CHECK(state.stimulate({{"emotions",{{"anger",1}}},{"desires",{{"rest",1}}}},"coarse-band",1000,issue));CHECK(state.context().at("dominant_emotion")==initial.at("dominant_emotion"));CHECK(state.context().at("dominant_desire")==initial.at("dominant_desire"));advance(state,60,{.15,.4,.7,.8,false});const auto coarse=state.context();CHECK(coarse.at("dominant_emotion")=="anger");CHECK(coarse.at("dominant_desire")=="rest");const auto rawBefore=state.snapshot();state.tick(.25,{.15,.4,.7,.8,false});CHECK(state.snapshot()!=rawBefore);CHECK(state.context().at("dominant_emotion")==coarse.at("dominant_emotion"));CHECK(state.context().at("dominant_desire")==coarse.at("dominant_desire"));
}
TEST_CASE(irony_permission_separates_mixed_anger_even_when_joy_remains_dominant){
    EmotionState state;auto saved=state.snapshot();saved["emotions"]["joy"]=.6;saved["emotions"]["anger"]=.195;state.restore(saved);const auto before=state.context();CHECK(before.at("dominant_emotion")=="joy");CHECK(before.at("behavior").at("may_use_dry_irony")==false);std::string issue;CHECK(state.stimulate({{"emotions",{{"anger",1}}}},"mixed-event",1000,issue));advance(state,1);const auto after=state.context();CHECK(after.at("dominant_emotion")=="joy");CHECK(after.at("behavior").at("may_use_dry_irony")==true);
}
TEST_CASE(background_feeling_and_future_plan_are_optional_bounded_and_reject_ambiguous_fields){
    Json batch={{"version",1},{"commands",Json::array()}};CHECK(validateActivities(batch.dump()).ok);
    batch["feeling"]={{"source_id","actual-source"},{"stimulus",{{"emotions",{{"joy",.6},{"anger",nullptr}}},{"desires",{{"play",.2}}}}}};
    batch["schedule"]={{"id","later-note"},{"topic","稍后看看草稿"},{"memory","medium"},{"when",{{"type","at"},{"after_minutes",60}}},{"source_ids",Json::array({"actual-source"})}};
    auto checked=validateActivities(batch.dump());CHECK(checked.ok);CHECK(!checked.value.at("feeling").at("stimulus").at("emotions").contains("anger"));CHECK(checked.value.at("schedule").at("id")=="later-note");
    auto invalid=batch;invalid["schedule"]["permission_override"]=true;CHECK(!validateActivities(invalid.dump()).ok);
    invalid=batch;invalid["feeling"]["stimulus"]["emotions"]["dependency"]=nullptr;CHECK(!validateActivities(invalid.dump()).ok);
    invalid=batch;invalid["schedule"]["source_ids"]=Json::array();CHECK(!validateActivities(invalid.dump()).ok);
    CHECK(!validateActivities(R"({"version":1,"commands":[],"feeling":null,"feeling":{"source_id":"x","stimulus":{"emotions":{"joy":1}}}})").ok);
    batch["schedule"]=nullptr;batch["feeling"]=nullptr;checked=validateActivities(batch.dump());CHECK(checked.ok);CHECK(!checked.value.contains("schedule"));CHECK(!checked.value.contains("feeling"));
}
TEST_SUITE_END
