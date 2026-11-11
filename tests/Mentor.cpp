#include <Hibernation/MentorAdvice.h>
#include <Hibernation/MentorExecutable.h>
#include <Hibernation/PluginRuntime.h>
#include <Hibernation/OperationReplay.h>
#include <AYTestFixtures.h>
#include <fstream>
using namespace hibernation;
namespace {
PluginJson catalogue(){return PluginJson::array({PluginJson{{"name","workspace.list"},{"parameters",{{"type","object"},{"additionalProperties",false},{"properties",{{"mount",{{"enum",PluginJson::array({"materials","drafts"})}}}}},{"required",PluginJson::array({"mount"})}}}}});}
PluginJson advice(){return {{"status","advice"},{"summary","先看实际素材"},{"missing_capability",""},{"steps",PluginJson::array({PluginJson{{"instruction","查看素材"},{"tool","workspace.list"},{"arguments_json","{\"mount\":\"materials\"}"}}})}};}
}
TEST_SUITE(Mentor)
TEST_CASE(managed_codex_update_recovers_only_official_versioned_paths){
    ayt::test::ScratchDirectory scratch("mentor-executable");const auto root=scratch.path()/"managed";std::filesystem::create_directories(root/"new-version");
    const auto current=root/"new-version"/"codex.exe";std::ofstream(current)<<"fixture";
    CHECK(resolveMentorExecutable(root/"old-version"/"codex.exe",root)==current);
    CHECK(resolveMentorExecutable({},root)==current);
    CHECK(resolveMentorExecutable(scratch.path()/"custom"/"codex.exe",root).empty());
    CHECK(resolveMentorExecutable(root/"old-version"/"other.exe",root).empty());
    const auto custom=scratch.path()/"chosen.exe";std::ofstream(custom)<<"fixture";CHECK(resolveMentorExecutable(custom,root)==custom);
}
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
TEST_CASE(conversation_answers_without_operation_steps_are_valid){
    auto value=advice();value["summary"]="我愿意聊聊这个想法。";value["steps"]=PluginJson::array();const auto answer=mentor::validate(value.dump(),PluginJson::array());
    CHECK(answer&&answer->at("proposals").empty()&&answer->at("operations_executed")==false);CHECK(answer->at("summary")==value.at("summary"));
    CHECK(!mentor::validate(advice().dump(),PluginJson::array()));
}
#ifdef HA_MENTOR_FIXTURE
TEST_CASE(continuing_native_mentor_recovers_private_history_and_owns_its_workspace){
    ayt::test::ScratchDirectory scratch("mentor-session");const auto root=scratch.path();std::string issue;
    const PluginJson settings={{"mentor",{{"enabled",true},{"codexExecutable",HA_MENTOR_FIXTURE},{"cooldownSeconds",0},{"timeoutSeconds",5}}}};
    PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{root,HA_PROJECT_ROOT,false,{}});CHECK(runtime.configure(settings,issue));
    const auto tools=runtime.catalog();const auto execute=std::find_if(tools.begin(),tools.end(),[](const auto& t){return t.at("name")=="mentor.execute";});CHECK(execute!=tools.end());if(execute==tools.end())return;
    CHECK(ayt::serializer::validateJsonDocument(R"({"goal":"帮我操作"})",execute->at("parameters").dump()));
    auto status=runtime.invoke("mentor.status",PluginJson::object(),{});CHECK_MSG(status.value("ok",false),status.dump().c_str());if(!status.value("ok",false))return;
    CHECK(status.at("result").at("requests_used")==0);const auto session=status.at("result").at("session_id");
    const auto ask=[&](const char* goal,const char* turn){runtime.event({{"kind","mentor.context"},{"turn_id",turn},{"tools",PluginJson::array()}});return runtime.invoke("mentor.ask",{{"goal",goal}},{});};
    auto answer=ask("fixture_chat","first");CHECK_MSG(answer.value("ok",false),answer.dump().c_str());if(!answer.value("ok",false))return;
    CHECK(answer.at("result").at("summary")=="可以聊一会儿。你刚才想到什么有趣的事？");CHECK(answer.at("result").at("execution_mode")=="native_mentor");CHECK(!answer.at("result").at("host_execution_required").get<bool>());
    answer=ask("fixture_continuity","second");CHECK(answer.value("ok",false)&&answer.at("result").at("summary")=="native-turn-2");
    CHECK(ask("fixture_chat","second").at("error").at("code")=="mentor_once_per_turn");
    answer=ask("fixture_memory","memory");CHECK(answer.value("ok",false));
    const auto saved=runtime.snapshots();runtime.stop();
    PluginRuntime resumed(HA_TEST_PLUGIN_ROOT,{root,HA_PROJECT_ROOT,false,{}});resumed.restore(saved);CHECK(resumed.configure(settings,issue));
    resumed.event({{"kind","mentor.context"},{"turn_id","recover"},{"tools",PluginJson::array()}});answer=resumed.invoke("mentor.ask",{{"goal","fixture_recovered"}},{});CHECK_MSG(answer.value("ok",false),answer.dump().c_str());if(answer.value("ok",false)){CHECK(answer.at("result").at("session_id")==session);CHECK(answer.at("result").at("summary")=="recovered-history");}
    resumed.event({{"kind","mentor.context"},{"turn_id","execute"},{"tools",PluginJson::array()},{"execution_authorized",true}});answer=resumed.invoke("mentor.execute",{{"goal","fixture_execute_write"}},{});CHECK(answer.value("ok",false)&&answer.at("result").at("operations_executed")==true);CHECK(std::filesystem::is_regular_file(root/"mentors"/"default"/"workspace"/"mentor-proof.txt"));
    CHECK(!std::filesystem::exists(root/"drafts"/"mentor-proof.txt"));
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root/"mentors"/"default"))if(entry.is_regular_file()&&entry.path().extension()==".dpapi"){std::ifstream in(entry.path(),std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(in)),{});CHECK(bytes.find("fixture-private-memory")==bytes.npos);CHECK(bytes.find("fixture_chat")==bytes.npos);}
}
TEST_CASE(native_mentor_failures_and_permissions_are_explicit_and_do_not_repeat_operations){
    ayt::test::ScratchDirectory scratch("mentor-errors");PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{scratch.path(),HA_PROJECT_ROOT,false,{}});std::string issue;
    CHECK(runtime.configure({{"mentor",{{"enabled",true},{"codexExecutable",HA_MENTOR_FIXTURE},{"cooldownSeconds",0},{"timeoutSeconds",5},{"maxRequestsPerHour",5}}}},issue));
    const auto ask=[&](const char* goal,const char* turn){runtime.event({{"kind","mentor.context"},{"turn_id",turn},{"tools",PluginJson::array()}});return runtime.invoke("mentor.ask",{{"goal",goal}},{});};
    auto answer=ask("fixture_approval","approval");CHECK_MSG(answer.value("ok",false),answer.dump().c_str());if(answer.value("ok",false))CHECK(answer.at("result").at("approval_required")==true&&!answer.at("result").at("operations_executed").get<bool>());
    answer=ask("fixture_malformed","bad");CHECK(!answer.value("ok",false));
    answer=ask("fixture_failure","failure");CHECK(!answer.value("ok",false)&&answer.dump().find("private upstream")==std::string::npos);
    answer=ask("fixture_timeout","timeout");CHECK(!answer.value("ok",false)&&answer.at("error").at("code")=="mentor_timeout");
    runtime.event({{"kind","mentor.context"},{"turn_id","cancel"}});CHECK(runtime.invoke("mentor.ask",{{"goal","skip"}},[]{return true;}).at("error").at("code")=="cancelled");
    CHECK(runtime.invoke("mentor.session.rebuild",PluginJson::object(),{}).value("ok",false));
    CHECK(ask("fixture_chat","last").value("ok",false));CHECK(ask("fixture_chat","over").at("error").at("code")=="mentor_hourly_budget");
}

