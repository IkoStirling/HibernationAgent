#include <Hibernation/MentorAdvice.h>
#include <Hibernation/PluginRuntime.h>
#include <Hibernation/OperationReplay.h>
#include <AYTestFixtures.h>
using namespace hibernation;
namespace {
PluginJson catalogue(){return PluginJson::array({PluginJson{{"name","workspace.list"},{"parameters",{{"type","object"},{"additionalProperties",false},{"properties",{{"mount",{{"enum",PluginJson::array({"materials","drafts"})}}}}},{"required",PluginJson::array({"mount"})}}}}});}
PluginJson advice(){return {{"status","advice"},{"summary","先看实际素材"},{"missing_capability",""},{"steps",PluginJson::array({PluginJson{{"instruction","查看素材"},{"tool","workspace.list"},{"arguments_json","{\"mount\":\"materials\"}"}}})}};}
}
TEST_SUITE(Mentor)
TEST_CASE(deferred_or_rejected_requests_cannot_be_promoted_to_completed_effects){
    CHECK(mentor::pendingEffect(PluginJson{{"queued",1}}));CHECK(mentor::pendingEffect(PluginJson{{"queued",true}}));CHECK(mentor::pendingEffect(PluginJson{{"pending",PluginJson::array({1})}}));
    CHECK(mentor::pendingEffect(PluginJson{{"completed",false}}));CHECK(!mentor::pendingEffect(PluginJson{{"pending",PluginJson::array()},{"queued",0}}));CHECK(!mentor::pendingEffect(PluginJson{{"written",true}}));
    CHECK(replayProtectedOperation("mentor.execute"));
}
TEST_CASE(advice_never_claims_execution_and_unknown_or_malformed_tools_fail_closed){
    auto value=advice();auto result=mentor::validate(value.dump(),catalogue());CHECK(result&&result->at("advisory_only")==true&&result->at("operations_executed")==false);CHECK(result->at("proposals")[0].at("args").at("mount")=="materials");
    value["steps"][0]["tool"]="mentor.ask";CHECK(!mentor::validate(value.dump(),catalogue()));value=advice();value["steps"][0]["tool"]="workspace.write";CHECK(!mentor::validate(value.dump(),catalogue()));
    value=advice();value["steps"][0]["arguments_json"]="{\"mount\":\"archive\"}";CHECK(!mentor::validate(value.dump(),catalogue()));value=advice();value["steps"][0]["arguments_json"]="{\"mount\":\"materials\",\"command\":\"extra\"}";CHECK(!mentor::validate(value.dump(),catalogue()));
    value=advice();value["operations_executed"]=true;CHECK(!mentor::validate(value.dump(),catalogue()));value=advice();value["status"]="blocked";CHECK(!mentor::validate(value.dump(),catalogue()));
    value["steps"]=PluginJson::array();CHECK(mentor::validate(value.dump(),catalogue()));CHECK(!mentor::validate("```json\n"+value.dump()+"\n```",catalogue()));
    CHECK(!mentor::operational("memory.remember")&&!mentor::operational("state.propose")&&!mentor::operational("mentor.ask"));
}
TEST_CASE(transport_requires_actual_completion_and_ignores_non_json_diagnostics){
    const auto item=PluginJson{{"type","item.completed"},{"item",{{"type","agent_message"},{"text",advice().dump()}}}}.dump()+"\n";
    CHECK(!mentor::finalMessage(item));CHECK(mentor::finalMessage("diagnostic line\n"+item+"{\"type\":\"turn.completed\"}\n")==advice().dump());
    CHECK(!mentor::finalMessage(item+"{\"type\":\"turn.completed\"}\n{\"type\":\"turn.failed\"}\n"));
    CHECK(!mentor::finalMessage(item+"{\"type\":\"item.completed\",\"item\":{\"type\":\"command_execution\"}}\n{\"type\":\"turn.completed\"}\n"));
}
#ifdef HA_MENTOR_FIXTURE
TEST_CASE(real_child_bridge_limits_and_cancellation_preserve_character_authority){
    ayt::test::ScratchDirectory scratch("mentor-child");PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{scratch.path(),HA_PROJECT_ROOT,false,{}});std::string issue;
    const PluginJson settings={{"mentor",{{"enabled",true},{"backend","chatgpt"},{"codexExecutable",HA_MENTOR_FIXTURE},{"cooldownSeconds",0},{"timeoutSeconds",5},{"maxRequestsPerHour",12}}}};
    const auto configured=runtime.configure(settings,issue);CHECK_MSG(configured,issue.c_str());if(!configured)return;const auto status=runtime.invoke("mentor.status",PluginJson::object(),{});CHECK(status.at("ok").get<bool>()&&status.at("result").at("requests_used")==0);
    const auto ask=[&](const char* goal,const char* turn){runtime.event({{"kind","mentor.context"},{"turn_id",turn},{"tools",catalogue()}});return runtime.invoke("mentor.ask",{{"goal",goal},{"obstacle","不知道怎么查找素材"},{"tool_names",PluginJson::array({"workspace.list"})}},{});};
    auto result=ask("查找素材","turn1");CHECK_MSG(result.at("ok").get<bool>(),result.dump().c_str());if(result.at("ok")==true)CHECK(result.at("result").at("operations_executed")==false);
    CHECK(ask("查找素材","turn1").at("error").at("code")=="mentor_once_per_turn");
    CHECK(ask("fixture_forbidden","turn2").at("error").at("code")=="mentor_invalid_advice");CHECK(ask("fixture_malformed","turn3").at("error").at("code")=="mentor_incomplete_response");
    result=ask("fixture_failure","turn4");CHECK(result.at("error").at("code")=="mentor_request_failed");CHECK(result.dump().find("private upstream")==std::string::npos);
    CHECK(ask("fixture_timeout","turn5").at("error").at("code")=="mentor_timeout");
    runtime.event({{"kind","mentor.context"},{"turn_id","cancelled"},{"tools",catalogue()}});CHECK(runtime.invoke("mentor.ask",{{"goal","cancel"},{"obstacle","unknown"},{"tool_names",PluginJson::array({"workspace.list"})}},[]{return true;}).at("error").at("code")=="cancelled");
    const auto saved=runtime.snapshots();CHECK(saved.at("mentor").at("requests_used")==5);runtime.stop();PluginRuntime restored(HA_TEST_PLUGIN_ROOT,{scratch.path(),HA_PROJECT_ROOT,false,{}});restored.restore(saved);auto limited=settings;limited["mentor"]["maxRequestsPerHour"]=5;CHECK(restored.configure(limited,issue));restored.event({{"kind","mentor.context"},{"turn_id","turn6"},{"tools",catalogue()}});
    CHECK(restored.invoke("mentor.ask",{{"goal","查找素材"},{"obstacle","unknown"},{"tool_names",PluginJson::array({"workspace.list"})}},{}).at("error").at("code")=="mentor_hourly_budget");
}
#endif
TEST_SUITE_END
