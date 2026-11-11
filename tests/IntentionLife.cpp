#include <Hibernation/IntentionLife.h>
#include <Hibernation/IntentionEffects.h>
#include <Hibernation/IntentionContext.h>
#include <Hibernation/CurrentActions.h>
#include <Hibernation/ObservationContext.h>
#include <Hibernation/PluginCharacterContext.h>
#include <Hibernation/ActivityPlan.h>
#include <AYTestFixtures.h>
using namespace hibernation;
TEST_SUITE(IntentionLife)
TEST_CASE(pause_is_reversible_without_completing_or_scheduling_the_intention){
    Json thread={{"kind","intention"},{"status","active"},{"note","想做一点茶点"},{"edit_revision",4}};
    Json proposal={{"choice","pause"},{"when","soon"},{"note","先把手边的故事看完"}};
    Json sources=Json::array({{{"id","real-source"},{"scope","ha.archive"},{"revision",1},{"kind","user_message"}}});
    auto paused=intention_life::reconsidered(thread,proposal,sources,1000);
    CHECK(!intention_life::available(paused,1001));CHECK(!intention_life::available(paused,300999));CHECK(intention_life::available(paused,301000));
    CHECK(paused.at("status")=="active");CHECK(paused.at("note")==thread.at("note"));CHECK(paused.at("edit_revision")==4);CHECK(!paused.contains("scheduled_job"));
    proposal["choice"]="resume";auto resumed=intention_life::reconsidered(paused,proposal,sources,1500);CHECK(intention_life::available(resumed,1500));CHECK(resumed.at("decision_sources").size()==1);
    resumed["status"]="released";CHECK(!intention_life::available(resumed,99999999));
}
TEST_CASE(each_life_domain_has_tools_but_cannot_invoke_external_contact_or_activate_another_domain){
    CHECK(intention_life::toolAllowed("canteen.recipe","kitchen"));CHECK(intention_life::toolAllowed("canteen.request_supplies","kitchen"));CHECK(intention_life::toolAllowed("habitat.gather","kitchen"));
    CHECK(intention_life::toolAllowed("reading.open","reading"));CHECK(intention_life::toolAllowed("atelier.write","reading"));CHECK(intention_life::toolAllowed("pixel.create","creative"));
    CHECK(intention_life::toolAllowed("computer.screenshot","desktop"));CHECK(!intention_life::toolAllowed("computer.screenshot","kitchen"));CHECK(!intention_life::toolAllowed("canteen.cook","reading"));
    for(const auto* domain:{"kitchen","reading","creative","desktop"})for(const auto* tool:{"napcat.send_image","computer.open_application","mentor.ask","skills.activate","task.start"})CHECK(!intention_life::toolAllowed(tool,domain));
    CHECK(!intention_life::toolAllowed("self.observe","made-up-domain"));
}
TEST_CASE(pursuit_requires_existing_binding_and_one_bounded_domain){
    Json command={{"kind","pursue_intention"},{"domain","kitchen"},{"intention",{{"id","tea"},{"revision",2}}}};
    Json batch={{"version",1},{"commands",Json::array({command})}};CHECK(validateActivities(batch.dump()).ok);
    batch["commands"][0].erase("intention");CHECK(!validateActivities(batch.dump()).ok);batch["commands"]=Json::array({command,command});CHECK(!validateActivities(batch.dump()).ok);
    batch["commands"]=Json::array({command});batch["commands"][0]["domain"]="system";CHECK(!validateActivities(batch.dump()).ok);
}
TEST_CASE(reconsidered_versions_are_never_enqueued_in_the_same_batch){
    Json proposal={{"id","tea"},{"expected_revision",2},{"choice","pause"},{"when","later"},{"note","先读几页"},{"source_ids",Json::array({"real"})}};
    Json batch={{"version",1},{"commands",Json::array()},{"reconsideration",proposal}};CHECK(validateActivities(batch.dump()).ok);
    batch["commands"].push_back({{"kind","wait"},{"seconds",1},{"intention",{{"id","tea"},{"revision",2}}}});CHECK(!validateActivities(batch.dump()).ok);
    batch["commands"]=Json::array();batch["close_threads"]=Json::array({{{"id","tea"},{"expected_revision",2},{"outcome","released"},{"note","放下"},{"source_ids",Json::array({"real"})}}});CHECK(!validateActivities(batch.dump()).ok);
    batch.erase("close_threads");batch["reconsideration"]["choice"]="complete";CHECK(!validateActivities(batch.dump()).ok);
}
TEST_CASE(compact_planning_schema_removes_repeated_prose_without_losing_constraints){
    const auto original=activitySchema(),shorter=compactActivitySchema(original);CHECK(shorter.size()+1000<original.size());
    const auto compact=Json::parse(shorter);CHECK(compact.at("additionalProperties")==false);CHECK(compact.at("properties").at("commands").at("maxItems")==6);
    CHECK(shorter.find("expected_revision")!=std::string::npos);CHECK(shorter.find("source_ids")!=std::string::npos);
}
TEST_CASE(actual_kitchen_outcomes_match_exact_batches_and_never_regress){
    Json ready={{"plugin","canteen"},{"kind","canteen.ready"},{"batch",{{"id","tea-1"}}}};
    CHECK(intention_life::effectBatch(ready)=="tea-1");CHECK(intention_life::effectPhase(ready)=="ready");ready["plugin"]="another";CHECK(intention_life::effectBatch(ready).empty());
    Json consumed={{"plugin","habitat"},{"kind","habitat.consumed"},{"item",{{"provenance",{{"batch_id","tea-1"}}}}}};CHECK(intention_life::effectBatch(consumed)=="tea-1");CHECK(intention_life::effectPhase(consumed)=="consumed");
    CHECK(intention_life::advanceEffect("cooking","ready"));CHECK(intention_life::advanceEffect("ready","on_desktop"));CHECK(intention_life::advanceEffect("on_desktop","consumed"));
    CHECK(!intention_life::advanceEffect("on_desktop","ready"));CHECK(!intention_life::advanceEffect("consumed","on_desktop"));CHECK(!intention_life::advanceEffect("cancelled","ready"));CHECK(!intention_life::advanceEffect("ready","ready"));
}
TEST_CASE(ordinary_company_sees_a_kitchen_phase_without_repeating_the_production_timer){
    const Json full={{"job",{{"id","job-1"},{"batch_id","tea-1"},{"recipe_id","tea"},{"phase","cooking"},{"stage_name","注水"},{"remaining_seconds",11.983},{"started_at",45.2},{"input_items",Json::array({"private-lot-ledger"})}}},{"ready_count",1},{"shelf",Json::array({{{"id","tea-2"},{"recipe_id","tea"},{"name","清茶"},{"expires_at",900}}})}};
    const auto ambient=pluginDialogueContext("canteen",full);CHECK(ambient.at("job").at("phase")=="cooking");CHECK(ambient.at("job").at("batch_id")=="tea-1");CHECK(!ambient.at("job").contains("remaining_seconds"));CHECK(!ambient.at("job").contains("input_items"));
    CHECK(ambient.at("ready").at(0).at("name")=="清茶");CHECK(!ambient.at("ready").at(0).contains("expires_at"));CHECK(pluginCharacterContext("canteen",Json{{"current",full}})==full);
}
TEST_CASE(prompt_only_schema_references_expand_to_the_identical_inline_contract){
    const auto inlineText=compactActivitySchema(activitySchema());const auto compactText=activityPromptSchema(inlineText);CHECK(compactText.size()+2000<inlineText.size());
    auto expanded=Json::parse(compactText);const auto defs=expanded.at("$defs");expanded.erase("$defs");
    std::function<void(Json&)> expand=[&](Json& node){if(node.is_object()&&node.contains("$ref")){const auto ref=node.at("$ref").get<std::string>();CHECK(ref.starts_with("#/$defs/"));node=defs.at(ref.substr(8));}if(node.is_object()||node.is_array())for(auto& child:node)expand(child);};expand(expanded);CHECK(expanded==Json::parse(inlineText));
    Json bad={{"version",1},{"commands",Json::array({{{"kind","pursue_intention"},{"domain","creative"},{"intention",{{"id","one"},{"revision",0}}}}})}};CHECK(!validateActivities(bad.dump()).ok);
}
TEST_CASE(updating_the_bound_intention_in_the_same_plan_is_rejected_before_any_side_effect){
    Json note={{"id","story"},{"expected_revision",1},{"kind","intention"},{"topic","灯边的小故事"},{"note","想换一种写法"},{"stance","want"},{"source_ids",Json::array({"real-event"})}};
    Json batch={{"version",1},{"commands",Json::array({{{"kind","pursue_intention"},{"domain","creative"},{"intention",{{"id","story"},{"revision",1}}}}})},{"carry_forward",Json::array({note})}};
    const auto invalid=validateActivities(batch.dump());CHECK(!invalid.ok&&invalid.code=="activity_updated_intention_still_queued");
    batch["carry_forward"][0]["id"]="another-story";CHECK(validateActivities(batch.dump()).ok);batch["commands"]=Json::array();batch["carry_forward"][0]["id"]="story";CHECK(validateActivities(batch.dump()).ok);
}
TEST_CASE(empty_gather_is_an_observed_stop_without_becoming_a_completion){
    Json result={{"finished",true},{"completed",false},{"status","no_matching_resource"},{"gather_id","g-1"},{"collected",0}};
    CHECK(toolObservationKind("habitat.gather",result)=="tool_observation");CHECK(!personalCompletionObservation(Json{{"kind","tool_observation"},{"tool","habitat.gather"},{"result",result}}));
    result["finished"]=false;CHECK(toolObservationKind("habitat.gather",result)=="tool_intent");
}
TEST_CASE(content_basis_stays_bounded_while_the_original_provenance_is_unchanged){
    Json origins=Json::array();for(int i=0;i<80;++i)origins.push_back({{"id","history-"+std::to_string(i)},{"revision",1},{"scope","ha.archive"},{"kind","user_message"}});
    Json latest=Json::array({origins[70],origins[71],origins[72]}),decision=Json::array({origins[77],origins[78],origins[79]});
    const Json thread={{"evidence_sources",origins},{"episodes",Json::array({{{"sources",latest}}})},{"intention_pause",{{"sources",decision}}}};const auto basis=intention_life::contentBasis(thread);
    CHECK(basis.size()==4);CHECK(basis[0].at("id")=="history-72");CHECK(basis[1].at("id")=="history-71");CHECK(basis[2].at("id")=="history-79");CHECK(thread.at("evidence_sources").size()==80);
    const auto brief=intention_life::briefPursuit(Json{{"id","one"},{"domain","creative"},{"narrative_sources",origins}});CHECK(!brief.contains("narrative_sources"));CHECK(brief.at("id")=="one");
}
TEST_CASE(planning_keeps_recent_context_and_rotates_available_older_intentions_without_rewriting_them){
    Json rows=Json::array();for(int i=0;i<12;++i)rows.push_back({{"id","t"+std::to_string(i)},{"kind","intention"},{"revision",1},{"availability","available"},{"last_noticed_ms",12-i},{"note","original-"+std::to_string(i)}});
    const Json all={{"threads",rows},{"matching_count",12}};const auto first=intention_life::planningView(all,Json::object());CHECK(first.at("threads").size()==8);CHECK(first.at("threads")[6].at("id")=="t6");
    Json offers={{"t6",{{"revision",1},{"at_ms",1000}}},{"t7",{{"revision",1},{"at_ms",1000}}}};const auto next=intention_life::planningView(all,offers);CHECK(next.at("threads")[6].at("id")=="t8");CHECK(next.at("threads")[7].at("id")=="t9");CHECK(next.at("omitted_count")==4);CHECK(all.at("threads")==rows);
    auto paused=all;paused["threads"][8]["availability"]="paused";const auto quiet=intention_life::planningView(paused,offers);CHECK(quiet.at("threads")[6].at("id")=="t9");
    const auto focused=intention_life::focusView(all,"t11");CHECK(focused.at("threads")[0]==rows[11]);CHECK(focused.at("threads").size()==8);CHECK(focused.at("omitted_count")==4);
}
TEST_CASE(own_time_has_a_distinct_durable_kind_with_the_same_foreground_priority){
    CHECK(intention_life::solitudeJob("role.personal-pursuit"));CHECK(intention_life::solitudeJob("role.solitude"));CHECK(!intention_life::solitudeJob("role.turn"));CHECK(!intention_life::solitudeJob("operator.tool"));
}
TEST_CASE(body_sees_current_gathering_and_cooking_without_old_outcomes_or_production_timers){
    const Json snapshots={{"habitat",{{"gather",{{"id","g1"},{"phase","collecting"},{"requested",2},{"collected",1},{"needs",{{"berry",1}}},{"results",Json::array({"HIDDEN_OLD_RECEIPT"})}}},{"last_gather",{{"id","HIDDEN_OLD_GATHER"}}}}},{"canteen",{{"current",{{"job",{{"id","j1"},{"batch_id","b1"},{"recipe_id","tea"},{"phase","cooking"},{"remaining_seconds",9}}}}}}}};
    const auto body=currentWorldActions(snapshots);CHECK(body.at("gathering").at("phase")=="collecting");CHECK(body.at("gathering").at("collected")==1);CHECK(body.at("cooking").at("batch_id")=="b1");CHECK(body.dump().find("HIDDEN_OLD")==std::string::npos);CHECK(!body.at("cooking").contains("remaining_seconds"));
    CHECK(currentWorldActions(Json::object()).at("gathering").is_null());CHECK(currentWorldActions(Json{{"habitat",nullptr},{"canteen",{{"current",nullptr}}}}).at("cooking").is_null());
}
TEST_SUITE_END