TEST_CASE(mentor_models_and_efforts_are_configurable_without_splitting_native_conversation){
    ayt::test::ScratchDirectory scratch("mentor-routing");PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{scratch.path(),HA_PROJECT_ROOT,false,{}});std::string issue;
    PluginJson cfg={{"mentor",{{"enabled",true},{"codexExecutable",HA_MENTOR_FIXTURE},{"cooldownSeconds",0},{"timeoutSeconds",5}}}};CHECK(runtime.configure(cfg,issue));
    const auto ask=[&](const char* level,const char* turn){runtime.event({{"kind","mentor.context"},{"turn_id",turn}});return runtime.invoke("mentor.ask",{{"goal","fixture_routing"},{"level",level}},{});};
    auto reply=ask("casual","fixed");CHECK_MSG(reply.value("ok",false),reply.dump().c_str());if(!reply.value("ok",false))return;CHECK(reply.at("result").at("summary")=="gpt-6.1-sol|high|1");
    cfg["mentor"]["routingMode"]="character";cfg["mentor"]["casualModel"]="gpt-6-luna";CHECK(runtime.configure(cfg,issue));
    reply=ask("casual","casual");CHECK(reply.value("ok",false)&&reply.at("result").at("summary")=="gpt-6-luna|low|1");
    reply=ask("normal","normal");CHECK(reply.value("ok",false)&&reply.at("result").at("summary")=="gpt-6.1-sol|medium|2");
    reply=ask("complex","complex");CHECK(reply.value("ok",false)&&reply.at("result").at("summary")=="gpt-6.1-sol|high|3");
    auto invalid=cfg;invalid["mentor"]["normalEffort"]="unsupported";CHECK(!runtime.configure(invalid,issue));
    const auto status=runtime.invoke("mentor.status",PluginJson::object(),{});CHECK(status.at("result").at("last_selected_effort")=="high");CHECK(status.at("result").at("work_quota").at("known")==true);
}
TEST_CASE(exhausted_work_routes_to_verified_chat_page_and_retains_encrypted_mentor_identity){
    ayt::test::ScratchDirectory scratch("mentor-chat-fallback");const auto quota=scratch.path()/"mentor-quota.exe";std::filesystem::copy_file(HA_MENTOR_FIXTURE,quota);auto native=quota.u8string();const std::string executable(reinterpret_cast<const char*>(native.data()),native.size());
    int sends=0;bool normalChat=true;PluginHostContext host{scratch.path(),HA_PROJECT_ROOT,true,{}};host.browser=[&](const std::string& op,const PluginJson& args,const std::function<bool()>&){
        if(op=="mentor_chat.status")return toolSuccess({{"ready",true},{"mode_verified",normalChat},{"work_mode_detected",!normalChat}});
        if(op=="mentor_chat.send"){++sends;CHECK(args.at("prompt").get_ref<const std::string&>().size()<=18*1024);return toolSuccess({{"accepted",true}});}
        if(op=="mentor_chat.poll")return toolSuccess({{"state","complete"},{"text","synthetic ordinary Chat mentor reply"},{"url","https://chatgpt.com/c/01234567-89ab-cdef-0123-456789abcdef"}});
        return toolSuccess({{"opened",true}});
    };
    PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,host);std::string issue;PluginJson cfg={{"mentor",{{"enabled",true},{"codexExecutable",executable},{"cooldownSeconds",0},{"timeoutSeconds",5}}}};CHECK(runtime.configure(cfg,issue));
    runtime.event({{"kind","mentor.context"},{"turn_id","fallback"},{"execution_authorized",true}});auto reply=runtime.invoke("mentor.execute",{{"goal","fixture_chat"}},{});CHECK_MSG(reply.value("ok",false),reply.dump().c_str());if(!reply.value("ok",false))return;
    CHECK(reply.at("result").at("execution_mode")=="chat_web_advice");CHECK(reply.at("result").at("provider")=="chatgpt_web_bridge");CHECK(reply.at("result").at("requires_work_for_execution")==true);CHECK(reply.at("result").at("operations_executed")==false);CHECK(sends==1);
    const auto state=runtime.snapshots();CHECK(state.at("mentor").at("work_quota").at("exhausted")==true);CHECK(state.at("mentor").at("chat_conversation_url")=="https://chatgpt.com/c/01234567-89ab-cdef-0123-456789abcdef");
    runtime.event({{"kind","mentor.context"},{"turn_id","fallback"}});CHECK(runtime.invoke("mentor.ask",{{"goal","again"}},{}).at("error").at("code")=="mentor_once_per_turn");CHECK(sends==1);
    normalChat=false;runtime.event({{"kind","mentor.context"},{"turn_id","not-chat"}});reply=runtime.invoke("mentor.ask",{{"goal","do not send to Work page"}},{});CHECK(!reply.value("ok",false)&&reply.at("error").at("code")=="mentor_chat_mode_unverified");CHECK(sends==1);
    cfg["mentor"]["quotaFallback"]="pause";CHECK(runtime.configure(cfg,issue));runtime.event({{"kind","mentor.context"},{"turn_id","pause"}});reply=runtime.invoke("mentor.ask",{{"goal","stay paused"}},{});CHECK(!reply.value("ok",false)&&reply.at("error").at("code")=="mentor_work_quota_exhausted");CHECK(sends==1);
}
#endif
TEST_SUITE_END
