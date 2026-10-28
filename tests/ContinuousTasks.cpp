#include <Hibernation/ContinuousTasks.h>
#include <Hibernation/TurnProtocol.h>
#include <Hibernation/NativeToolBridge.h>
#include <Hibernation/SkillRegistry.h>
#include <AYSerializer/JsonSchema.h>
#include <AYTestFixtures.h>
using namespace hibernation;
using Json=nlohmann::json;
TEST_SUITE(ContinuousTasks)
TEST_CASE(task_promises_and_own_bookkeeping_do_not_prove_completion){
    CHECK(!continuous::completionEvidence({{"kind","user_message"},{"text","请做一件小作品"}}));
    CHECK(!continuous::completionEvidence({{"kind","tool_intent"},{"tool","pixel.show"},{"result",{{"accepted",true},{"completed",false}}}}));
    for(const auto* tool:{"task.start","task.checkpoint","self.notice","memory.remember","schedule.create","state.propose"})CHECK(!continuous::completionEvidence({{"kind","tool_observation"},{"tool",tool},{"result",{{"completed",true}}}}));
    CHECK(continuous::completionEvidence({{"kind","tool_observation"},{"tool","workspace.write"},{"result",{{"written",true}}}}));
    CHECK(continuous::completionEvidence({{"kind","tool_observation"},{"tool","pixel.edit"},{"result",{{"completed",true}}}}));
    CHECK(continuous::completionEvidence({{"kind","tool_observation"},{"tool","reading.open"},{"result",Json::object()}}));
    CHECK(!continuous::completionEvidence({{"kind","assistant_message"},{"text","我已经做完了"}}));
}
TEST_CASE(task_views_never_publish_private_authority_or_raw_execution_state){
    const Json task={{"id","task-a"},{"goal","做一盏灯"},{"status","running"},{"completion_criterion","保存真实草稿"},{"instructions",Json::array({"private-source"})},{"allowed_tools",Json::array({"workspace.write"})},{"channel_identity",{{"sender_user_id","12345"}}},{"checkpoint",{{"trace_id","private-trace"}}},{"bindings",{{"drafts","private-root"}}}};
    const auto view=continuous::statusView(task,4);CHECK(view.at("revision")==4);CHECK(view.at("goal")==task.at("goal"));CHECK(!view.contains("instructions"));CHECK(!view.contains("allowed_tools"));CHECK(!view.contains("channel_identity"));CHECK(!view.contains("checkpoint"));CHECK(!view.contains("bindings"));
}
TEST_CASE(task_tool_contracts_require_typed_scoped_versioned_operations){
    const auto tools=continuous::tools();CHECK(tools.size()==5);
    const auto schema=[&](const char* name){for(const auto& tool:tools)if(tool.at("name")==name)return tool.at("parameters").dump();return std::string{};};
    CHECK(ayt::serializer::validateJsonDocument(Json{{"goal","灯"},{"completion_criterion","写入草稿"},{"source_id","actual-user"}}.dump(),schema("task.start")));
    CHECK(!ayt::serializer::validateJsonDocument(Json{{"goal","灯"},{"completion_criterion","写入草稿"},{"source_id","actual-user"},{"shell","anything"}}.dump(),schema("task.start")));
    CHECK(!ayt::serializer::validateJsonDocument(Json{{"id","task-a"},{"expected_revision",0},{"action","resume"},{"source_id","user"}}.dump(),schema("task.control")));
    CHECK(!ayt::serializer::validateJsonDocument(Json{{"id","task-a"},{"expected_revision",1},{"action","delete"},{"source_id","user"}}.dump(),schema("task.control")));
    CHECK(!ayt::serializer::validateJsonDocument(Json{{"id","task-a"},{"expected_revision",1},{"status","completed"},{"summary","写完"},{"next_step",""},{"source_ids",Json::array()},{"wait_seconds",0}}.dump(),schema("task.checkpoint")));
    CHECK(!skillGrantableTool("task.start"));CHECK(!skillGrantableTool("task.control"));CHECK(!skillGrantableTool("task.revise"));CHECK(skillGrantableTool("task.status"));
    NativeToolBridge bridge(tools);CHECK(bridge.definitions.size()>=tools.size());
}
TEST_CASE(task_pause_and_wait_are_distinct_from_terminal_outcomes){
    for(const auto* status:{"ready","running","waiting_input","paused","blocked"})CHECK(!continuous::terminal(status));
    CHECK(continuous::terminal("completed"));CHECK(continuous::terminal("abandoned"));
    CHECK(continuous::safeId("task-123_a"));CHECK(!continuous::safeId("../task"));CHECK(!continuous::safeId("task/a"));CHECK(!continuous::safeId(""));
}
TEST_SUITE_END
