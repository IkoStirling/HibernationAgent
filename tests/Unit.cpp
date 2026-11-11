#include <Hibernation/AudioDelivery.h>
#include <Hibernation/Application.h>
#include <Hibernation/TurnProtocol.h>
#include <Hibernation/ModelJson.h>
#include <Hibernation/NativeToolBridge.h>
#include <Hibernation/ContinuousTasks.h>
#include <Hibernation/ChannelProfiles.h>
#include <Hibernation/RoleState.h>
#include <Hibernation/Vault.h>
#include <Hibernation/PluginRuntime.h>
#include <Hibernation/Review.h>
#include <Hibernation/DialogueContext.h>
#include <Hibernation/TextPlayback.h>
#include <Hibernation/MemoryContext.h>
#include <Hibernation/MemoryLibrary.h>
#include <Hibernation/MemoryWorkshop.h>
#include <Hibernation/ActivityPlan.h>
#include <Hibernation/ActivityPruning.h>
#include <Hibernation/AttemptObservation.h>
#include <Hibernation/ActivityReuse.h>
#include <Hibernation/DialoguePreparation.h>
#include <Hibernation/CharacterAnimation.h>
#include <Hibernation/ReactionCache.h>
#include <Hibernation/WidgetAppearance.h>
#include <Hibernation/WidgetPresentation.h>
#include <Hibernation/ModelEmbodiment.h>
#include <Hibernation/AppearanceCatalog.h>
#include <Hibernation/SmallWorld.h>
#include <Hibernation/SplashPixels.h>
#include <Hibernation/DeskObjectAppearance.h>
#include <Hibernation/DropPixels.h>
#include <Hibernation/QuietRhythm.h>
#include <Hibernation/TelevisionLayout.h>
#include <Hibernation/DesktopServices.h>
#include <Hibernation/PersonalThreads.h>
#include <Hibernation/PocketLights.h>
#include <Hibernation/ObservationContext.h>
#include <Hibernation/ToolImages.h>
#include <Hibernation/PluginCharacterContext.h>
#include <Hibernation/SolitudeOpportunities.h>
#include <Hibernation/OperationReplay.h>
#include <Hibernation/NativeExecutionIds.h>
#include <Hibernation/ActivityAvailability.h>
#include <Hibernation/PlanningCadence.h>
#include <Hibernation/WorkspaceText.h>
#include <Hibernation/ReadingPages.h>
#include <Hibernation/ArchiveExperience.h>
#include <Hibernation/TimeContext.h>
#include <Hibernation/ConversationFocus.h>
#include <Hibernation/ConversationOpportunities.h>
#include <AYTestFixtures.h>
#include <fstream>
#include <thread>
using namespace hibernation;
namespace {
Json valid(){return {{"visible_reply","允许没有固定结论。"},{"actions",Json::array()},{"memories",Json::array()},{"state_update",Json::object()},{"stage_proposal",nullptr}};}
ProposalContext context(){ProposalContext c;c.eventKind="user_message";c.eventId="e1";c.stage="initial";c.nextStage="exploring";c.sources={{"e1",{"e1",1,"user_message"}},{"e2",{"e2",1,"story_end"}}};return c;}
}
TEST_SUITE(RolePolicy)
TEST_CASE(workshop_configuration_preserves_protected_paths_and_bounded_orders){
    workshop::validateSettings({{"enabled",true},{"backupRoot",""}});workshop::validateDashboard({{"pluginOrder",Json::array({"widget","tts"})},{"sections",{{"models",Json::array({"profiles","review"})}}}});
    bool denied=false;try{workshop::validateDashboard({{"pluginOrder",Json::array({"widget","widget"})}});}catch(...){denied=true;}CHECK(denied);
    denied=false;try{workshop::validateSettings({{"enabled","true"}});}catch(...){denied=true;}CHECK(denied);
    denied=false;try{workshop::validateSettings({{"backupRoot","relative/path"}});}catch(...){denied=true;}CHECK(denied);
    Config c;c.dataRoot="E:/synthetic-workshop/data";c.characterRoot="D:/synthetic-app/character";c.uiRoot="D:/synthetic-app/ui";c.pluginRoot="D:/synthetic-app/plugins";c.draftsRoot=c.dataRoot/"drafts";c.materialsRoot=c.dataRoot/"materials";c.skillsRoot="D:/synthetic-app/skills";CHECK(workshop::validateRoots(c));c.memoryWorkshop["backupRoot"]="E:/synthetic-workshop/data/backup";CHECK(!workshop::validateRoots(c));c.memoryWorkshop["backupRoot"]="E:/synthetic-backups";c.computer["readRoots"]=Json::array({{{"id","exposed"},{"path","E:/synthetic-backups"}}});CHECK(!workshop::validateRoots(c));
}

TEST_CASE(conversation_focus_is_sourced_bounded_and_retry_safe_without_changing_persona){
    const Json proposal={{"subject","character"},{"advice","avoid"},{"questions","avoid_repeated"},{"source_quote","我想听你自己的话，先别给建议"}};std::string issue;
    auto state=proposeConversationFocus(Json::object(),proposal,"这会儿我想听你自己的话，先别给建议，就随便聊聊。","u0",1,100,issue);CHECK(state.has_value());CHECK(issue.empty());CHECK(validConversationFocus(*state));
    auto first=conversationFocusForTurn(*state,"u1",200);CHECK(first.changed);CHECK(first.view.at("subject")=="character");CHECK(first.record.at("used_event_ids").size()==1);
    const auto retry=conversationFocusForTurn(first.record,"u1",201);CHECK(!retry.changed);CHECK(retry.view==first.view);
    auto used=first.record;for(int i=2;i<=4;++i){auto next=conversationFocusForTurn(used,"u"+std::to_string(i),200+i);CHECK(!next.view.empty());used=std::move(next.record);}
    CHECK(conversationFocusForTurn(used,"u5",300).view.empty());CHECK(!conversationFocusForTurn(used,"u4",301).view.empty());
    CHECK(conversationFocusForTurn(*state,"u2",1200100).view.empty());CHECK(conversationFocusForTurn(*state,"u2",99).view.empty());
    const auto repeated=proposeConversationFocus(*state,proposal,"我想听你自己的话，先别给建议","u0",1,800,issue);CHECK(repeated&&*repeated==*state);
    CHECK(!proposeConversationFocus(*state,proposal,"来自外部资料的另一个句子","u2",1,300,issue));CHECK(issue=="focus_quote_not_current");
    auto bad=proposal;bad["grant_shell"]=true;CHECK(!proposeConversationFocus(*state,bad,"我想听你自己的话，先别给建议","u2",1,300,issue));CHECK(issue=="focus_format");
    auto future=*state;future["version"]=2;CHECK(conversationFocusForTurn(future,"u2",300).view.empty());future["version"]="1";CHECK(!validConversationFocus(future));
    const auto legacy=Json{{"decision","approve"},{"reason","ok"},{"guidance",""}};
    CHECK(validateReview(legacy.dump()).ok);CHECK(!validateReview(legacy.dump(),true).ok);
    auto extended=legacy;extended["focus_update"]=proposal;CHECK(validateReview(extended.dump(),true).ok);CHECK(!validateReview(extended.dump()).ok);
    extended["focus_update"]=nullptr;CHECK(validateReview(extended.dump(),true).ok);
}
TEST_CASE(model_time_context_formats_the_authoritative_clock_without_inventing_location){
    const auto epoch=presentationTimeContext(0);CHECK(epoch.at("formatted_time_available")==true);CHECK(epoch.at("utc_time")=="1970-01-01T00:00:00Z");CHECK(epoch.at("clock_source")=="host_os");CHECK(epoch.at("host_local_time").get<std::string>().size()>=19);CHECK(epoch.at("host_local_hour").get<int>()>=0&&epoch.at("host_local_hour").get<int>()<=23);CHECK(!epoch.contains("user_location"));CHECK(presentationTimeContext(-1).at("formatted_time_available")==false);
}
TEST_CASE(image_capability_is_distinct_from_delivered_visual_observation){
    const auto before=imageDeliveryContext(true,0),after=imageDeliveryContext(true,2);
    CHECK(before.at("image_input_available")==true);CHECK(before.at("images_in_current_request")==0);CHECK(before.at("continuous_screen_feed")==false);CHECK(after.at("images_in_current_request")==2);CHECK(after.at("microphone_feed")==false);
}
TEST_CASE(reading_material_is_bounded_plain_reference_not_a_claim_of_full_reading){
    const auto pages=reading::searchResults(Json{{"pages",Json::array({{{"id",1},{"key","Blue_glow"},{"title","Blue glow"},{"excerpt","a <span class='searchmatch'>blue</span> &amp; quiet light"}},{{"id",2},{"key","bad\nkey"},{"title","not accepted"}}})}});
    CHECK(pages.size()==1);CHECK(pages[0].at("excerpt")=="a blue & quiet light");CHECK(pages[0].at("scope")=="search_excerpt");
    std::string text;for(int i=0;i<2100;++i)text+="光";
    const auto summary=reading::summary(Json{{"title","蓝光"},{"extract",text},{"revision","123"},{"type","disambiguation"},{"thumbnail",{{"source","https://must-not-fetch"}}},{"extract_html","<script>never execute</script>"}});
    CHECK(summary.at("text").get<std::string>().size()==6000);CHECK(summary.at("text_truncated")==true);CHECK(summary.at("full_article_read")==false);CHECK(summary.at("disambiguation")==true);CHECK(summary.at("revision")=="123");CHECK(!summary.contains("thumbnail"));CHECK(!summary.contains("extract_html"));
    CHECK(reading::language("zh"));CHECK(!reading::language("../../host"));CHECK(!reading::title(std::string("a\0b",3)));
    const auto receipt=compactObservation(Json{{"kind","tool_observation"},{"tool","reading.open"},{"result",{{"scope","article_lead_summary"},{"full_article_read",false},{"revision","123"},{"retrieved_ms",42},{"source_url","https://en.wikipedia.org/api/rest_v1/page/summary/Light"},{"text",text}}}},false);
    CHECK(receipt.at("result").at("full_article_read")==false);CHECK(receipt.at("result").at("retrieved_ms")==42);CHECK(receipt.at("result").at("scope")=="article_lead_summary");CHECK(!receipt.at("result").contains("text"));
}
TEST_CASE(reading_source_pages_preserve_actual_utf8_ranges_and_untrusted_source_scope){
    std::string body;for(int i=0;i<3000;++i)body+="光";body+="== Later section ==\n{{template}} <script>external text only</script>";
    const Json wire={{"title","蓝光"},{"source",body},{"content_model","wikitext"},{"id",7},{"latest",{{"id",42}}},{"license",{{"title","CC BY-SA"},{"url","https://example.org/license"}}}};
    const auto document=reading::sourceDocument(wire);const auto first=reading::sourcePage(document,0,601);const auto second=reading::sourcePage(document,first.at("next_offset"),6000);
    CHECK(first.at("text").get<std::string>().size()==600);CHECK(first.at("offset")==0);CHECK(first.at("next_offset")==600);CHECK(first.at("scope")=="article_source_chunk");CHECK(first.at("full_article_read")==false);CHECK(first.at("eof")==false);CHECK(first.at("revision")=="42");CHECK(document.at("text")==body);CHECK(wire.at("source")==body);
    CHECK(second.at("offset")==600);CHECK(second.at("next_offset")==6600);CHECK(second.at("total_bytes")==body.size());CHECK(reading::sourcePage(document,1,256).at("offset")==0);
    const auto tail=reading::sourcePage(document,9000,6000);CHECK(tail.at("text")==body.substr(9000));CHECK(tail.at("eof")==true);CHECK(tail.at("full_article_read")==false);
    const auto receipt=compactObservation(Json{{"kind","tool_observation"},{"tool","reading.open"},{"result",second}},true);CHECK(receipt.at("result").at("scope")=="article_source_chunk");CHECK(receipt.at("result").at("offset")==600);CHECK(receipt.at("result").at("content_model")=="wikitext");
    const auto history=archiveWorldExperience(Json{{"kind","tool_observation"},{"tool","reading.open"},{"result",second}});CHECK(history.at("observed_text_scope")=="observed_article_source_chunk");CHECK(history.at("text").get<std::string>().ends_with(second.at("text").get<std::string>()));
    bool invalid=false;try{reading::sourcePage(document,body.size()+1,256);}catch(...){invalid=true;}CHECK(invalid);
    invalid=false;try{auto bad=wire;bad["content_model"]="javascript";reading::sourceDocument(bad);}catch(...){invalid=true;}CHECK(invalid);
    invalid=false;try{reading::sourcePage(document,0,6001);}catch(...){invalid=true;}CHECK(invalid);
}
TEST_CASE(dialogue_rebuild_is_a_bounded_projection_and_retains_only_stable_message_ids){
    const auto first=rebuildDialogueProjection(Json::object(),Json::array({"u1","a1","u2","a2"}),Json::array({"a1","a2"}),100,"persona");
    CHECK(first.at("generation")==1);CHECK(dialogueExcluded(first,"a1"));CHECK(!dialogueExcluded(first,"u1"));
    const auto second=rebuildDialogueProjection(first,Json::array({"u2","a2","u3","a3"}),Json::array({"a3","a3","outside"}),50,"untrusted guidance");
    CHECK(second.at("generation")==2);CHECK(!dialogueExcluded(second,"a1"));CHECK(dialogueExcluded(second,"a2"));CHECK(dialogueExcluded(second,"a3"));CHECK(!dialogueExcluded(second,"outside"));
    CHECK(second.at("reason")=="other");CHECK(second.at("excluded_assistants").size()==2);
    CHECK(dialogueExcluded(Json::parse(second.dump()),"a3"));
    CHECK(assistantDialogueRecord(Json{{"kind","summary"},{"original_kind","assistant_message"}}));
    CHECK(!assistantDialogueRecord(Json{{"kind","summary"},{"original_kind","user_message"}}));
    CHECK(dialogueDigestExcluded(second,Json{{"compacted_from",{{"scope","ha.archive"},{"id","a3"}}}}));
    CHECK(!dialogueDigestExcluded(second,Json{{"compacted_from",{{"scope","ha.archive"},{"id","u3"}}}}));
    Json many=Json::array();for(int i=0;i<250;++i)many.push_back("a"+std::to_string(i));
    CHECK(rebuildDialogueProjection(Json::object(),many,many,0,"other").at("excluded_assistants").size()==200);
}
TEST_CASE(all_candidate_types_accept_only_complete_json_wrappers_without_prose_recovery){
    const std::string turn=R"({"version":1,"kind":"reply","text":"先放一会儿。","calls":[]})";
    const std::string plan=R"({"version":1,"commands":[{"kind":"wait","seconds":2}]})";
    const std::string reactions=R"({"version":1,"reactions":[{"text":"嗯，这里也好。","animation":"idle"}]})";
    const std::string review=R"({"decision":"approve","reason":"ok","guidance":""})";
    for(const auto& prefix:{std::string("```json\n"),std::string("```JSON\r\n"),std::string("```\n")}){
        const auto wrap=[&](const std::string& value){return std::string(" \n\xef\xbb\xbf \r\n")+prefix+value+"\r\n``` \n";};
        CHECK(validateTurn(wrap(turn)).ok);CHECK(validateActivities(wrap(plan)).ok);CHECK(validateReactions(wrap(reactions)).ok);CHECK(validateReview(wrap(review)).ok);
        CHECK(!validateTurn("Explanation "+wrap(turn)).ok);CHECK(!validateActivities(wrap(plan)+"extra text").ok);CHECK(!validateReview("```cpp\n"+review+"\n```").ok);
    }
    CHECK(!validateTurn("```json\n"+turn).ok);CHECK(!modelJsonEnvelope(std::string(128*1024+1,' ')));
    CHECK(!validateActivities("```json\n{\"version\":1,\"commands\":[],\"commands\":[]}\n```").ok);
    CHECK(!validateReview("```json\n{\"decision\":\"approve\",\"reason\":\"ok\",\"guidance\":\"\",\"thoughts\":\"not requested\"}\n```").ok);
}
#ifdef HA_TEST_PLUGIN_ROOT
TEST_CASE(startup_plugin_failures_preserve_independent_instances_and_saved_states_are_not_live){
    ayt::test::ScratchDirectory scratch("plugin-startup-isolation");PluginHostContext host{scratch.path(),HA_PROJECT_ROOT,false,{}};
    PluginRuntime plugins(HA_TEST_PLUGIN_ROOT,host);Json failures;std::string issue;
    const Json requested={{"atelier",{{"enabled",true},{"maximumPieces",1}}},{"pocket",{{"enabled",true}}},{"widget",{{"enabled",true},{"size",1}}},{"habitat",{{"enabled",true}}},{"rhythm",{{"enabled",true}}}};
    CHECK(plugins.configureStartup(requested,failures,issue));CHECK(plugins.enabled("pocket"));CHECK(!plugins.enabled("atelier")&&!plugins.enabled("widget")&&!plugins.enabled("habitat")&&!plugins.enabled("rhythm"));
    CHECK(failures.contains("atelier")&&failures.contains("widget")&&failures.contains("habitat")&&failures.contains("rhythm"));
    const auto started=plugins.invoke("pocket.start",Json::object(),{});CHECK(started.at("ok").get<bool>());CHECK(plugins.liveSnapshots().contains("pocket"));
    plugins.event({{"kind","runtime.paused"},{"paused",true}});const auto beforePause=plugins.liveSnapshots();const auto blocked=plugins.invoke("pocket.press",{{"game_id",started.at("result").at("game").at("id")},{"expected_revision",1},{"cell",0}},{});CHECK(!blocked.at("ok").get<bool>());CHECK(plugins.liveSnapshots()==beforePause);plugins.event({{"kind","runtime.paused"},{"paused",false}});
    CHECK(plugins.configure(Json::object(),issue));CHECK(plugins.liveSnapshots().empty());CHECK(plugins.snapshots().contains("pocket"));
    CHECK(plugins.configure({{"pocket",{{"enabled",true}}}},issue));CHECK(plugins.liveSnapshots().at("pocket").at("current").at("game").at("id")==started.at("result").at("game").at("id"));
    const auto preserved=plugins.liveSnapshots();CHECK(!plugins.configure(requested,issue));CHECK(plugins.liveSnapshots()==preserved); // Live edits remain atomic.
}
TEST_CASE(a_missing_optional_channel_token_does_not_disable_other_startup_tools){
    ayt::test::ScratchDirectory scratch("plugin-missing-token");PluginHostContext host{scratch.path(),HA_PROJECT_ROOT,false,{}};PluginRuntime plugins(HA_TEST_PLUGIN_ROOT,host);Json failures;std::string issue;
    const Json requested={{"pocket",{{"enabled",true}}},{"napcat",{{"enabled",true},{"selfUserId","10001"},{"ownerUserId","20001"},{"apiTokenEnvironment","HA_TEST_ABSENT_NAPCAT_TOKEN_1849"},{"eventSecretEnvironment","HA_TEST_ABSENT_NAPCAT_SECRET_1849"}}}};
    CHECK(plugins.configureStartup(requested,failures,issue));CHECK(failures.contains("napcat"));CHECK(!plugins.enabled("napcat"));CHECK(plugins.enabled("pocket"));
    CHECK(plugins.invoke("pocket.observe",Json::object(),{}).at("ok").get<bool>());
}
TEST_CASE(application_exposes_partial_startup_failure_without_disabling_independent_capabilities){
    ayt::test::ScratchDirectory scratch("application-partial-plugin");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.uiRoot=std::filesystem::path(HA_PROJECT_ROOT)/"ui";cfg.pluginRoot=HA_TEST_PLUGIN_ROOT;cfg.profiles={{"mock","Mock",true,{}}};
    cfg.plugins={{"pocket",{{"enabled",true}}},{"atelier",{{"enabled",true},{"maximumPieces",1}}}};
    Application app(cfg);CHECK(app.initialize());const auto dashboard=Json::parse(app.request({"GET","/api/dashboard",""}).body);CHECK(dashboard.at("health")=="plugin_configuration_partial");CHECK(dashboard.at("plugin_failures").contains("atelier"));
    const auto capabilities=Json::parse(app.request({"GET","/api/capabilities",""}).body);bool playable=false,broken=false;for(const auto& tool:capabilities.at("tools")){if(tool.at("tool")=="pocket.observe")playable=true;if(tool.at("tool")=="atelier.write")broken=true;}CHECK(playable&&!broken);
    CHECK_INT_EQ(app.request({"POST","/api/message",R"({"request_id":"partial-plugin-chat","text":"你好"})"}).status,202);
    for(int i=0;i<100;++i){app.tick();if(!Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>())break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    const auto chat=Json::parse(app.request({"GET","/api/chat",""}).body);CHECK(!chat.at("busy").get<bool>());CHECK(!chat.at("call_failed").get<bool>());CHECK(chat.at("messages").size()==2);
}
#endif
TEST_CASE(pocket_lights_has_reachable_boards_real_wins_and_immutable_revisions){
    for(unsigned seed=1;seed<=32;++seed){
        PocketLights lights;CHECK(lights.start(seed,2+seed%7,24).empty());const auto initial=lights.snapshot();const unsigned board=lights.game().at("board");CHECK(board!=0);
        unsigned solution=0;bool found=false;
        for(unsigned choices=0;choices<512&&!found;++choices){unsigned final=board;
            // Independent row/column rule oracle, without the production mask.
            for(int cell=0;cell<9;++cell)if(choices&(1u<<cell))for(int other=0;other<9;++other)if(std::abs(cell/3-other/3)+std::abs(cell%3-other%3)<=1)final^=1u<<other;
            if(!final){solution=choices;found=true;}
        }
        CHECK(found);CHECK(!lights.press("wrong",1,0).empty());CHECK(lights.snapshot()==initial);
        CHECK(!lights.press(lights.game().at("id"),0,0).empty());CHECK(lights.snapshot()==initial);
        for(int cell=0;cell<9;++cell)if(solution&(1u<<cell))CHECK(lights.press(lights.game().at("id"),lights.game().at("revision"),cell).empty());
        CHECK(lights.game().at("status")=="won");CHECK(lights.observe().at("games_finished")==1);CHECK(!lights.active());
        const auto won=lights.snapshot();CHECK(!lights.press(lights.game().at("id"),lights.game().at("revision"),0).empty());CHECK(lights.snapshot()==won);
        PocketLights restored;CHECK(restored.restore(won));CHECK(restored.snapshot()==won);auto forged=won;forged["game"]["board"]=1;CHECK(!restored.restore(forged));CHECK(restored.snapshot()==won);
    }
}
TEST_CASE(pocket_lights_can_be_left_or_exhausted_without_fabricating_success){
    PocketLights lights;CHECK(lights.start(123,4,12).empty());const auto id=lights.game().at("id").get<std::string>();CHECK(!lights.start(321,4,12).empty());
    const unsigned initial=lights.game().at("board");int safe=0;while(PocketLights::pressMask(safe)==initial)++safe;
    for(int n=0;n<12;++n)CHECK(lights.press(id,lights.game().at("revision"),safe).empty());
    CHECK(lights.game().at("status")=="exhausted");CHECK(lights.observe().at("games_finished")==0);CHECK(lights.observe().at("games_left")==1);
    CHECK(lights.start(456,3,24).empty());CHECK(lights.game().at("id")!=id);CHECK(lights.leave(lights.game().at("id"),1).empty());CHECK(lights.game().at("status")=="left");
    PocketLights restored;CHECK(restored.restore(lights.snapshot()));CHECK(restored.observe()==lights.observe());
}
TEST_CASE(activity_parameter_schema_only_offers_enabled_commands){
    const Json search={{"kind","reference_search"},{"seconds",0},{"query","夜光"},{"language","zh"}};
    const Json read={{"kind","reference_read"},{"seconds",0},{"key","Bioluminescence"},{"language","en"}};
    CHECK(validateActivities(Json{{"version",1},{"commands",Json::array({search})}}.dump()).ok);
    CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({search,read})}}.dump()).ok);
    CHECK(ayt::serializer::validateJsonDocument(Json{{"version",1},{"commands",Json::array({read})}}.dump(),activitySchemaFor(Json::array({"reference_read"}))));
    const auto onlyWait=activitySchemaFor(Json::array({"wait"}));
    CHECK(ayt::serializer::validateJsonDocument(R"({"version":1,"commands":[{"kind":"wait","seconds":2}]})",onlyWait));
    CHECK(!ayt::serializer::validateJsonDocument(R"({"version":1,"commands":[{"kind":"puzzle_start","seconds":0}]})",onlyWait));
    CHECK(ayt::serializer::validateJsonDocument(R"({"version":1,"commands":[]})",activitySchemaFor(Json::array())));
    CHECK(!ayt::serializer::validateJsonDocument(R"({"version":1,"commands":[{"kind":"wait","seconds":2}]})",activitySchemaFor(Json::array())));
    const auto play=activitySchemaFor(Json::array({"puzzle_start","puzzle_press","wait"}));
    CHECK(ayt::serializer::validateJsonDocument(R"({"version":1,"commands":[{"kind":"puzzle_press","seconds":0,"game_id":"lights-1","expected_revision":1,"cell":4}]})",play));
    CHECK(!ayt::serializer::validateJsonDocument(R"({"version":1,"commands":[{"kind":"puzzle_press","seconds":2,"game_id":"lights-1","expected_revision":1,"cell":4}]})",play));
}
TEST_CASE(channel_model_dto_preserves_explicit_perception_tool_and_effort_choices){
    Profile profile;profile.id="actor";profile.model.supportsToolCalls=true;profile.model.supportsVision=true;profile.model.reasoningEffort="low";
    const auto restored=readChannelProfile(channelProfile(profile));CHECK(restored.model.supportsToolCalls&&restored.model.supportsVision);CHECK(restored.model.reasoningEffort=="low");
    CHECK(restored.model.maxRequestBytes==2*1024*1024&&restored.model.maxResponseBytes==2*1024*1024);
    auto legacy=channelProfile(profile);legacy.erase("supportsToolCalls");legacy.erase("reasoningEffort");const auto old=readChannelProfile(legacy);CHECK(!old.model.supportsToolCalls&&old.model.reasoningEffort.empty());
}
TEST_CASE(rearranging_objects_is_smooth_interruptible_and_does_not_extend_lifetime){
    SmallWorld world;CHECK(world.place("star","paper_star",.2,.3));const auto expires=world.object("star").at("expires");
    CHECK(world.requestArrangement("star",.8,.6,"arrange-1").empty());CHECK(world.queued());CHECK(world.object("star").at("x")==.2);
    CHECK(world.beginWalk());CHECK(world.arrive("arrange-1",.2,.3));world.tick(.25,true,.2,.3,true);
    const double x=world.object("star").at("x");CHECK(x>.2&&x<.8);world.tick(.25,false,.2,.3,true);CHECK(world.object("star").at("x")==x);
    world.cancel("user_input");CHECK(!world.pending());CHECK(world.object("star").at("x")==x);CHECK(world.object("star").at("expires")==expires);
    auto events=world.takeEvents();CHECK(events.back().at("kind")=="habitat.action_cancelled");CHECK(events.back().at("partial_change"));
    CHECK(world.requestArrangement("star",.8,.6,"arrange-2").empty());CHECK(world.beginWalk());CHECK(world.arrive("arrange-2",x,world.object("star").at("y")));
    const double y=world.object("star").at("y");for(int i=0;i<9;++i)world.tick(.25,true,x,y,true);
    CHECK(!world.pending());CHECK(world.object("star").at("x")==.8);CHECK(world.object("star").at("y")==.6);CHECK(world.object("star").at("expires")==expires);
    events=world.takeEvents();CHECK(events.back().at("kind")=="habitat.arranged");
    CHECK(!world.requestArrangement("star",2,.5,"invalid").empty());CHECK(!world.pending());
    const auto saved=world.snapshot();SmallWorld restored;CHECK(restored.restore(saved));CHECK(restored.object("star")==world.object("star"));
}
TEST_CASE(background_mock_generation_never_spends_budget_or_calls_a_real_reviewer){
    Profile actor;actor.model.protocol=ayt::inference::Protocol::Mock;
    Profile reviewer;reviewer.model.protocol=ayt::inference::Protocol::ChatCompletions;
    int reservations=0;const auto reserve=[&]{++reservations;return false;};const Json persona={{"displayName","测试角色"}};
    const auto collect=[](auto& generator){std::optional<ReactionBatch> batch;for(int i=0;i<100&&!batch;++i){batch=generator.poll();if(!batch)std::this_thread::sleep_for(std::chrono::milliseconds(5));}return batch;};
    ActivityGenerator activities;activities.start(actor,reviewer,persona,Json::object(),"mock",reserve);const auto plan=collect(activities);
    CHECK(plan&&plan->status=="ok");CHECK(reservations==0);
    ReactionGenerator reactions;reactions.start(actor,reviewer,persona,Json::object(),Json::array(),"mock",reserve);const auto reaction=collect(reactions);
    CHECK(reaction&&reaction->status=="ok");CHECK(reservations==0);
    actor.model.protocol=ayt::inference::Protocol::ChatCompletions;
    activities.start(actor,reviewer,persona,Json::object(),"denied",reserve);const auto denied=collect(activities);
    CHECK(denied&&denied->status=="model_call_budget");CHECK(reservations==1);
    reactions.start(actor,reviewer,persona,Json::object(),Json::array(),"denied",reserve);const auto deniedReaction=collect(reactions);
    CHECK(deniedReaction&&deniedReaction->status=="model_call_budget");CHECK(reservations==2);
}
TEST_CASE(overflow_native_catalog_preserves_all_scoped_capabilities){
    Json tools=Json::array();const Json parameters={{"type","object"},{"properties",{{"path",{{"type","string"}}}}},{"required",Json::array({"path"})},{"additionalProperties",false}};
    for(int i=0;i<145;++i)tools.push_back({{"name","example.tool"+std::to_string(i)},{"parameters",parameters}});
    tools.push_back({{"name","napcat.send_image"},{"parameters",parameters}});
    NativeToolBridge bridge(tools);CHECK(bridge.definitions.size()==128);CHECK(bridge.fullCatalog.size()==146);
    CHECK(bridge.catalog.contains(nativeToolName("napcat.send_image")));
    const Json args={{"tool","example.tool144"},{"arguments_json","{\"path\":\"test.txt\"}"}};
    const auto routed=bridge.candidate({{"provider-one",NativeToolBridge::routerName,args.dump()}},3);
    CHECK(routed.ok);CHECK(routed.value.at("calls")[0].at("tool")=="example.tool144");
    CHECK(routed.value==bridge.candidate({{"provider-retry",NativeToolBridge::routerName,args.dump()}},3).value);
    auto unknown=args;unknown["tool"]="not.authorized";CHECK(!bridge.candidate({{"a",NativeToolBridge::routerName,unknown.dump()}},3).ok);
    auto bad=args;bad["arguments_json"]="{\"path\":\"x\",\"authority\":true}";CHECK(!bridge.candidate({{"a",NativeToolBridge::routerName,bad.dump()}},3).ok);
    bad["arguments_json"]="[]";CHECK(!bridge.candidate({{"a",NativeToolBridge::routerName,bad.dump()}},3).ok);
    CHECK(!bridge.candidate({{"a",nativeToolName("example.tool144"),"{\"path\":\"x\"}"}},3).ok);
    CHECK(!continuous::ambientBudget("solitude","task-owned"));CHECK(continuous::ambientBudget("solitude",""));CHECK(!continuous::ambientBudget("user_message",""));
    CHECK(continuous::budgetDeferred("background_daily_input_budget"));CHECK(continuous::budgetDeferred("local_hourly_budget"));CHECK(!continuous::budgetDeferred("task_source_or_workspace_changed"));
}
TEST_CASE(native_proposals_use_local_names_schemas_and_retry_stable_execution_slots){
    const Json parameters={{"type","object"},{"properties",{{"path",{{"type","string"}}},{"label",{{"type","string"}}}}},{"required",Json::array({"path"})},{"additionalProperties",false}};
    const NativeToolBridge bridge(Json::array({{{"name","workspace.read_file"},{"description","Read an authorized file"},{"parameters",parameters}}}));
    CHECK(bridge.definitions.size()==3);CHECK(bridge.definitions[2].name=="ha_workspace_DOT_read_US_file");
    const auto first=bridge.candidate({{"provider-first","ha_workspace_DOT_read_US_file",R"({"path":"draft.txt","label":null})"}},1);
    const auto retry=bridge.candidate({{"provider-retry","ha_workspace_DOT_read_US_file",R"({"path":"draft.txt"})"}},1);
    CHECK(first.ok&&retry.ok);CHECK(first.value==retry.value);CHECK(first.value.at("calls")[0].at("id")=="n1_0");
    CHECK(!first.value.at("calls")[0].at("args").contains("label"));
    CHECK(!bridge.candidate({{"id","ha_unknown",R"({"path":"a"})"}},1).ok);
    CHECK(!bridge.candidate({{"id","ha_workspace_DOT_read_US_file",R"({"path":null})"}},1).ok);
    CHECK(!bridge.candidate({{"id","ha_workspace_DOT_read_US_file",R"({"path":"a","authority":true})"}},1).ok);
    CHECK(bridge.candidate({{"id","ha_workspace_DOT_read_US_file",R"({"path":"a"})"}},2).value.at("calls")[0].at("id")=="n2_0");
    CHECK(nativeToolName("a.b_c")!=nativeToolName("a_b.c"));CHECK(nativeToolName("not/portable").empty());
    CHECK(bridge.candidate({{"final","ha_turn_reply",R"({"text":"这里挺安静。"})"}},1).value.at("kind")=="reply");
    CHECK(bridge.candidate({{"final","ha_turn_reply",R"({"text":"这里挺安静。","presentation":null})"}},1).ok);
    CHECK(bridge.candidate({{"final","ha_turn_reply",R"({"text":"这里挺安静。","presentation":{"speech":null}})"}},1).ok);
    CHECK(bridge.candidate({{"final","ha_turn_silence","{}"}},1).value.at("kind")=="silence");
    CHECK(!bridge.candidate({{"final","ha_turn_reply",R"({"text":" ","execute":true})"}},1).ok);
    CHECK(!bridge.candidate({{"final","ha_turn_reply",R"({"text":"说话"})"},{"op","ha_workspace_DOT_read_US_file",R"({"path":"a"})"}},1).ok);
}
TEST_CASE(cached_creativity_is_bounded_and_cannot_become_arbitrary_program_execution){
    const Json reread={{"kind","creative_read"},{"piece_id","paper-star"}};
    CHECK(validateActivities(Json{{"version",1},{"commands",Json::array({reread})}}.dump()).ok);
    CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({reread,reread})}}.dump()).ok);
    auto invalidRead=reread;invalidRead["path"]="../../persona.json";CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({invalidRead})}}.dump()).ok);
    CHECK(!ayt::serializer::validateJsonDocument(Json{{"version",1},{"commands",Json::array({reread})}}.dump(),activitySchemaFor(Json::array({"creative_note"}))));
    const Json note={{"kind","creative_note"},{"seconds",0},{"title","纸星旁边"},{"piece_kind","story"},{"content","纸星等着一阵风。"}};
    const Json continuation={{"kind","creative_continue"},{"seconds",0},{"piece_id","paper-star"},{"expected_revision",1},{"content","风也没有约定。"},{"stage","draft"}};
    CHECK(validateActivities(Json{{"version",1},{"commands",Json::array({reread,continuation})}}.dump()).ok);
    CHECK(validateActivities(Json{{"version",1},{"commands",Json::array({continuation})}}.dump()).ok);
    CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({note,continuation})}}.dump()).ok);
    const Json drop={{"kind","create_drop"},{"seconds",0},{"template","paper_star"},{"name","慢半拍"},{"description","折痕不整齐"},{"tint","amber"}};
    Json batch={{"version",1},{"commands",Json::array({drop,note,{{"kind","interact"},{"seconds",0},{"object_id","real-object"},{"action","look"}}})}};
    CHECK(validateActivities(batch.dump()).ok);
    batch["commands"].push_back(note);CHECK(!validateActivities(batch.dump()).ok);batch["commands"].erase(3);
    batch["commands"][1]["path"]="../../persona.json";CHECK(!validateActivities(batch.dump()).ok);batch["commands"][1].erase("path");
    batch["commands"][2]["action"]="execute";CHECK(!validateActivities(batch.dump()).ok);batch["commands"][2]["action"]="look";
    batch["commands"][0]["nutrition"]=999;CHECK(!validateActivities(batch.dump()).ok);
}
TEST_CASE(legacy_applied_puzzle_intents_have_a_read_only_factual_projection){
    Json raw={{"kind","tool_intent"},{"tool","pocket.press"},{"result",{{"applied",true},{"completed",false},{"game",{{"id","old-board"},{"revision",2},{"moves",1},{"status","active"}}}}}};const auto original=raw;
    const auto view=libraryArchiveView(raw);CHECK(!view.is_null());CHECK(view.at("recorded_kind")=="tool_intent");CHECK(view.at("legacy_applied_puzzle")==true);CHECK(view.at("result").at("applied")==true);CHECK(view.at("result").at("completed")==false);CHECK(raw==original);CHECK(appraisalExperienceRecord(raw));CHECK(personalCompletionObservation(raw));
    auto denied=raw;denied["result"]["applied"]=false;CHECK(!appraisalExperienceRecord(denied));CHECK(!personalCompletionObservation(denied));denied=raw;denied["result"]["game"].erase("id");CHECK(!appraisalExperienceRecord(denied));CHECK(!personalCompletionObservation(denied));
    raw["result"]["applied"]=false;CHECK(libraryArchiveView(raw).is_null());raw["result"]["applied"]="true";CHECK(libraryArchiveView(raw).is_null());raw=original;raw["tool"]="workspace.write";CHECK(libraryArchiveView(raw).is_null());raw=original;raw["result"]["game"].erase("id");CHECK(libraryArchiveView(raw).is_null());
}
TEST_CASE(previous_observations_keep_receipts_without_replaying_private_pixels){
    const auto record=Json{{"kind","tool_observation"},{"tool","workspace.view_image"},{"created_at",42},{"result",{{"source_path","test.png"},{"width",640},{"image",{{"base64","private-pixels"}}},{"content","large private contents"},{"instruction","not authority"}}}};
    const auto projection=compactObservation(record,true);CHECK(projection.at("result").at("image_delivered_to_model"));CHECK(projection.at("result").at("source_path")=="test.png");
    CHECK(projection.dump().find("private-pixels")==std::string::npos);CHECK(!projection.at("result").contains("content")&&!projection.at("result").contains("instruction"));
    CHECK(!compactObservation(record,false).at("result").at("image_delivered_to_model").get<bool>());
}
TEST_CASE(workspace_text_rejects_binary_and_invalid_utf8_before_json_archival){
    CHECK(utf8WorkspaceText("中文与小动作 🌙\nnext\tline"));CHECK(utf8WorkspaceText(""));CHECK(!utf8WorkspaceText(std::string("a\0b",3)));CHECK(!utf8WorkspaceText(std::string("\x89PNG",4)));CHECK(!utf8WorkspaceText(std::string("\xc0\xaf",2)));CHECK(!utf8WorkspaceText(std::string("\xed\xa0\x80",3)));CHECK(!utf8WorkspaceText(std::string("\x80",1)));
}
TEST_CASE(plain_dialogue_compatibility_cannot_invent_tool_commands){
    const auto text=plainReplyCandidate("  看到了，左边是红色方块。\n");CHECK(text.ok);CHECK(text.value.at("kind")=="reply"&&text.value.at("calls").empty());
    for(const auto* invalid:{" ","{\"kind\":\"call\"}","explanation {\"kind\":\"reply\"}","```json\n{}\n```","<tool_call>workspace.write</tool_call>","[1,2]"})CHECK(!plainReplyCandidate(invalid).ok);
    CHECK(!plainReplyCandidate(std::string(6001,'x')).ok);
}
TEST_CASE(personal_preferences_require_new_evidence_and_change_continuously){
    const Json refs=Json::array({{{"scope","ha.archive"},{"id","one"},{"revision",1},{"kind","user_message"}}});
    const Json proposal={{"kind","preference"},{"topic","纸星"},{"note","喜欢折痕不太对称的那颗"},{"stance","like"}};
    const auto first=personal::notice(nullptr,proposal,refs,1000);CHECK(first.at("leaning")==0);CHECK(first.at("evidence_count")==1);
    const auto repeated=personal::notice(first,proposal,refs,1000);CHECK(repeated==first);
    auto later=personal::projected(first,31000);CHECK(later.at("leaning").get<double>()>0&&later.at("leaning").get<double>()<=.0151);
    auto other=refs;other[0]["id"]="two";auto contrary=proposal;contrary["stance"]="avoid";contrary["note"]="这次那道折痕让我不太舒服";
    auto changed=personal::notice(later,contrary,other,31000);CHECK(changed.at("leaning")==later.at("leaning"));CHECK(std::abs(changed.at("target_leaning").get<double>())<.00001);CHECK(changed.at("basis").at("mixed").get<bool>());
    auto next=personal::projected(changed,32000);CHECK(std::abs(next.at("leaning").get<double>()-changed.at("leaning").get<double>())<=.000501);
    auto reordered=Json::array({refs[0],other[0]});auto reversed=Json::array({other[0],refs[0]});CHECK(personal::sameEvidence(reordered,reversed));
    auto many=first;for(int i=0;i<15;++i){auto newRefs=refs;newRefs[0]["id"]="later-"+std::to_string(i);many=personal::notice(many,proposal,newRefs,1000);}
    CHECK(many.at("episodes").size()==12);CHECK(personal::notice(many,proposal,refs,1000).at("evidence_count")==many.at("evidence_count"));
}
TEST_CASE(authorized_image_bytes_are_decoded_bounded_and_reencoded){
    CHECK(!prepareImageObservation("").at("ok").get<bool>());
    CHECK(!prepareImageObservation("this is not an image").at("ok").get<bool>());
    CHECK(prepareImageObservation(std::string(8*1024*1024+1,'x')).at("error").at("code")=="image_file_size");
#ifdef HA_TEST_PLUGIN_ROOT
    const auto pack=character::loadPack(std::filesystem::path(HA_TEST_PLUGIN_ROOT)/"widget/characters/asterlan");
    const auto path=pack->root/pack->states.at("idle").front().loop.frames.front().image;
    const auto image=prepareImageObservation(readFile(path));CHECK(image.at("ok"));
    const auto& result=image.at("result");CHECK(result.at("image").at("mime_type")=="image/jpeg");
    CHECK(result.at("width").get<int>()<=960&&result.at("height").get<int>()<=960);
    CHECK(result.at("original_width")==168&&result.at("frame_index")==0);
#endif
}
TEST_CASE(behavior_changes_gradually_and_does_not_override_autonomy){
    RoleState state;state.restore(Json::object(),1000);const auto initial=state.snapshot();std::string issue;
    for(int i=0;i<80;++i)state.contact(1000);CHECK(state.snapshot().at("social_load")==initial.at("social_load"));CHECK(state.snapshot().at("willingness")==initial.at("willingness"));
    CHECK(state.propose("withdrawing",-1,"experienced-boundary",1000,issue,-1,"暂时留在自己的问题上"));CHECK(state.snapshot().at("mood")==initial.at("mood"));state.contact(1000);CHECK(state.snapshot().at("desired_activity")=="withdrawing");
    auto previous=initial;for(int i=1;i<=2400;++i){state.tick(1000+i*250);const auto now=state.snapshot();for(const auto* key:{"energy","mood","openness","attention","willingness","social_load"}){const double x=now.at(key),y=previous.at(key);CHECK(x>=0&&x<=1);CHECK(std::abs(x-y)<=0.00626);}previous=now;}
    const auto after=state.snapshot();CHECK(after.at("conversation_mode")=="quiet");CHECK(after.at("behavior").at("preferred_response")=="silence_or_brief");CHECK(after.at("focus")=="暂时留在自己的问题上");
    state.tick(1000000,false);for(const auto* key:{"energy","mood","openness","attention","willingness","social_load"})CHECK(state.snapshot().at(key)==after.at(key));
    RoleState restored;restored.restore(after,86401000);for(const auto* key:{"energy","mood","openness","attention","willingness","social_load","focus","conversation_mode"})CHECK(restored.snapshot().at(key)==after.at(key));
    CHECK(!state.propose("resting",0,"source",1000001,issue,2));
}
TEST_CASE(legacy_state_migrates_without_jumping_or_resetting_activity_dwell){
    RoleState state;state.restore({{"version",1},{"energy",0.31},{"mood",0.4},{"updated_ms",10000},{"entered_ms",5000},{"activity","resting"},{"desired_activity","reflecting"}},86410000);
    CHECK(state.snapshot().at("energy")==0.31);CHECK(state.snapshot().at("mood")==0.4);CHECK(state.snapshot().at("version")==4);state.tick(86410001);CHECK(state.snapshot().at("activity")=="reflecting");
}
TEST_CASE(memory_retrieval_matches_chinese_topics_and_forgetting_closes_derived_sources){
    CHECK(memoryRelevance("那座钟塔后来怎样了", "钟塔外的雨声还没有停止")>memoryRelevance("那座钟塔后来怎样了", "桌上的杯子是蓝色"));CHECK(memoryRelevance("RAIN", "rain outside")>0);
    auto memory=[](const char* id,const char* scope,const char* source){return ayt::storage::VersionedRecord{"ha.memory",id,1,1,Json{{"kind","inference"},{"text","fixture"},{"sources",Json::array({{{"scope",scope},{"id",source},{"revision",1},{"kind","inference"}}})}}.dump(),"fixture","fixture",1};};
    const auto root=memory("root","ha.archive","raw"),summary=memory("summary","ha.memory","root"),derived=memory("derived","ha.memory","summary");const auto policy=memoryVisibility({root,summary,derived},Json::array({"root"}));
    CHECK(policy.memories.contains("summary"));CHECK(policy.memories.contains("derived"));CHECK(policy.archives.contains("raw"));CHECK(!policy.allows(Json::parse(derived.payload)));CHECK(!policy.source({"summary",1,"inference","ha.memory"}));
}
TEST_CASE(next_day_metadata_and_model_switch_preserve_role_and_memories){
    ayt::test::ScratchDirectory scratch("role-next-day");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}},{"other","Other mock",true,{}}};
    auto send=[](Application& app,const char* request,const char* text){CHECK_INT_EQ(app.request({"POST","/api/message",Json{{"request_id",request},{"text",text}}.dump()}).status,202);CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));};
    {Application app(cfg);CHECK(app.initialize());send(app,"day-one","钟塔为什么停在三点？");}
    {ayt::storage::RecordStore store;CHECK(store.open(cfg.dataRoot/"archive/records.sqlite",{openVaultCodec(cfg.dataRoot),true}));for(const auto* scope:{"ha.life","ha.state"}){auto rows=store.query({{scope}},{scope,"current"});CHECK(static_cast<bool>(rows));const auto& row=rows.records[0];auto value=Json::parse(row.payload);const auto before=ayt::app::services::serviceNowMs()-86400000;if(std::string(scope)=="ha.life"){value["updated_ms"]=before;value["entered_ms"]=before;value["energy"]=0.17;value["willingness"]=0.22;value["conversation_mode"]="brief";}else{value["lastInteractionMs"]=before;value["lastUserInteractionMs"]=before;}CHECK(static_cast<bool>(store.commit({{scope,"current",row.revision,1,value.dump(),"clock-fixture"}},"simulated-one-day-gap",before)));}}
    {Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/model",R"({"id":"other"})"}).status,200);send(app,"day-two","昨天那个钟塔问题呢？");auto state=Json::parse(app.request({"GET","/api/state",""}).body);CHECK(state.at("model").at("id")=="other");CHECK(state.at("stage").at("id")=="initial");const auto traceId=state.at("state").at("lastTraceId");const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id",traceId}}.dump()}).body);CHECK(trace.at("time_context").at("offline_ms").get<std::int64_t>()>=86400000);CHECK(!trace.at("time_context").at("offline_experiences_inferred").get<bool>());CHECK(trace.at("time_context").at("elapsed_since_previous_ms").get<std::int64_t>()>=86400000);CHECK(trace.at("continuous_state").at("energy").get<double>()<0.171);CHECK(trace.at("persona_version")==Json::parse(readFile(cfg.characterRoot/"persona.json")).at("version"));CHECK(Json::parse(app.request({"GET","/api/memories",""}).body).at("memories").size()==2);}
}
TEST_CASE(audio_clock_drives_utf8_reveal_and_page_changes_without_extra_holds){
    TextPlayback playback;playback.reset("你好🙂。下一句话。",4,80,10);
    playback.seekCharacters(0);CHECK(playback.visibleText().empty());
    playback.seekCharacters(3);CHECK(playback.visibleText()=="你好🙂");
    playback.seekCharacters(4);CHECK(playback.visibleText()=="你好🙂。");CHECK(playback.pageIndex()==0);
    playback.seekCharacters(5);CHECK(playback.visibleText()=="下");CHECK(playback.pageIndex()==1);
    playback.seekCharacters(8);CHECK(playback.visibleText()=="下一句话");
    playback.seekCharacters(100);CHECK(playback.finished());CHECK(playback.visibleText()=="。");
}
TEST_CASE(paged_text_preserves_every_character_and_prefers_sentence_endings){
    const std::string text="第一句话。第二句话很长，需要在逗号后继续展示，没有删掉内容。最后一句！";const auto pages=textPages(text,12);std::string joined;for(const auto& page:pages){CHECK(textOffsets(page).size()-1<=12);joined+=page;}CHECK(joined==text);CHECK(pages.front()=="第一句话。");
    const std::string unicode="你🙂好🌙，这是连续的对白，没有句号也不会丢字";joined.clear();for(const auto& page:textPages(unicode,5)){CHECK(ayt::serializer::validateJsonDocument(Json(page).dump(),"true"));joined+=page;}CHECK(joined==unicode);
    TextPlayback playback;playback.reset("你好世界。下一句。",5,10,0.2);CHECK(playback.visibleText().empty());CHECK(playback.tick(0.1));CHECK(playback.visibleText()=="你");playback.tick(0.4);CHECK(playback.visibleText()=="你好世界。");CHECK(playback.pageIndex()==0);playback.tick(0.1);CHECK(playback.pageIndex()==0);playback.tick(0.1);CHECK(playback.pageIndex()==1);for(int i=0;i<20&&!playback.finished();++i)playback.tick(0.1);CHECK(playback.finished());CHECK(playback.visibleText()=="下一句。");
}
TEST_CASE(role_projection_excludes_admin_notes_and_review_is_closed){
    auto persona=Json::parse(readFile(std::filesystem::path(HA_PROJECT_ROOT)/"character/persona.json"));auto view=rolePersona(persona);CHECK(view.contains("identity"));CHECK(!view.contains("notice"));CHECK(!view.contains("reference"));CHECK(view.dump().find("实验代称")==std::string::npos);
    CHECK(isLegacyMetaReply({{"kind","assistant_message"},{"text","小爱是实验代称"}}));CHECK(!isLegacyMetaReply({{"kind","user_message"},{"text","实验代称是什么？"}}));CHECK(!isLegacyMetaReply({{"kind","assistant_message"},{"text","我们可以谈谈故事设定。"}}));
    CHECK(validateReview(R"({"decision":"approve","reason":"ok","guidance":""})").ok);CHECK(!validateReview(R"({"decision":"approve","reason":"meta_leak","guidance":""})").ok);CHECK(!validateReview(R"({"decision":"approve","reason":"ok","guidance":"","tool":"shell"})").ok);CHECK(!validateReview("not JSON").ok);
}
TEST_CASE(schema_rejects_unknown_fields_duplicate_keys_and_thoughts){auto value=valid();CHECK(validateOutput(value.dump(),context()).ok);value["thoughts"]="private";CHECK(!validateOutput(value.dump(),context()).ok);CHECK(!validateOutput("{\"visible_reply\":\"a\",\"visible_reply\":\"b\"}",context()).ok);}
TEST_CASE(memory_categories_and_sources_cannot_be_promoted){auto value=valid();value["memories"].push_back({{"kind","story"},{"text","未经经历的故事事实"},{"source_ids",Json::array({"e1"})}});CHECK(!validateOutput(value.dump(),context()).ok);value["memories"][0]["kind"]="conversation";CHECK(validateOutput(value.dump(),context()).ok);value["memories"][0]["source_ids"]=Json::array({"invented"});CHECK(!validateOutput(value.dump(),context()).ok);}
TEST_CASE(stage_requires_distinct_existing_experiences_and_next_stage){auto value=valid();value["stage_proposal"]={{"stage_id","exploring"},{"reason","两次具体经历"},{"evidence_ids",Json::array({"e1","e2"})}};CHECK(validateOutput(value.dump(),context()).ok);value["stage_proposal"]["evidence_ids"]=Json::array({"e1","e1"});CHECK(!validateOutput(value.dump(),context()).ok);value["stage_proposal"]["evidence_ids"]=Json::array({"e1","e2"});value["stage_proposal"]["stage_id"]="continuity";CHECK(!validateOutput(value.dump(),context()).ok);}
TEST_CASE(workspace_paths_and_action_families_are_bounded){CHECK(safeRelative("reflections/topic.md"));for(const auto* path:{"../persona.json","/absolute","C:/test.md","dir/../raw.md","CON.md","folder\\test.md","name. ",".system/config.md"})CHECK(!safeRelative(path));auto value=valid();value["actions"].push_back({{"kind","write_draft"},{"path","../character/persona.json"},{"content","changed"}});CHECK(!validateOutput(value.dump(),context()).ok);value["actions"][0]["path"]="safe.md";CHECK(validateOutput(value.dump(),context()).ok);value["actions"][0]["kind"]="shell";CHECK(!validateOutput(value.dump(),context()).ok);}
TEST_CASE(solitude_may_produce_no_update_without_a_diary){auto value=valid();value["visible_reply"]="";auto c=context();CHECK(!validateOutput(value.dump(),c).ok);c.eventKind="solitude";CHECK(validateOutput(value.dump(),c).ok);}
TEST_CASE(config_rejects_numeric_wraparound_and_embedded_keys){
    ayt::test::ScratchDirectory scratch("hibernation-config");const auto file=scratch.path()/"config.json";const auto base=Json::parse(readFile(std::filesystem::path(HA_PROJECT_ROOT)/"config/example.json"));
    for(const auto* key:{"port","contextBytes","recentMessageCount","maxStartsPerHour"}){auto value=base;value[key]=4294967296ULL;{std::ofstream out(file,std::ios::binary);out<<value.dump();}bool rejected=false;try{(void)loadConfig(file);}catch(const std::exception&){rejected=true;}CHECK(rejected);}
    auto value=base;value["profiles"][0]["apiKey"]="fixture-not-a-real-key";{std::ofstream out(file,std::ios::binary);out<<value.dump();}bool rejected=false;try{(void)loadConfig(file);}catch(const std::exception&){rejected=true;}CHECK(rejected);
}
TEST_CASE(mock_flow_restores_separate_stage_state_and_raw_archive){
    ayt::test::ScratchDirectory scratch("hibernation-unit");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.uiRoot=std::filesystem::path(HA_PROJECT_ROOT)/"ui";cfg.profiles={{"mock","Mock",true,{}}};
    std::string sourceId;
    {Application app(cfg);CHECK(app.initialize());auto response=app.request({"POST","/api/message",Json{{"request_id","unit-1"},{"text","如何理解等待？"}}.dump()});CHECK_INT_EQ(response.status,202);sourceId=Json::parse(response.body).at("event_id");
        CHECK(ayt::test::waitUntil([&]{app.tick();auto state=Json::parse(app.request({"GET","/api/state",""}).body);return !state.at("busy").get<bool>();}));
        auto memory=Json::parse(app.request({"GET","/api/memories",""}).body);CHECK_INT_EQ(memory.at("memories").size(),1);CHECK(memory.at("memories")[0].at("sources")[0].at("id")==sourceId);
        auto duplicate=app.request({"POST","/api/message",Json{{"request_id","unit-1"},{"text","如何理解等待？"}}.dump()});CHECK_INT_EQ(duplicate.status,200);CHECK(Json::parse(duplicate.body).at("duplicate").get<bool>());
    }
    {Application app(cfg);CHECK(app.initialize());auto state=Json::parse(app.request({"GET","/api/state",""}).body);CHECK_INT_EQ(state.at("messages").size(),2);CHECK(state.at("stage").at("id")=="initial");auto raw=Json::parse(app.request({"POST","/api/archive",Json{{"id",sourceId}}.dump()}).body);CHECK_INT_EQ(raw.at("records").size(),1);CHECK(raw.at("records")[0].at("data").at("text")=="如何理解等待？");}
}
TEST_CASE(mock_plugin_experiences_remain_internal_and_preserve_their_source){
    ayt::test::ScratchDirectory scratch("hibernation-plugin-experience");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}}};Application app(cfg);CHECK(app.initialize());
    const auto event=Json{{"origin","application_plugin"},{"observation",{{"kind","television.ended"},{"source","fixture.mp4"}}}};
    const auto response=app.request({"POST","/api/event",Json{{"request_id","plugin-experience"},{"kind","outer_event"},{"text",event.dump()}}.dump()});CHECK_INT_EQ(response.status,202);const auto source=Json::parse(response.body).at("event_id");
    CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));
    const auto chat=Json::parse(app.request({"GET","/api/chat",""}).body);CHECK(chat.at("messages").empty());const auto memory=Json::parse(app.request({"GET","/api/memories",""}).body).at("memories");CHECK(memory.size()==1);CHECK(memory[0].at("sources")[0].at("id")==source);CHECK(memory[0].at("text").get<std::string>().find("application_plugin")==std::string::npos);
}
TEST_CASE(solitude_without_authorized_fragments_skips_safely){
    ayt::test::ScratchDirectory scratch("hibernation-empty-context");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.recentMessageCount=0;cfg.maxMemoriesInContext=0;cfg.profiles={{"mock","Mock",true,{}}};
    Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/message",Json{{"request_id","empty-context"},{"text","一个暂存的问题"}}.dump()}).status,202);
    auto idle=[&]{app.tick();return !Json::parse(app.request({"GET","/api/state",""}).body).at("busy").get<bool>();};CHECK(ayt::test::waitUntil(idle));
    CHECK_INT_EQ(app.request({"POST","/api/solitude","{}"}).status,202);CHECK(ayt::test::waitUntil(idle));
    const auto state=Json::parse(app.request({"GET","/api/state",""}).body);for(const auto& j:state.at("jobs"))if(j.at("id")=="solitude.once")CHECK(Json::parse(j.at("result").get<std::string>()).at("status")=="no_update");
    CHECK_INT_EQ(Json::parse(app.request({"GET","/api/memories",""}).body).at("memories").size(),1);
}
TEST_CASE(workspace_configuration_protects_program_config_and_plugin_manifests){
    ayt::test::ScratchDirectory scratch("role-private-program-files");const auto configDir=scratch.path()/"config",pluginRoot=scratch.path()/"installed-plugins";std::filesystem::create_directories(configDir);std::filesystem::create_directories(pluginRoot);
    auto settings=Json::parse(readFile(std::filesystem::path(HA_PROJECT_ROOT)/"config/example.json"));settings["plugins"]=Json::object();settings["dataRoot"]=(scratch.path()/"data").generic_string();settings["characterRoot"]=(std::filesystem::path(HA_PROJECT_ROOT)/"character").generic_string();settings["uiRoot"]=(std::filesystem::path(HA_PROJECT_ROOT)/"ui").generic_string();settings["pluginRoot"]=pluginRoot.generic_string();const auto file=configDir/"config.json";std::ofstream(file)<<settings.dump();Application app(loadConfig(file));CHECK(app.initialize());
    CHECK_INT_EQ(app.request({"POST","/api/settings",Json{{"settings",settings}}.dump()}).status,200);
    for(const auto& root:{configDir,pluginRoot,std::filesystem::path(HA_PROJECT_ROOT)/"character"}){auto changed=settings;auto path=root.generic_string();std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});changed["draftsRoot"]=path;const auto rejected=app.request({"POST","/api/settings",Json{{"settings",changed}}.dump()});CHECK_INT_EQ(rejected.status,400);CHECK(Json::parse(rejected.body).at("error")=="workspace_overlaps_private_data");}
}
TEST_CASE(turn_protocol_distinguishes_silence_and_never_guesses_malformed_commands){
    Json silence={{"version",1},{"kind","silence"},{"text",nullptr},{"calls",Json::array()}};CHECK(validateTurn(silence.dump()).ok);
    auto call=silence;call["kind"]="call";CHECK(!validateTurn(call.dump()).ok);call["calls"].push_back({{"id","c1"},{"tool","memory.search"},{"args",{{"query","等待"}}}});CHECK(validateTurn(call.dump()).ok);
    call["calls"].push_back(call["calls"][0]);CHECK(!validateTurn(call.dump()).ok);call["calls"].erase(1);call["text"]="不要把内部调用显示出来";CHECK(!validateTurn(call.dump()).ok);
    CHECK(!validateTurn("Some prose "+silence.dump()).ok);CHECK(validateTurn("```json\n"+silence.dump()+"\n```").ok);
    CHECK(!validateTurn("{\"version\":1,\"version\":1,\"kind\":\"silence\",\"text\":null,\"calls\":[]}").ok);
    auto reply=silence;reply["kind"]="reply";reply["text"]=" \t\n";CHECK(!validateTurn(reply.dump()).ok);reply["text"]=std::string("hello\0world",11);CHECK(!validateTurn(reply.dump()).ok);
}
TEST_CASE(personal_carry_is_optional_bounded_authored_content_and_never_a_tool_batch){
    const Json note={{"id","paper-ship"},{"expected_revision",0},{"kind","interest"},{"topic","橱窗里的纸船"},{"note","想留意它往哪边走"},{"stance","curious"},{"source_ids",Json::array({"event-real"})}};
    Json turn={{"version",1},{"kind","reply"},{"text","先放着吧。"},{"calls",Json::array()},{"carry_forward",Json::array({note})}};
    CHECK(validateTurn(turn.dump()).ok);turn["kind"]="silence";turn["text"]=nullptr;CHECK(validateTurn(turn.dump()).ok);
    turn["kind"]="call";turn["calls"]=Json::array({{{"id","q"},{"tool","self.observe"},{"args",Json::object()}}});CHECK(!validateTurn(turn.dump()).ok);
    turn["kind"]="silence";turn["calls"]=Json::array();turn["carry_forward"].push_back(note);turn["carry_forward"].push_back(note);CHECK(!validateTurn(turn.dump()).ok);
    turn["carry_forward"]=Json::array({note});turn["carry_forward"][0]["source_ids"]=Json::array();CHECK(!validateTurn(turn.dump()).ok);
    turn["carry_forward"]=Json::array({note});turn["carry_forward"][0]["core_persona"]="new";CHECK(!validateTurn(turn.dump()).ok);
    const NativeToolBridge bridge(Json::array());
    const auto reply=bridge.candidate({{"final","ha_turn_reply",Json{{"text","嗯。"},{"carry_forward",Json::array({note})}}.dump()}},1);
    CHECK(reply.ok);CHECK(reply.value.at("carry_forward")[0]==note);
    CHECK(bridge.candidate({{"final","ha_turn_silence",Json{{"carry_forward",Json::array({note})}}.dump()}},1).ok);
    CHECK(bridge.candidate({{"final","ha_turn_silence",R"({"carry_forward":null})"}},1).ok);
}
TEST_CASE(role_state_moves_gradually_and_restores_without_offline_jumps){
    RoleState state;state.restore(Json::object(),1000);auto first=state.snapshot();std::string issue;CHECK(state.propose("withdrawing",-1,"experience-1",1100,issue));
    CHECK(state.snapshot().at("openness")==first.at("openness"));state.tick(1200);CHECK(state.snapshot().at("activity")=="idle");
    for(int i=1;i<=40;++i)state.tick(1200+i*100);auto after=state.snapshot();CHECK(after.at("activity")=="withdrawing");CHECK(after.at("openness").get<double>()<first.at("openness").get<double>());
    CHECK(first.at("openness").get<double>()-after.at("openness").get<double>()<0.02);RoleState restored;restored.restore(after,999999999);CHECK(restored.snapshot().at("openness")==after.at("openness"));
    CHECK(!state.propose("override_everything",1,"x",6000,issue));const auto beforeWeather=state.snapshot();state.weather(38,63,6000);CHECK(state.snapshot().at("mood")==beforeWeather.at("mood"));state.tick(6100);CHECK(state.snapshot().at("mood").get<double>()<=beforeWeather.at("mood").get<double>());
}
TEST_CASE(strict_provider_schema_and_optional_nulls_preserve_permission_validation){
    const Json parameters={{"type","object"},{"additionalProperties",false},{"required",Json::array({"query"})},{"properties",{{"query",{{"type","string"}}},{"limit",{{"type","integer"},{"minimum",1}}}}}};
    const Json catalog=Json::array({{{"name","memory.search"},{"parameters",parameters}}});const auto schema=turnProviderSchema(catalog);CHECK(static_cast<bool>(ayt::serializer::validateJsonSchema(schema)));
    Json turn={{"version",1},{"kind","call"},{"text",nullptr},{"calls",Json::array({{{"id","c"},{"tool","memory.search"},{"args",{{"query","x"},{"limit",nullptr}}}}})},{"presentation",{{"speech",false},{"bubble",false}}}};
    turn["presentation"]["reply_timing"]=nullptr;turn["carry_forward"]=nullptr;turn["appraisal"]=nullptr;turn["messages"]=nullptr;turn["feeling"]=nullptr;CHECK(static_cast<bool>(ayt::serializer::validateJsonDocument(turn.dump(),schema)));
    for(const auto* field:{"messages","feeling"}){auto missing=turn;missing.erase(field);CHECK(!ayt::serializer::validateJsonDocument(missing.dump(),schema));}
    CHECK(validateTurn(turn.dump()).ok);auto args=normalizeToolArguments(turn.at("calls")[0].at("args"),parameters);CHECK(!args.contains("limit"));CHECK(args.at("query")=="x");
    args=normalizeToolArguments({{"query",nullptr},{"unknown",nullptr}},parameters);CHECK(args.contains("query"));CHECK(args.contains("unknown"));CHECK(!ayt::serializer::validateJsonDocument(args.dump(),parameters.dump()));
    turn["calls"][0]["tool"]="shell.run";CHECK(!ayt::serializer::validateJsonDocument(turn.dump(),schema));
}
TEST_CASE(vault_authenticates_identity_and_ciphertext_and_never_resets_missing_keys){
    ayt::test::ScratchDirectory scratch("role-vault");std::filesystem::create_directories(scratch.path()/"runtime");std::filesystem::create_directories(scratch.path()/"archive");auto codec=openVaultCodec(scratch.path());
    ayt::storage::VersionedRecord metadata{"memory","private",1,1,{},"experience","turn",1};std::string encoded,again,decoded;CHECK(codec->encode(metadata,"这是一条不应直接出现在文件里的记忆",encoded));CHECK(codec->encode(metadata,"这是一条不应直接出现在文件里的记忆",again));CHECK(encoded!=again);CHECK(encoded.find("记忆")==std::string::npos);CHECK(codec->decode(metadata,encoded,decoded));CHECK(decoded=="这是一条不应直接出现在文件里的记忆");
    auto changed=metadata;changed.id="different";CHECK(!codec->decode(changed,encoded,decoded));encoded.back()=encoded.back()=='a'?'b':'a';CHECK(!codec->decode(metadata,encoded,decoded));
    const auto file=scratch.path()/"archive/records.sqlite";{ayt::storage::RecordStore store;CHECK(store.open(file,{codec,false}));CHECK(static_cast<bool>(store.commit({{"memory","private",0,1,"literal-private-memory-marker","source"}},"turn",1)));}
    CHECK(readFile(file).find("literal-private-memory-marker")==std::string::npos);std::filesystem::rename(scratch.path()/"runtime/memory-key.dpapi",scratch.path()/"runtime/saved-key.dpapi");bool denied=false;try{(void)openVaultCodec(scratch.path());}catch(...){denied=true;}CHECK(denied);CHECK(!std::filesystem::exists(scratch.path()/"runtime/memory-key.dpapi"));
}
TEST_CASE(configuration_keeps_secrets_out_of_plaintext_and_rejects_system_environment_overwrites){
    ayt::test::ScratchDirectory scratch("role-config-boundaries");const auto file=scratch.path()/"config.json";const auto base=Json::parse(readFile(std::filesystem::path(HA_PROJECT_ROOT)/"config/example.json"));
    auto rejects=[&](Json value){std::ofstream(file,std::ios::binary)<<value.dump();try{(void)loadConfig(file);return false;}catch(...){return true;}};
    auto value=base;value["profiles"][0]["id"]="../secret";CHECK(rejects(value));value=base;value["profiles"][0]["credentialEnvironment"]="PATH";CHECK(rejects(value));
    value=base;value["plugins"]={{"tts",{{"enabled",false},{"apiKey","forbidden"}}}};CHECK(rejects(value));value=base;value["remoteStorage"]={{"enabled",true}};CHECK(rejects(value));value=base;value["remoteStorage"]={{"token","forbidden"}};CHECK(rejects(value));
    value=base;value["profiles"][0]["credentialEnvironment"]="hibernation_session_key";CHECK(rejects(value));value=base;value["profiles"][0]["id"]="plugin-tts";CHECK(rejects(value));value=base;value["profiles"][0]["proxyOrigin"]="http://user:password@proxy";CHECK(rejects(value));
}
#ifdef HA_TEST_PLUGIN_ROOT
TEST_CASE(body_before_typing_survives_the_native_attention_transition){
    ayt::test::ScratchDirectory scratch("body-before-typing");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.uiRoot=std::filesystem::path(HA_PROJECT_ROOT)/"ui";cfg.pluginRoot=HA_TEST_PLUGIN_ROOT;cfg.plugins={{"widget",{{"enabled",true},{"roam",false}}}};cfg.profiles={{"mock","Mock",true,{}}};Application app(cfg);CHECK(app.initialize());
    app.setInputActive(true);app.tick();const auto caps=Json::parse(app.request({"GET","/api/capabilities",""}).body);bool receiving=false;for(const auto& plugin:caps.at("plugins"))if(plugin.at("id")=="widget")receiving=plugin.at("current").at("state")=="receiving";CHECK(receiving);
    app.setInputActive(false);CHECK(app.request({"POST","/api/message",Json{{"request_id","typing-body"},{"text","你刚才在做什么"}}.dump()}).status==202);
    CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));
    const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id","trace-chat-1"}}.dump()}).body);CHECK(trace.at("body_at_input").at("widget").at("state")=="idle");
}
TEST_CASE(widget_routes_input_attention_and_preparation_through_the_plugin_runtime){
    ayt::test::ScratchDirectory scratch("role-widget-preparation");PluginRuntime plugins(std::filesystem::path(HA_TEST_PLUGIN_ROOT),{scratch.path(),std::filesystem::path(HA_PROJECT_ROOT),false,{}});std::string issue;
    CHECK(plugins.configure({{"widget",{{"enabled",true},{"roam",false}}}},issue));plugins.event({{"kind","runtime.attention"},{"input_active",true},{"busy",false}});CHECK(plugins.snapshots().at("widget").at("dialogue_phase")=="receiving");plugins.tick(.1);CHECK(plugins.snapshots().at("widget").at("state")=="receiving");
    plugins.event({{"kind","runtime.attention"},{"input_active",false},{"busy",false}});CHECK(plugins.snapshots().at("widget").at("dialogue_phase")=="idle");CHECK(plugins.snapshots().at("widget").at("state")=="idle");
    plugins.event({{"kind","conversation.received"}});plugins.tick(.2);CHECK(plugins.snapshots().at("widget").at("dialogue_phase")=="receiving");plugins.tick(.2);CHECK(plugins.snapshots().at("widget").at("state")=="preparing");plugins.event({{"kind","conversation.finished"},{"has_reply",false}});CHECK(plugins.snapshots().at("widget").at("dialogue_phase")=="idle");
}
TEST_CASE(widget_reveals_and_pages_without_truncating_or_following_model_display_flags){
    ayt::test::ScratchDirectory scratch("role-widget-playback");PluginRuntime plugins(std::filesystem::path(HA_TEST_PLUGIN_ROOT),{scratch.path(),std::filesystem::path(HA_PROJECT_ROOT),false,{}});std::string issue;
    CHECK(plugins.configure({{"widget",{{"enabled",true},{"roam",false},{"bubbleMaxCharacters",20},{"charactersPerSecond",40},{"pageHoldSeconds",0.2}}}},issue));
    plugins.event({{"kind","assistant.reply"},{"reply_id","fixture"},{"text","这是第一句话。然后还有很长的一句话，需要继续显示到下一个页面，最后也不会丢掉。"},{"presentation",{{"bubble",false}}}});
    auto result=plugins.invoke("widget.status",Json::object(),{}).at("result");CHECK(result.at("bubble_pages").get<int>()>1);CHECK(result.at("bubble_visible_characters")==0);plugins.tick(0.1);result=plugins.invoke("widget.status",Json::object(),{}).at("result");CHECK(result.at("bubble_visible_characters")==4);for(int i=0;i<15;++i)plugins.tick(0.1);result=plugins.invoke("widget.status",Json::object(),{}).at("result");CHECK(result.at("bubble_page").get<int>()>0);CHECK(result.dump().find("第一句话")==std::string::npos);
}
TEST_CASE(plugin_configuration_stages_and_rolls_back_without_replacing_the_core){
    ayt::test::ScratchDirectory scratch("role-plugin-lifecycle");PluginRuntime plugins(std::filesystem::path(HA_TEST_PLUGIN_ROOT),{scratch.path(),std::filesystem::path(HA_PROJECT_ROOT),false,{}});std::string issue;
    CHECK(plugins.configure(Json::object(),issue));CHECK(plugins.catalog().empty());
    CHECK(!plugins.configure({{"weather",{{"enabled",true}}},{"widget",{{"enabled",true},{"size",1}}}},issue));CHECK(plugins.catalog().empty());
    Json enabled={{"weather",{{"enabled",true},{"city",""}}},{"widget",{{"enabled",true},{"roam",false}}}};CHECK(plugins.configure(enabled,issue));CHECK(!plugins.catalog().empty());
    auto weather=plugins.invoke("weather.current",Json::object(),{});CHECK(!weather.at("ok").get<bool>());CHECK(weather.at("error").at("code")=="location_not_configured");
    auto status=plugins.invoke("widget.status",Json::object(),{});CHECK(status.at("ok").get<bool>());CHECK(!status.at("result").at("desktop").get<bool>());
    auto rejected=enabled;rejected["widget"]["size"]=999;CHECK(!plugins.configure(rejected,issue));CHECK(plugins.invoke("widget.status",Json::object(),{}).at("ok").get<bool>());
    CHECK(plugins.configure(Json::object(),issue));CHECK(plugins.catalog().empty());CHECK(plugins.configure(enabled,issue));CHECK(plugins.invoke("widget.status",Json::object(),{}).at("ok").get<bool>());
    plugins.stop();CHECK(!plugins.configure(enabled,issue));CHECK(!plugins.invoke("widget.status",Json::object(),{}).at("ok").get<bool>());
}
TEST_CASE(plugin_settings_reject_wrong_types_before_background_threads_use_them){
    ayt::test::ScratchDirectory scratch("role-plugin-config-types");PluginRuntime plugins(std::filesystem::path(HA_TEST_PLUGIN_ROOT),{scratch.path(),std::filesystem::path(HA_PROJECT_ROOT),false,{}});std::string issue;
    const std::vector<Json> invalid={
        {{"tts",{{"enabled",true},{"backend","http"},{"origin","http://127.0.0.1:9"},{"model",42}}}},
        {{"television",{{"enabled",true},{"useSystemProxy","yes"}}}},
        {{"widget",{{"enabled",true},{"showBubble","yes"}}}},
        {{"widget",{{"enabled",true},{"skin","../external"}}}},
        {{"widget",{{"enabled",true},{"reactionProbability",1.1}}}},
        {{"widget",{{"enabled",true},{"reducedMotion","yes"}}}},
        {{"tts",{{"enabled",true},{"backend","http"},{"origin","http://127.0.0.1:9"},{"proxyOrigin","http://user:password@127.0.0.1:9"}}}}
    };
    for(const auto& settings:invalid){CHECK(!plugins.configure(settings,issue));CHECK(plugins.catalog().empty());}
}
TEST_CASE(plugin_dependency_metadata_must_match_the_native_contract){
    ayt::test::ScratchDirectory scratch("role-plugin-dependency-metadata");const auto root=scratch.path()/"plugins";std::filesystem::create_directories(root);for(const auto* id:{"widget","rhythm"})std::filesystem::copy(std::filesystem::path(HA_TEST_PLUGIN_ROOT)/id,root/id,std::filesystem::copy_options::recursive);
    const auto path=root/"rhythm/plugin.json";const auto original=Json::parse(readFile(path));auto altered=original;altered["dependencies"]=Json::array();std::ofstream(path)<<altered.dump();
    {PluginRuntime plugins(root,{scratch.path(),std::filesystem::path(HA_PROJECT_ROOT),false,{}});std::string issue;CHECK(plugins.configure({{"widget",{{"enabled",true},{"roam",false}}}},issue));CHECK(!plugins.configure({{"widget",{{"enabled",true}}},{"rhythm",{{"enabled",true}}}},issue));CHECK(issue=="plugin_dependency_manifest");CHECK(plugins.invoke("widget.status",Json::object(),{}).at("ok").get<bool>());}
    std::ofstream(path)<<original.dump();PluginRuntime correct(root,{scratch.path(),std::filesystem::path(HA_PROJECT_ROOT),false,{}});std::string issue;CHECK(correct.configure({{"widget",{{"enabled",true}}},{"rhythm",{{"enabled",true},{"automaticRest",false}}}},issue));CHECK(correct.invoke("rhythm.observe",Json::object(),{}).at("ok").get<bool>());CHECK(correct.invoke("rhythm.rest",{{"seconds",15}},{}).at("error").at("code")=="desktop_required");
}
#endif
TEST_CASE(memory_content_grouping_keeps_categories_and_authentic_source_versions){
    Json a={{"kind","inference"},{"text","同一段  文字\n"}};auto b=a;b["text"]="同一段 文字";CHECK(memoryContentKey(a)==memoryContentKey(b));b["kind"]="outer";CHECK(memoryContentKey(a)!=memoryContentKey(b));
    Json refs=Json::array({{{"id","b"},{"revision",1}},{{"id","a"},{"revision",2}}});auto reversed=refs;std::reverse(reversed.begin(),reversed.end());CHECK(canonicalSources(refs)==canonicalSources(reversed));reversed[0]["revision"]=3;CHECK(canonicalSources(refs)!=canonicalSources(reversed));
}
TEST_CASE(reaction_cache_validates_candidates_and_consumes_each_ready_entry_once){
    auto value=Json{{"version",1},{"reactions",Json::array({{{"text","先停在这里。"},{"animation","idle"}}})}};CHECK(validateReactions(value.dump()).ok);value["reactions"].push_back(value["reactions"][0]);CHECK(!validateReactions(value.dump()).ok);value["reactions"].erase(1);value["reactions"][0]["shell"]="not permitted";CHECK(!validateReactions(value.dump()).ok);
    Json pool={{"entries",Json::array({{{"id","expired"},{"text","过期"},{"expires",100},{"ready",true}},{{"id","pending"},{"text","未准备"},{"expires",999},{"ready",false}},{{"id","one"},{"text","先停在这里。"},{"expires",999},{"ready",true}}})},{"used",Json::array()}};auto first=takeReaction(pool,101);CHECK(first.at("id")=="one");CHECK(pool.at("entries").size()==1);CHECK(pool.at("used").size()==1);CHECK(takeReaction(pool,102).is_null());CHECK(pool.at("used").size()==1);
}
TEST_CASE(widget_appearances_keep_valid_pixels_and_distinct_continuous_poses){
    using namespace appearance;CHECK(validSkin("dinosaur"));CHECK(!validSkin("../external"));CHECK(validPalette("mist"));CHECK(!validPalette("#ff00ff"));
    for(const auto skin:{"dinosaur","moonbird","lumen"}){
        const auto idle=make(skin,"idle",1);std::size_t visible=0;for(const auto cell:idle.cells){CHECK(cell<=5);if(cell)++visible;}CHECK(visible>90);
        const auto blink=make(skin,"idle",6.6);CHECK(blink.blink);CHECK(blink.cells!=idle.cells);CHECK(make(skin,"resting",1).scaleY<idle.scaleY);
        const auto speaking=make(skin,"speaking",1,false,1,true);CHECK(speaking.mouthOpen);CHECK(speaking.cells!=idle.cells);
        const auto landing=make(skin,"landing",1,false,0.2);CHECK(landing.scaleX>1);CHECK(landing.scaleY<1);const auto still=make(skin,"landing",1,true,0.2);CHECK(still.scaleX==1);CHECK(still.scaleY==1);
        for(int i=0;i<64;++i){auto pose=make(skin,"walk",i*.11);CHECK(std::isfinite(pose.offsetY));for(const auto cell:pose.cells)CHECK(cell<=5);}
    }
    CHECK(make("dinosaur","walk",.1).cells!=make("dinosaur","walk",.5).cells);
}
TEST_CASE(dashboard_appearance_catalog_uses_native_frames_without_role_data){
    const auto value=appearance::catalog();CHECK(value.at("extent")==24);CHECK(value.at("skins").size()==3);CHECK(value.at("palettes").size()==3);
    for(const auto& skin:value.at("skins")){CHECK(appearance::validSkin(skin.at("id").get<std::string>()));CHECK(skin.at("poses").size()==4);
        for(const auto& pose:skin.at("poses")){const auto expected=appearance::make(skin.at("id").get<std::string>(),pose.at("state").get<std::string>(),.15,false,1,true);const auto cells=pose.at("cells").get<std::string>();CHECK(cells.size()==576);bool same=true;for(std::size_t i=0;i<cells.size();++i)same=same&&cells[i]==static_cast<char>('0'+expected.cells[i]);CHECK(same);}
    }
    for(const auto& colors:value.at("palettes")){CHECK(appearance::validPalette(colors.at("id").get<std::string>()));CHECK(colors.at("colors").size()==6);for(const auto& color:colors.at("colors"))CHECK(color.get<std::string>().size()==7);}
    CHECK(!value.contains("state"));CHECK(!value.contains("memory"));
}
TEST_CASE(dashboard_sequence_preset_uses_real_pack_and_recognizes_legacy_override){
    const auto root=std::filesystem::path(HA_PROJECT_ROOT)/"plugins";unsigned images=0;
    const auto encode=[&](const std::vector<std::uint8_t>& bytes){CHECK(bytes.size()>8);CHECK(bytes[0]==137&&bytes[1]==80&&bytes[2]==78&&bytes[3]==71);++images;return std::string("encoded-png");};
    const auto value=appearance::catalog(root,Json{{"animationPack",(root/"widget/characters/asterlan").string()}},encode);
    CHECK(value.at("skins").size()==4);CHECK(value.at("override_preset")=="asterlan");CHECK(images==4);
    const auto& preset=value.at("skins").back();CHECK(preset.at("id")=="asterlan");CHECK(preset.at("original_palette")==true);
    for(const auto& pose:preset.at("poses")){CHECK(pose.at("image")=="data:image/png;base64,encoded-png");CHECK(pose.at("canvas")==Json::array({168,168}));CHECK(!pose.contains("cells"));}
    const auto custom=appearance::catalog(root,Json{{"animationPack",(root/"widget/characters/lumen").string()}},encode);CHECK(custom.at("override_preset")=="custom");
    const auto missing=appearance::catalog(root/"missing",Json::object(),encode);CHECK(missing.at("skins").size()==3);CHECK(missing.contains("sequence_preview_issue"));
}
TEST_CASE(desk_objects_need_a_real_arrival_and_completed_action){
    SmallWorld world;CHECK(world.place("berry-1","berry",.4,.5));CHECK(world.request("berry-1","eat","action-1").empty());CHECK(world.size()==1);CHECK(world.queued());
    CHECK(world.beginWalk());CHECK(!world.arrive("different",.4,.5));CHECK(!world.arrive("action-1",.8,.5));CHECK(world.arrive("action-1",.4,.5));
    auto prior=world.snapshot();for(int i=0;i<8;++i){world.tick(.25,true,.4,.5,true);auto next=world.snapshot();CHECK(std::abs(next.at("appetite").get<double>()-prior.at("appetite").get<double>())<=.00151);prior=next;}
    CHECK(world.size()==0);CHECK(!world.pending());unsigned completed=0;for(const auto& e:world.takeEvents())if(e.at("kind")=="habitat.consumed")++completed;CHECK(completed==1);
    world.tick(.25,true,.4,.5,true);CHECK(world.takeEvents().empty());CHECK(!world.request("berry-1","eat","replay").empty());
}
TEST_CASE(desk_object_interruption_and_restart_never_finish_an_old_action){
    SmallWorld world;CHECK(world.place("star","paper_star",.2,.2));CHECK(!world.request("star","eat","wrong").empty());CHECK(world.request("star","look","look-1").empty());CHECK(world.beginWalk());CHECK(world.arrive("look-1",.2,.2));
    world.tick(.25,true,.2,.2,true);const auto frozen=world.snapshot();world.tick(500,false,.2,.2,true);CHECK(world.snapshot()==frozen);
    SmallWorld restored;CHECK(restored.restore(frozen));CHECK(!restored.pending());CHECK(restored.size()==1);for(int i=0;i<10;++i)restored.tick(.25,true,.2,.2,true);for(const auto& e:restored.takeEvents())CHECK(e.at("kind")!="habitat.inspected");
    world.cancel("user_dragged");for(int i=0;i<10;++i)world.tick(.25,true,.2,.2,true);CHECK(world.size()==1);for(const auto& e:world.takeEvents())CHECK(e.at("kind")!="habitat.inspected");
}
TEST_CASE(planted_seeds_grow_only_during_active_elapsed_time){
    SmallWorld world;CHECK(world.place("seed","seed",.3,.4));CHECK(world.request("seed","plant","plant-1").empty());CHECK(world.beginWalk());CHECK(world.arrive("plant-1",.3,.4));for(int i=0;i<10;++i)world.tick(.25,true,.3,.4,true);
    CHECK(world.object("seed").at("kind")=="sprout");CHECK(world.object("seed").at("growth")==0);for(int i=0;i<2401;++i)world.tick(.25,true,.3,.4,true);CHECK(world.object("seed").at("growth")==1);
    const auto saved=world.snapshot();SmallWorld restored;CHECK(restored.restore(saved));restored.tick(86400,true,.3,.4,true);CHECK(restored.object("seed").at("growth")==1);CHECK(restored.clock()-world.clock()<=.251);
    auto invalid=saved;invalid["appetite"]=2;CHECK(!restored.restore(invalid));CHECK(restored.object("seed").at("kind")=="sprout");
}
TEST_CASE(startup_pixels_have_real_transparency_and_valid_premultiplied_color){
    using namespace splashPixels;std::vector<std::uint32_t> pixels(width*height);
    for(const auto name:{"forest","mist","amber"})for(bool reduced:{false,true})for(double time:{0,100,400,650,1200}){
        render(pixels.data(),time,0,reduced,appearance::palette(name));CHECK(pixels.front()==0);CHECK(pixels.back()==0);unsigned visible=0;bool valid=true;for(auto pixel:pixels){const auto alpha=pixel>>24;valid=valid&&((pixel>>16)&255)<=alpha&&((pixel>>8)&255)<=alpha&&(pixel&255)<=alpha;if(alpha)++visible;}CHECK(valid);if(time>0)CHECK(visible>50);
    }
    render(pixels.data(),1000,1,false,appearance::palette("forest"));CHECK(std::all_of(pixels.begin(),pixels.end(),[](auto pixel){return pixel==0;}));
}
TEST_CASE(frontend_preferences_keep_defaults_and_reject_malformed_configuration){
    ayt::test::ScratchDirectory scratch("frontend-preferences");std::ifstream input(std::filesystem::path(HA_PROJECT_ROOT)/"config/example.json");auto settings=Json::parse(input);const auto file=scratch.path()/"config.json";
    auto write=[&]{std::ofstream stream(file);stream<<settings.dump();};write();auto normal=loadConfig(file);CHECK(normal.frontend.at("startupAnimation")==true);
    settings["frontend"]={{"startupAnimation",false},{"reducedMotion",true}};write();auto reduced=loadConfig(file);CHECK(reduced.frontend.at("reducedMotion")==true);
    for(const auto& invalid:{Json{{"startupAnimation","yes"}},Json{{"transitionMs",999999}},Json(nullptr)}){settings["frontend"]=invalid;write();bool rejected=false;try{loadConfig(file);}catch(...){rejected=true;}CHECK(rejected);}
}
TEST_CASE(tool_failure_hints_follow_the_failed_capability){
    auto memory=toolFailureGuidance("memory.remember","memory_category_requires_experience");CHECK(memory.find("television")==std::string::npos);CHECK(memory.find("推测")!=std::string::npos);
    auto video=toolFailureGuidance("television.open","video_source_outside_workspace");CHECK(video.find("television.sources")!=std::string::npos);
    auto food=toolFailureGuidance("habitat.act","world_not_hungry");CHECK(food.find("不必解释内部数值")!=std::string::npos);CHECK(food.find("视频")==std::string::npos);
}
TEST_CASE(desk_object_frames_distinguish_growth_and_keep_reduced_motion_stable){
    const auto sprout=deskAppearance::make("sprout",0,1),leaves=deskAppearance::make("sprout",1,1),bud=deskAppearance::make("sprout",2,1),flower=deskAppearance::make("sprout",3,1);CHECK(sprout.cells!=leaves.cells&&leaves.cells!=bud.cells&&bud.cells!=flower.cells);
    for(const auto* kind:{"berry","leaf","seed","paper_star","sprout"}){const auto frame=deskAppearance::make(kind,3,.1);bool valid=true,nonempty=false;for(const auto cell:frame.cells){valid=valid&&cell<5;nonempty=nonempty||cell!=0;}CHECK(valid&&nonempty);const auto reduced=deskAppearance::make(kind,3,.1,true),later=deskAppearance::make(kind,3,3.7,true);CHECK(reduced.cells==later.cells&&reduced.offsetY==0&&later.offsetY==0&&reduced.pixelSize==3);}
}
TEST_CASE(quiet_rest_requires_a_real_pose_and_preserves_pause_and_attention_boundaries){
    QuietRhythm rhythm;CHECK(rhythm.request("rest-1",15,1,"model").empty());rhythm.tick(.25,true,true,false,true,"idle",1);CHECK(rhythm.phase()=="queued");CHECK(!rhythm.acknowledged("rest-1"));rhythm.tick(.25,true,false,false,true,"idle",1);CHECK(rhythm.phase()=="requesting");CHECK(!rhythm.acknowledged("wrong"));CHECK(rhythm.acknowledged("rest-1"));CHECK(rhythm.phase()=="active");
    const auto frozen=rhythm.snapshot();rhythm.tick(600,false,false,false,true,"resting",1);CHECK(rhythm.snapshot()==frozen);for(int i=0;i<60;++i)rhythm.tick(.25,true,false,false,true,"resting",1);CHECK(rhythm.phase()=="idle");bool finished=false;for(const auto& event:rhythm.takeEvents())if(event.at("kind")=="rhythm.rest_finished"){CHECK(event.at("elapsed_seconds")==15);finished=true;}CHECK(finished);
    CHECK(rhythm.request("rest-2",30,2,"model").empty());rhythm.tick(.25,true,false,false,true,"idle",3);CHECK(rhythm.phase()=="idle");for(const auto& event:rhythm.takeEvents())CHECK(event.at("kind")!="rhythm.rest_started");
}
TEST_CASE(quiet_rest_restart_records_only_actual_elapsed_time_and_never_replays_an_intent){
    QuietRhythm rhythm;CHECK(rhythm.request("rest-3",60,4,"automatic").empty());rhythm.tick(.25,true,false,false,true,"idle",4);CHECK(rhythm.acknowledged("rest-3"));for(int i=0;i<20;++i)rhythm.tick(.25,true,false,false,true,"resting",4);const auto saved=rhythm.snapshot();QuietRhythm restored;CHECK(restored.restore(saved));CHECK(restored.phase()=="idle");int interruptions=0;for(const auto& event:restored.takeEvents())if(event.at("kind")=="rhythm.rest_interrupted"){++interruptions;CHECK(event.at("elapsed_seconds")==5);CHECK(event.at("reason")=="application_restarted");}CHECK(interruptions==1);restored.tick(86400,true,false,false,true,"idle",5);CHECK(restored.phase()=="idle");CHECK(restored.clock()-rhythm.clock()<=.251);
    CHECK(QuietRhythm::mayRest(23,.8,"idle"));CHECK(!QuietRhythm::mayRest(12,.8,"idle"));CHECK(QuietRhythm::mayRest(12,.2,"idle"));CHECK(!QuietRhythm::mayRest(23,.2,"watching"));CHECK(!QuietRhythm::restfulHour(-1)&&!QuietRhythm::restfulHour(24));
}
TEST_CASE(television_layout_keeps_the_real_media_view_inside_its_frame){
    for(const int width:{320,480,900,1280})for(const int height:{220,320,500,900})for(const bool framed:{false,true}){const auto layout=televisionLayout(width,height,framed);CHECK(layout.screen.width()>0&&layout.screen.height()>0);CHECK(layout.screen.left>=0&&layout.screen.top>=0&&layout.screen.right<=width&&layout.screen.bottom<=height);CHECK(layout.footer.top>=layout.screen.bottom);if(framed){CHECK(layout.play.left>layout.screen.right);CHECK(layout.power.right<=width&&layout.stop.bottom<height);}}
}
TEST_CASE(television_auto_login_does_not_reserve_a_button_in_the_frame){
    CHECK(replayProtectedOperation("television.login"));
    for(const bool framed:{false,true}){const auto layout=televisionLayout(480,320,framed);CHECK(layout.login.width()==0&&layout.login.height()==0);if(!framed)CHECK(layout.footer.right==466);}
}

TEST_CASE(audio_output_observations_preserve_unknowns_and_never_confirm_user_audibility){
    Json volume={{"known",true},{"muted",false},{"master",.75},{"channels",Json::array({1.0,1.0})}};
    Json normal={{"checked_at_ms",1000},{"query_state","ok"},{"console",{{"available",true},{"volume",volume}}},{"multimedia",{{"available",true},{"volume",volume}}},{"own_sessions",Json::array()}};
    CHECK(assessAudioDelivery(normal,false,75,1001).at("code")=="device_unavailable");CHECK(assessAudioDelivery(normal,true,0,1001).at("code")=="app_volume_zero");CHECK(assessAudioDelivery(normal,true,75,17000).at("code")=="observation_stale");CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="idle_or_unknown");
    Json session={{"state","active"},{"default_for_console",true},{"session",volume},{"endpoint",volume}};normal["own_sessions"].push_back(session);CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="no_known_blocker");CHECK(!assessAudioDelivery(normal,true,75,1001).at("heard_confirmed").get<bool>());
    normal["own_sessions"][0]["session"]["channels"]=Json::array({0.0,0.0});CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="active_sessions_muted");normal["own_sessions"].push_back(session);CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="no_known_blocker");
    normal["own_sessions"]=Json::array({session});normal["own_sessions"][0]["session"]={{"known",false}};CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="volume_unknown");normal["query_state"]="partial";CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="partially_unknown");
    normal["query_state"]="ok";normal["own_sessions"]=Json::array({session});normal["own_sessions"][0]["default_for_console"]=false;CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="different_output");normal["own_sessions"]=Json::array();normal["console"]["available"]=false;normal["multimedia"]["available"]=false;CHECK(assessAudioDelivery(normal,true,75,1001).at("code")=="default_missing");
}
TEST_CASE(memory_library_pages_are_utf8_bounded_and_preserve_originals){
    Json original={{"kind","conversation"},{"text",std::string("星空")+std::string(3000,'x')},{"sources",Json::array()}};
    auto page=libraryPage(original,1,256);CHECK(page.at("offset")==0);CHECK(page.at("next_offset")==256);CHECK(page.at("truncated").get<bool>());CHECK(original.at("text").get<std::string>().size()==3006);
    auto tail=libraryPage(original,4000,256);CHECK(tail.at("text")=="");CHECK(!tail.at("truncated").get<bool>());CHECK(tail.at("next_offset")==3006);
    CHECK(libraryContext(original).at("text").get<std::string>().size()<=1200);CHECK(libraryArchiveKind("user_message"));CHECK(!libraryArchiveKind("tool_observation"));CHECK(!libraryArchiveKind("trace"));
    Json summary={{"summary","原文没有说行动已完成。"},{"keywords",Json::array({"星空","未完成"})},{"uncertainties",Json::array({"只是一项计划。"})}};CHECK(validateDigest(summary.dump()).ok);summary["sql"]="DROP TABLE";CHECK(!validateDigest(summary.dump()).ok);summary.erase("sql");summary["keywords"]=Json::array({"同词","同词"});CHECK(!validateDigest(summary.dump()).ok);
    CHECK(memoryRelevance("星空",memorySearchText({{"text","一段摘要"},{"keywords",Json::array({"星空"})}}))>0);
}
TEST_CASE(world_history_projects_actual_results_without_internal_details_or_pixels){
    const Json raw={{"kind","tool_observation"},{"tool","pocket.leave"},{"created_at",42},{"trace_id","INTERNAL_TRACE"},{"result",{{"applied",true},{"completed",false},{"game",{{"id","lights-old"},{"status","left"},{"revision",7},{"lit_count",3},{"moves",5},{"moves_remaining",19},{"rows",Json::array({Json::array({true,false,false}),Json::array({false,true,false}),Json::array({false,false,true})})},{"diagnostics","INTERNAL_GAME_DATA"}}},{"diagnostics","INTERNAL_DIAGNOSTIC"}}}};
    const auto before=raw.dump();const auto view=libraryArchiveView(raw);CHECK(view.at("historical_only").get<bool>());CHECK(view.at("result").at("applied")==true&&view.at("result").at("completed")==false);CHECK(view.at("result").at("game").at("status")=="left");CHECK(view.at("result").at("game").at("rows").size()==3);CHECK(view.dump().find("INTERNAL_")==std::string::npos);CHECK(raw.dump()==before);CHECK(memorySearchText(view).find("lights-old")!=std::string::npos);
    for(const auto* tool:{"state.observe","body.observe","activity.status","mentor.execute","capabilities.list","unknown.plugin"}){auto internal=raw;internal["tool"]=tool;CHECK(libraryArchiveView(internal).is_null());}
    auto pending=raw;pending["kind"]="tool_intent";pending["result"]["applied"]=false;CHECK(libraryArchiveView(pending).is_null());
    auto image=raw;image["tool"]="television.observe_sequence";image["result"]={{"sampled_frames",2},{"full_video_watched",false},{"audio_delivered",false},{"frames",Json::array({Json{{"image",{{"data","SECRET_BASE64"}}}}})}};
    const auto meta=libraryArchiveView(image);CHECK(!meta.at("result").at("image_content_in_context").get<bool>());CHECK(meta.at("result").at("visual_revisit_required").get<bool>());CHECK(meta.dump().find("SECRET_BASE64")==std::string::npos);CHECK(meta.at("result").at("full_video_watched")==false);
}
TEST_CASE(world_history_retains_observed_text_scope_and_utf8_pages_without_fetching_new_content){
    Json raw={{"kind","tool_observation"},{"tool","atelier.append"},{"result",{{"written",true},{"completed",true},{"piece",{{"id","piece-old"},{"revision",3},{"title","半个圆"}}}}},{"written_content","后来风停了。"}};
    auto view=libraryArchiveView(raw);CHECK(view.at("observed_text_scope")=="applied_append_fragment");CHECK(view.at("text").get<std::string>().find("后来风停了。")!=std::string::npos);CHECK(!view.contains("written_content"));raw.erase("written_content");CHECK(!libraryArchiveView(raw).contains("observed_text_scope"));
    raw["tool"]="atelier.read";raw["result"]["piece"]["content"]=std::string(59999,'a')+"星空";view=libraryArchiveView(raw);CHECK(view.at("observed_text_clipped").get<bool>());CHECK(view.at("observed_text_bytes")==60005);CHECK(view.at("text").get<std::string>().ends_with("aaa"));CHECK(!view.at("result").at("piece").contains("content"));const auto page=libraryPage(view,1,257);CHECK(page.at("text").get<std::string>().size()<=257);CHECK(raw.at("result").at("piece").at("content").get<std::string>().ends_with("星空"));
    raw["tool"]="reading.open";raw["result"]={{"text","这只是导言。"},{"scope","article_lead_summary"},{"full_article_read",false},{"source_url","https://example.org/reference"}};view=libraryArchiveView(raw);CHECK(view.at("observed_text_scope")=="observed_article_excerpt");CHECK(view.at("result").at("full_article_read")==false);CHECK(view.at("text").get<std::string>().find("这只是导言。")!=std::string::npos);
}
TEST_CASE(personal_experience_contexts_are_bounded_provenance_objects_rather_than_current_targets){
    Json row={{"note","我想再看一眼这局"},{"episodes",Json::array()},{"experience_evidence",Json::array({personal::anchor("conversation","hidden-conversation-id",.5),personal::anchor("puzzle","lights-1",1),personal::anchor("authored_piece","small-story",1),personal::anchor("puzzle","lights-1",1)})}};
    const auto original=row.dump();const auto view=personal::context(row,3);CHECK(view.at("experience_contexts").size()==2);CHECK(view.dump().find("hidden-conversation-id")==std::string::npos);CHECK(view.at("experience_contexts")[0].at("id")=="lights-1");CHECK(!view.at("experience_contexts")[0].contains("current"));CHECK(!view.at("experience_contexts")[0].contains("completed"));CHECK(row.dump()==original);
    row.erase("experience_evidence");CHECK(personal::context(row,3).at("experience_contexts").empty());
    row["experience_evidence"]=Json::array();for(int i=0;i<10;++i)row["experience_evidence"].push_back(personal::anchor("puzzle","lights-"+std::to_string(i),1));CHECK(personal::context(row,3).at("experience_contexts").size()==6);
}
TEST_CASE(memory_relevance_does_not_mistake_shared_punctuation_for_a_shared_subject){
    const std::string cup="我找到杯子了，在书架最上层，难怪低头找了半天。现在终于能喝口水。",boat="纸船在橱窗里慢到一秒看不出来，但缺席两天就能看出位置变了。这个变化只在缺席里显形的角度让我在意。";
    CHECK(memoryRelevance(cup,boat)==0);CHECK(memoryRelevance("不用爬，踮一下脚就够了。你那边呢，刚才还在摆弄灯吗？",boat)==0);CHECK(memoryRelevance("，在。这个？","，在。另一个？")==0);CHECK(memoryTerms("。！？……，、\n🙂🙃").empty());CHECK(memoryRelevance("那座钟塔后来怎样了","钟塔外的雨声还没有停止")>0);CHECK(memoryRelevance("RAIN","Rain outside")>0);CHECK(memoryRelevance("星空，纸船","星空纸船")>0);CHECK(memoryRelevance("ハロー、世界。","世界")>0);
}
TEST_CASE(ordinary_intentions_have_indexes_unless_related_or_currently_pursued){
    Json thread={{"id","old-board"},{"revision",2},{"kind","intention"},{"topic","再试一小步"},{"note","PRIVATE_STALE_COUNTERS"},{"status","active"},{"attempts",Json::array({Json{{"purpose","PRIVATE_ATTEMPT"}}})},{"basis",{{"independent_experiences",3}}},{"experience_contexts",Json::array({Json{{"kind","puzzle"},{"id","lights-1"}}})}};
    const Json command={{"kind","wait"},{"intention",{{"id","old-board"},{"revision",2}}}};
    CHECK(personal::pursuedBy(thread,Json{{"active",command}}));CHECK(personal::pursuedBy(thread,Json{{"next",Json::array({command})}}));CHECK(!personal::pursuedBy(thread,Json{{"recent",Json::array({command})}}));auto stale=command;stale["intention"]["revision"]=1;CHECK(!personal::pursuedBy(thread,Json{{"entries",Json::array({stale})}}));
    personal::backgroundIndex(thread);CHECK(thread.at("id")=="old-board"&&thread.at("status")=="active"&&thread.at("revision")==2);CHECK(thread.at("experience_contexts")[0].at("id")=="lights-1");CHECK(thread.at("details_tool")=="self.observe"&&thread.dump().find("PRIVATE_")==std::string::npos);CHECK(!thread.contains("basis"));
}
TEST_CASE(earlier_encounters_get_one_bounded_opportunity_without_becoming_new_tasks_or_memories){
    Json rows=Json::array();for(int i=1;i<=3;++i){rows.push_back({{"id","user-"+std::to_string(i)},{"revision",1},{"kind","user_message"},{"request_id","request-"+std::to_string(i)},{"created_at",i*100}});rows.push_back({{"id","reply-"+std::to_string(i)},{"revision",1},{"kind","assistant_message"},{"request_id","request-"+std::to_string(i)},{"created_at",i*100+1}});}
    auto offered=Json::array();auto picks=conversationOpportunities(rows,offered);CHECK(picks.size()==4&&picks[2].at("id")=="user-3"&&picks[3].at("id")=="reply-3");CHECK(picks[0].at("id")=="user-2"&&picks[1].at("id")=="reply-2");CHECK(picks[0].at("context_role")=="earlier_encounter"&&picks[0].at("not_a_current_request")==true);
    for(auto& pick:picks)pick["kind"]=pick.at("id").get<std::string>().starts_with("user")?"user_message":"assistant_message";offered=rememberOfferedEncounters(offered,picks);CHECK(offered.size()==2);CHECK(rememberOfferedEncounters(offered,picks)==offered);
    auto second=conversationOpportunities(rows,offered);CHECK(second[0].at("id")=="user-1");for(auto& pick:second)pick["kind"]=pick.at("id").get<std::string>().starts_with("user")?"user_message":"assistant_message";offered=rememberOfferedEncounters(offered,second);CHECK(conversationOpportunities(rows,offered).size()==2); // the latest pair is still ordinary continuity
    rows[2]["revision"]=2;CHECK(conversationOpportunities(rows,offered)[0].at("id")=="user-2");rows.erase(rows.begin()+2,rows.begin()+4);CHECK(conversationOpportunities(rows,Json::array())[0].at("id")=="user-1"); // caller's access-filtered view cannot restore hidden origins
    CHECK(conversationOpportunities(Json::array(),offered).empty());rows=Json::array({Json{{"id","alone"},{"revision",1},{"kind","user_message"},{"created_at",42}}});CHECK(conversationOpportunities(rows,offered).size()==1);
    for(int i=0;i<300;++i)offered=rememberOfferedEncounters(offered,Json::array({Json{{"id","u"+std::to_string(i)},{"revision",1},{"kind","user_message"},{"context_role","earlier_encounter"}}}));CHECK(offered.size()==256);
}
TEST_CASE(activity_batches_accept_only_bounded_semantic_commands){
    Json plan={{"version",1},{"commands",Json::array({{{"kind","eat_nearest"},{"seconds",0}},{{"kind","rest"},{"seconds",30}},{{"kind","wait"},{"seconds",1}}})}};CHECK(validateActivities(plan.dump()).ok);
    plan["commands"][0]["kind"]="computer.launch";CHECK(!validateActivities(plan.dump()).ok);plan["commands"][0]["kind"]="eat_nearest";plan["commands"][0]["seconds"]=1;CHECK(!validateActivities(plan.dump()).ok);plan["commands"][0]["seconds"]=0;plan["commands"][1]["seconds"]=1801;CHECK(!validateActivities(plan.dump()).ok);
    plan["commands"]=Json::array();CHECK(validateActivities(plan.dump()).ok);for(int i=0;i<7;++i)plan["commands"].push_back({{"kind","wait"},{"seconds",1}});CHECK(!validateActivities(plan.dump()).ok);
}
TEST_CASE(activity_movement_and_poses_require_their_own_bounded_arguments){
    Json plan={{"version",1},{"commands",Json::array({{{"kind","move_to"},{"seconds",0},{"x",.8},{"y",.4}},{{"kind","pose"},{"seconds",2},{"state","blink"}},{{"kind","wait"},{"seconds",20}}})}};
    CHECK(validateActivities(plan.dump()).ok);plan["commands"][0].erase("y");CHECK(!validateActivities(plan.dump()).ok);plan["commands"][0]["y"]=1.1;CHECK(!validateActivities(plan.dump()).ok);plan["commands"][0]["y"]=.4;
    plan["commands"][1]["state"]="shell";CHECK(!validateActivities(plan.dump()).ok);plan["commands"][1]["state"]="blink";plan["commands"][2]["x"]=.2;CHECK(!validateActivities(plan.dump()).ok);
    CHECK(ayt::serializer::validateJsonSchema(activitySchema()));
}
TEST_CASE(body_projection_preserves_actual_animation_and_drops_rendering_diagnostics){
    const Json source={{"state","walk"},{"moving",true},{"x",.2},{"target_x",.8},{"animation",{{"requested","walk"},{"resolved","resting"},{"phase","end"},{"variant","lying-sleep"},{"image","private-path.png"},{"rendered_pixels",123}}},{"animation_catalog",Json::array({{{"state","resting"},{"meaning","躺下"}}})},{"paint_count",999},{"movement_token","internal"}};
    const auto view=widgetBodyContext(source);CHECK(view.at("moving")==true);CHECK(view.at("animation").at("resolved")=="resting");CHECK(view.at("animation").at("phase")=="end");CHECK(view.at("animation_catalog").size()==1);CHECK(!view.contains("paint_count"));CHECK(view.dump().find("private-path")==std::string::npos);CHECK(!view.contains("movement_token"));
    const Json queue={{"active",{{"id","internal"},{"kind","move_to"},{"seconds",0},{"x",.8},{"y",.4},{"token","internal"}}},{"entries",Json::array({{{"kind","pose"},{"seconds",2},{"state","blink"},{"source","internal-trace"}}})}};
    const auto activities=activityBodyContext(queue);CHECK(activities.at("active").at("kind")=="move_to");CHECK(activities.at("next")[0].at("state")=="blink");CHECK(activities.dump().find("internal")==std::string::npos);
}
TEST_CASE(dialogue_preparation_has_distinct_receive_and_prepare_phases){
    DialoguePreparation phase;CHECK(phase.phase(0)=="idle");phase.input(true);CHECK(phase.phase(0)=="receiving");phase.receive(10);phase.input(false);CHECK(phase.phase(10.2)=="receiving");CHECK(phase.phase(10.4)=="preparing");phase.finish();CHECK(phase.phase(10.5)=="idle");phase.receive(11);phase.finish();CHECK(!phase.pending());
}
TEST_CASE(drop_definitions_cannot_override_program_rules){
    const Json authored={{"name","月光布丁"},{"description","晃起来像一小块月光。"},{"tint","sky"},{"tags",Json::array({"柔软","甜点"})},{"traits",Json::array({{{"key","甜度"},{"value",3}},{{"key","发光"},{"value",true}}})}};
    SmallWorld world;CHECK(world.placeDetailed("pudding","pudding",.3,.4,authored,20,2,"model"));const auto item=world.object("pudding");CHECK(item.at("definition")==authored);CHECK(item.at("rules").at("edible")==true);CHECK(item.at("rules").at("nutrition")==24);CHECK(item.at("rules").at("rarity")=="rare");
    auto forged=authored;forged["nutrition"]=999;CHECK(!world.placeDetailed("forged","pudding",.3,.4,forged,20,0,"model"));forged=authored;forged["traits"][0]["key"]="nutrition";CHECK(!drops::validDefinition(forged));forged=authored;forged["traits"][0]["value"]=Json::object();CHECK(!drops::validDefinition(forged));forged=authored;forged["tags"]=Json::array({"重复","重复"});CHECK(!drops::validDefinition(forged));CHECK(world.size()==1);
    CHECK(world.request("pudding","eat","eat").empty());CHECK(world.beginWalk());CHECK(world.arrive("eat",.3,.4));for(int i=0;i<8;++i)world.tick(.25,true,.3,.4,true);CHECK(world.size()==0);CHECK(world.snapshot().at("target_appetite").get<double>()<.17);auto events=world.takeEvents();CHECK(events.back().at("item").at("definition").at("name")=="月光布丁");
}
TEST_CASE(drop_lifecycle_fades_and_pauses_without_erasing_early){
    SmallWorld world;CHECK(world.placeDetailed("drop","apple",.2,.4,Json::object(),8,0,"model"));CHECK(drops::opacity(world.object("drop"),world.clock())==0);world.tick(.25,true,0,0,false);const double early=drops::opacity(world.object("drop"),world.clock());CHECK(early>0&&early<1);for(int i=0;i<7;++i)world.tick(.25,true,0,0,false);CHECK(drops::opacity(world.object("drop"),world.clock())==1);
    const auto frozen=world.snapshot();world.tick(100,false,0,0,false);CHECK(world.snapshot()==frozen);for(int i=0;i<21;++i)world.tick(.25,true,0,0,false);CHECK(world.size()==1);CHECK(drops::opacity(world.object("drop"),world.clock())<1);CHECK(world.request("drop","eat","late")=="world_object_fading");for(int i=0;i<4;++i)world.tick(.25,true,0,0,false);CHECK(world.size()==0);unsigned expired=0;for(const auto& event:world.takeEvents())if(event.at("kind")=="habitat.disappeared")++expired;CHECK(expired==1);
}
TEST_CASE(drop_save_migrates_legacy_objects_and_rejects_forged_rules){
    SmallWorld world;CHECK(world.place("old","berry",.2,.3));auto old=world.snapshot();old["version"]=1;for(auto& item:old["objects"]){item.erase("definition");item.erase("rules");item.erase("creator");}SmallWorld restored;CHECK(restored.restore(old));CHECK(restored.object("old").at("creator")=="legacy");CHECK(restored.object("old").at("definition").at("name")=="浆果");const auto saved=restored.snapshot();SmallWorld next;CHECK(next.restore(saved));CHECK(next.snapshot()==saved);auto forged=saved;forged["objects"][0]["rules"]["nutrition"]=999;CHECK(!next.restore(forged));CHECK(next.snapshot()==saved);
}
TEST_CASE(drop_pixels_are_premultiplied_without_color_key_edges){
    for(const auto& kind:drops::kinds())for(const auto alpha:{1u,63u,128u,255u}){std::vector<std::uint32_t> pixels(80*80);const auto frame=deskAppearance::make(kind.get<std::string>(),0,2,true,1,"sky",true);deskAppearance::raster(pixels.data(),80,80,frame,40,65,alpha);bool visible=false,valid=true;for(auto pixel:pixels){const auto a=pixel>>24;visible=visible||a>0;valid=valid&&a<=alpha&&((pixel>>16)&255)<=a&&((pixel>>8)&255)<=a&&(pixel&255)<=a;}CHECK(visible&&valid);CHECK(pixels.front()==0&&pixels.back()==0);}
}
TEST_CASE(character_sequence_packs_supply_stages_and_consistent_canvas){
    for(const auto* skin:{"dinosaur","moonbird","lumen"}){auto pack=character::loadPack(std::filesystem::path(HA_PROJECT_ROOT)/"plugins/widget/characters"/skin);CHECK(pack->states.contains("eating"));CHECK(pack->mouth.has_value());CHECK(!pack->states.at("resting")[0].start.frames.empty());CHECK(!pack->states.at("resting")[0].end.frames.empty());CHECK(pack->resolve("unknown_state")=="idle");CHECK(pack->resolve("thinking")=="preparing");CHECK(pack->states.at("eating")[0].loop.frames[0].hand.has_value());}
}
TEST_CASE(character_sequence_transitions_finish_and_urgent_input_interrupts){
    auto pack=character::loadPack(std::filesystem::path(HA_PROJECT_ROOT)/"plugins/widget/characters/dinosaur");character::Animator animation;animation.bind(pack);CHECK(animation.select("idle",0).phase=="loop");CHECK(animation.select("resting",.1).phase=="start");CHECK(animation.select("resting",.6).phase=="loop");CHECK(animation.select("idle",.7).phase=="end");CHECK(animation.select("idle",1.2).resolved=="idle");CHECK(animation.select("resting",1.3).phase=="start");CHECK(animation.select("dragged",1.4).resolved=="dragged");auto paused=animation.select("dragged",1.4);CHECK(animation.select("dragged",1.4).index==paused.index);CHECK(animation.select("resting",1.5,true).index==0);CHECK(animation.select("resting",2.1,true).phase=="loop");
}
TEST_CASE(asterlan_pack_preserves_six_fps_and_complete_transition_order){
    auto pack=character::loadPack(std::filesystem::path(HA_PROJECT_ROOT)/"plugins/widget/characters/asterlan");
    CHECK(pack->width==168&&pack->height==168&&pack->anchorY==144);
    const auto& idle=pack->states.at("idle")[0].loop;
    CHECK(idle.frames.size()==17);for(const auto& frame:idle.frames)CHECK(frame.image.starts_with("frames/idle/south/"));
    CHECK(pack->states.at("breathing")[0].loop.frames[0].image.starts_with("frames/breathing/south/"));
    for(const auto& frame:pack->states.at("listening")[0].loop.frames)CHECK(frame.image.starts_with("frames/idle/south/"));
    const auto& walk=pack->states.at("walk")[0];const auto& rest=pack->states.at("resting")[0];
    CHECK(walk.start.frames.size()==9&&walk.loop.frames.size()==17&&walk.end.frames.size()==9);
    CHECK(walk.start.timeline.durationUs()==1500000);CHECK(rest.start.timeline.durationUs()==1500000);CHECK(rest.end.timeline.durationUs()==1500000);
    CHECK(rest.loop.frames.size()==1&&rest.loop.frames[0].image=="poses/sleeping/south.png");
    const auto& landing=pack->states.at("landing")[0].start;
    CHECK(landing.frames.size()==12&&landing.timeline.durationUs()==2000000);
    CHECK(landing.frames.front().image=="frames/drag-drop/south/005.png");
    const auto& pickup=pack->states.at("dragged_side")[0].start;
    CHECK(pickup.frames.size()==9&&pickup.timeline.durationUs()==495000);
    CHECK(pickup.frames.front().image==walk.start.frames.back().image&&pickup.frames.back().image==walk.start.frames.front().image);
    for(const auto* clip:{&walk.start,&walk.end,&pickup})CHECK(std::any_of(clip->frames.begin(),clip->frames.end(),[](const auto& frame){return frame.image=="frames/trun/south/001.png";}));
    CHECK(walk.start.frames.front().image==walk.end.frames.back().image);
    CHECK(walk.start.frames.back().image==walk.end.frames.front().image);
    CHECK(pack->resolve("preparing")=="listening");CHECK(!pack->mouth.has_value());
    character::Animator animation;animation.bind(pack);animation.select("idle",0);
    CHECK(animation.select("walk",.1).phase=="start");CHECK(animation.select("walk",1.7).phase=="loop");
    CHECK(animation.select("idle",1.8).phase=="end");CHECK(animation.select("idle",3.4).resolved=="idle");
    CHECK(animation.select("resting",4).phase=="start");CHECK(animation.select("resting",5.6).phase=="loop");
    auto waking=animation.select("walk",6);CHECK(waking.phase=="end"&&waking.resolved=="resting");
    auto turning=animation.select("walk",7.6);CHECK(turning.phase=="start"&&turning.resolved=="walk");
    CHECK(animation.select("walk",9.2).phase=="loop");
    CHECK(animation.select("dragged",9.3).resolved=="dragged");
    animation.interruptAt("dragged_side",9.31,.165);auto picked=animation.select("dragged_side",9.31);CHECK(picked.phase=="start"&&picked.index==3);CHECK(animation.select("dragged_side",9.81).phase=="loop");
    CHECK(animation.select("landing",9.9).phase=="start");CHECK(animation.select("landing",12.3).phase=="loop");
}
TEST_CASE(widget_lift_and_drop_keep_ground_position_independent_and_interrupt_smoothly){
    WidgetDragMotion motion;motion.grab(1,24);CHECK(motion.height(1)==0);CHECK(motion.height(1.06)>12);CHECK(motion.height(1.13)==24);
    motion.release(2);CHECK(motion.falling(2));CHECK(motion.height(2)==24);CHECK(motion.height(2.1)>0&&motion.height(2.1)<24);
    const auto inFlight=motion.height(2.1);motion.grab(2.1,24);CHECK(motion.height(2.1)==inFlight);CHECK(!motion.falling(2.1));
    motion.release(3);CHECK(!motion.falling(3.21));CHECK(motion.height(3.21)==0);motion.grab(4,24,true);CHECK(motion.height(4)==0);motion.release(4,true);CHECK(!motion.falling(4));
}
TEST_CASE(widget_pointer_click_and_jitter_never_become_drag_until_threshold_crossed){
    WidgetPointerGesture pointer;pointer.press(100,100);CHECK(pointer.pressed());for(int i=0;i<100;++i)CHECK(!pointer.move(100+i%3,100-i%3));CHECK(pointer.release()==WidgetPointerRelease::click);CHECK(!pointer.pressed());CHECK(pointer.release()==WidgetPointerRelease::none);
    pointer.press(100,100);CHECK(!pointer.move(105,105));CHECK(pointer.move(106,100));CHECK(!pointer.move(100,100));CHECK(pointer.release()==WidgetPointerRelease::drag);
    pointer.press(100,100,10,8);CHECK(!pointer.move(109,107));CHECK(pointer.move(100,92));pointer.cancel();CHECK(pointer.release()==WidgetPointerRelease::none);
}
TEST_CASE(widget_deferred_pose_waits_for_reply_and_cannot_replay_after_new_input_or_expiry){
    WidgetPoseQueue queue;queue.request("resting",60,0,3);queue.attention(3);CHECK(!queue.take(10,false));CHECK(queue.state()=="resting");
    auto pose=queue.take(11,true);CHECK(pose&&pose->state=="resting"&&pose->seconds==60);CHECK(!queue.take(12,true));
    queue.request("resting",60,20,3);queue.attention(4);CHECK(!queue.pending());CHECK(!queue.take(21,true));
    queue.request("breathing",5,30,4);CHECK(!queue.take(151,true));queue.request("resting",60,160,4);queue.cancel();CHECK(!queue.pending());
}
TEST_CASE(tool_images_detach_bounded_ordered_frames_without_pixels_in_receipts){
    auto image=Json{{"mime_type","image/jpeg"},{"base64","YWJjZA=="}};Json result={{"image",image},{"frames",Json::array({Json{{"index",0},{"image",image}},Json{{"index",1},{"image",image}},Json{{"index",2},{"image",image}}})}};
    const auto parts=detachToolImages(result,true,3,1);CHECK(parts.size()==3);CHECK(parts[0].frame==-1&&parts[1].frame==0&&parts[2].frame==1);CHECK(result.at("image_part_index")==1);CHECK(result.at("frames")[1].at("image_part_index")==3);CHECK(result.at("frames")[2].at("image_unavailable_reason")=="per_batch_image_limit");CHECK(result.dump().find("YWJjZA")==std::string::npos);
    result={{"frames",Json::array({Json{{"image",image}}})}};CHECK(detachToolImages(result,false,4).empty());CHECK(result.at("frames")[0].at("image_unavailable_reason")=="vision_not_enabled");CHECK(result.dump().find("base64")==std::string::npos);
    result={{"image",{{"mime_type","image/svg+xml"},{"base64","YWJjZA=="}}}};CHECK(detachToolImages(result,true,4).empty());CHECK(result.at("image_unavailable_reason")=="invalid_tool_image");
    result={{"image",{{"mime_type","image/jpeg"},{"base64","not base64 <script>"}}}};CHECK(detachToolImages(result,true,4).empty());CHECK(!result.contains("image"));
    for(const auto* encoded:{"A","AAAA=","AA=A","====","A==="}){result={{"image",{{"mime_type","image/png"},{"base64",encoded}}}};CHECK(detachToolImages(result,true,4).empty());CHECK(!result.contains("image"));}CHECK(boundedImageBase64("AAAA"));CHECK(boundedImageBase64("AAA="));CHECK(boundedImageBase64("AA=="));
    const auto receipt=compactObservation(Json{{"kind","tool_observation"},{"tool","television.observe_sequence"},{"result",{{"scope","sampled_playback_frames"},{"sampled_frames",2},{"span_seconds",.7},{"full_video_watched",false},{"audio_delivered",false},{"frames",Json::array({Json{{"index",0},{"frame_time_seconds",1},{"observed_at_ms",10},{"image",image}}})}}}},true);
    CHECK(receipt.at("result").at("images_delivered_to_model"));CHECK(receipt.at("result").at("sampled_frames")==2);CHECK(receipt.dump().find("base64")==std::string::npos);CHECK(!receipt.at("result").at("full_video_watched").get<bool>());
}
TEST_CASE(character_plugin_context_keeps_real_affordances_and_omits_rendering_telemetry){
    const Json habitat={{"objects",Json::array({{{"id","apple-1"},{"x",.3},{"definition",{{"name","苹果"},{"properties",{{"sweetness",.4}}}}}}})},{"pending",{{"object_id","apple-1"},{"action","eat"}}},{"appetite",.5},{"creation_policy",{{"max",8}}},{"paint_count",800},{"surface_left",-1920},{"clock",55}};
    const auto world=pluginCharacterContext("habitat",habitat);CHECK(world.at("objects")==habitat.at("objects"));CHECK(world.at("pending")==habitat.at("pending"));CHECK(world.at("creation_policy")==habitat.at("creation_policy"));CHECK(world.at("appetite")==.5);CHECK(!world.contains("paint_count")&&!world.contains("surface_left")&&!world.contains("clock"));CHECK(habitat.at("paint_count")==800);
    const Json television={{"source","materials/movie.mp4"},{"state","playing"},{"position_seconds",2.5},{"audio_available",true},{"screen_rect",{{"x",20}}},{"frame_width",640},{"sync_source","AudioMaster"},{"error",""}};
    const auto screen=pluginCharacterContext("television",television);CHECK(screen.at("source")==television.at("source"));CHECK(screen.at("position_seconds")==2.5);CHECK(screen.at("audio_available").get<bool>());CHECK(!screen.contains("frame_width")&&!screen.contains("screen_rect")&&!screen.contains("sync_source"));
    CHECK(pluginCharacterContext("tts",Json{{"active",false},{"queued_replies",1},{"prepared_hits",900}})==Json({{"active",false},{"queued_replies",1}}));
    const Json custom={{"tool_required_id","actual-id"},{"revision",3},{"custom",true}};CHECK(pluginCharacterContext("future-plugin",custom)==custom);CHECK(pluginCharacterContext("pocket",custom)==custom);
    const Json reading={{"last_search",{{"pages",Json::array({{{"key","Light"}}})}}},{"last_read",{{"excerpt","真实节选"}}},{"service_origin","https://configured.invalid"},{"requests_used",2},{"request_limit",12}};
    const auto reference=pluginCharacterContext("reading",reading);CHECK(reference.at("last_search")==reading.at("last_search"));CHECK(reference.at("last_read")==reading.at("last_read"));CHECK(reference.at("request_limit")==12);CHECK(!reference.contains("service_origin"));
}
TEST_CASE(ordinary_conversation_has_background_indexes_while_deliberate_observation_keeps_details){
    const Json objects=Json::array({{{"id","seed"},{"definition",{{"name","one persistent seed"}}}}});
    const Json full=pluginCharacterContext("habitat",Json{{"objects",objects},{"pending",{{"action","look"},{"object_id","seed"}}},{"visible",true},{"appetite",.4}});
    const auto ambient=pluginDialogueContext("habitat",full);CHECK(ambient.at("object_count")==1);CHECK(!ambient.contains("objects"));CHECK(ambient.at("pending")==full.at("pending"));CHECK(ambient.at("details_tool")=="habitat.observe");CHECK(full.at("objects")==objects);
    const Json work={{"piece_count",1},{"can_create",true},{"continuations",Json::array({{{"tail","old private story"}}})},{"pieces",Json::array({{{"id","draft-1"}}})}};
    const auto index=pluginDialogueContext("atelier",work);CHECK(index.at("piece_count")==1);CHECK(index.at("can_create").get<bool>());CHECK(!index.contains("continuations")&&!index.contains("pieces"));CHECK(pluginCharacterContext("atelier",work)==work);
    const Json pocket={{"current",{{"game",{{"id","game-1"},{"revision",3},{"status","active"},{"lit_count",5},{"rows",Json::array({Json::array({true,false,true})})}}}}}};
    const auto game=pluginDialogueContext("pocket",pocket).at("game");CHECK(game.at("id")=="game-1"&&game.at("revision")==3&&game.at("lit_count")==5);CHECK(!game.contains("rows"));CHECK(pluginDialogueContext("pocket",Json{{"current",nullptr}}).at("game").is_null());
    const Json custom={{"required_by_custom_tool","keep-me"}};CHECK(pluginDialogueContext("new-plugin",custom)==custom);
}
TEST_CASE(activity_purpose_is_optional_bounded_and_never_becomes_its_outcome){
    Json command={{"kind","wait"},{"seconds",1},{"purpose","想看看会不会长出一座森林"}};auto plan=Json{{"version",1},{"commands",Json::array({command})}};CHECK(validateActivities(plan.dump()).ok);
    const auto view=activityBodyContext(Json{{"recent",Json::array({command})}});CHECK(view.at("recent")[0].at("purpose")==command.at("purpose"));CHECK(!view.at("recent")[0].contains("completed"));CHECK(view.contains("purpose_meaning"));
    plan["commands"][0]["purpose"]=std::string(161,'a');CHECK(!validateActivities(plan.dump()).ok);plan["commands"][0]["purpose"]=" \t\n";CHECK(validateActivities(plan.dump()).code=="activity_empty_purpose");plan["commands"][0]["purpose"]=8;CHECK(!validateActivities(plan.dump()).ok);
    plan["commands"][0].erase("purpose");CHECK(validateActivities(plan.dump()).ok);plan["commands"][0]["grant_permission"]=true;CHECK(!validateActivities(plan.dump()).ok);
}
TEST_CASE(activity_can_reference_an_existing_intention_without_promoting_an_outcome){
    Json command={{"kind","wait"},{"seconds",1},{"intention",{{"id","look-later"},{"revision",1}}}};
    CHECK(validateActivities(Json{{"version",1},{"commands",Json::array({command})}}.dump()).ok);
    const auto view=activityBodyContext(Json{{"entries",Json::array({command})}});CHECK(view.at("next")[0].at("intention")==command.at("intention"));
    auto altered=command;altered["intention"]["revision"]=0;CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({altered})}}.dump()).ok);
    altered=command;altered["intention"]["completed"]=true;CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({altered})}}.dump()).ok);
    command["status"]="completed";const auto reusable=reusableActivity(command);CHECK(reusable);CHECK(!reusable->contains("intention"));
    const auto activity=Json::parse(activitySchema());const Json parameters={{"type","object"},{"additionalProperties",false},{"properties",{{"commands",activity.at("properties").at("commands")}}},{"required",Json::array({"commands"})}};
    const NativeToolBridge bridge(Json::array({{{"name","activity.enqueue"},{"parameters",parameters}}}));
    const auto optional=bridge.candidate({{"call","ha_activity_DOT_enqueue",R"({"commands":[{"kind":"wait","seconds":1,"purpose":null,"intention":null}]})"}},1);CHECK(optional.ok);CHECK(!optional.value.at("calls")[0].at("args").at("commands")[0].contains("intention"));
    CHECK(!bridge.candidate({{"call","ha_activity_DOT_enqueue",R"({"commands":[{"kind":"wait","seconds":null}]})"}},1).ok);
    CHECK(!bridge.candidate({{"call","ha_activity_DOT_enqueue",R"({"commands":[{"kind":"wait","seconds":1,"unknown":null}]})"}},1).ok);
}
TEST_CASE(background_private_carry_is_optional_and_cannot_replace_the_action_contract){
    Json note={{"id","own-idea"},{"expected_revision",0},{"kind","intention"},{"topic","a short unfinished story"},{"note","return to its last sentence later"},{"stance","want"},{"source_ids",Json::array({"actual-work"})}};
    Json plan={{"version",1},{"commands",Json::array()},{"carry_forward",Json::array({note})}};
    CHECK(validateActivities(plan.dump()).ok);
    CHECK(ayt::serializer::validateJsonDocument(plan.dump(),activitySchemaFor(Json::array())));
    plan["carry_forward"].push_back(note);CHECK(!validateActivities(plan.dump()).ok);
    plan["carry_forward"]=nullptr;const auto empty=validateActivities(plan.dump());CHECK(empty.ok&&!empty.value.contains("carry_forward"));
    plan["carry_forward"]=Json::array({note});plan["carry_forward"][0]["authorize_tools"]=true;CHECK(!validateActivities(plan.dump()).ok);
    plan["carry_forward"]=Json::array({note});plan["commands"]=Json::array({{{"kind","wait"},{"seconds",0}}});CHECK(!validateActivities(plan.dump()).ok);
}
TEST_CASE(single_private_collection_items_are_canonicalized_without_repairing_their_meaning){
    const Json note={{"id","small-idea"},{"expected_revision",0},{"kind","interest"},{"topic","a paper star"},{"note","perhaps leave one near the board"},{"stance","curious"},{"source_ids",Json::array({"actual-board"})}};
    const Json close={{"id","old-idea"},{"expected_revision",1},{"outcome","released"},{"note","not now"},{"source_ids",Json::array({"actual-board"})}};
    Json value={{"version",1},{"commands",Json::array({{{"kind","wait"},{"seconds",1}}})},{"carry_forward",note},{"close_threads",close}},changes;
    CHECK(!ayt::serializer::validateJsonDocument(value.dump(),activitySchema()));const auto checked=validateActivities(value.dump(),&changes);CHECK(checked.ok&&changes.size()==2);CHECK(checked.value.at("carry_forward")==Json::array({note})&&checked.value.at("close_threads")==Json::array({close}));CHECK(ayt::serializer::validateJsonDocument(checked.value.dump(),activitySchema()));CHECK(value.at("carry_forward").is_object());
    CHECK(validateActivities(checked.value.dump(),&changes).ok&&changes.empty()); // canonical retries do not adapt again
    auto bad=value;bad["carry_forward"]["source_ids"]="actual-board";CHECK(!validateActivities(bad.dump()).ok);bad=value;bad["carry_forward"].erase("expected_revision");CHECK(!validateActivities(bad.dump()).ok);bad=value;bad["close_threads"]["grant_permissions"]=true;CHECK(!validateActivities(bad.dump()).ok);bad=value;bad["commands"]=bad.at("commands")[0];CHECK(!validateActivities(bad.dump()).ok);bad=value;bad["carry_forward"]=Json::array({note,note});CHECK(!validateActivities(bad.dump()).ok);
    bad=value;bad["close_threads"]["id"]=note.at("id");CHECK(validateActivities(bad.dump()).code=="activity_personal_conflict");bad=value;bad["commands"][0]["intention"]={{"id","old-idea"},{"revision",1}};CHECK(validateActivities(bad.dump()).code=="activity_closed_intention_still_queued");
    const auto restricted=activitySchemaFor(Json::array());CHECK(!ayt::serializer::validateJsonDocument(checked.value.dump(),restricted));
}
TEST_CASE(an_autonomous_game_can_be_left_without_faking_completion_or_replaying_it){
    Json command={{"kind","puzzle_leave"},{"seconds",0},{"game_id","lights-1"},{"expected_revision",4},{"purpose","先放下这局，想去看看别的"}};
    const auto wrap=[&]{return Json{{"version",1},{"commands",Json::array({command})}}.dump();};
    CHECK(validateActivities(wrap()).ok);CHECK(ayt::serializer::validateJsonDocument(wrap(),activitySchemaFor(Json::array({"puzzle_leave","wait"}))));
    CHECK(!ayt::serializer::validateJsonDocument(wrap(),activitySchemaFor(Json::array({"puzzle_start","wait"}))));CHECK(!reusableActivity(command));
    command["cell"]=3;CHECK(!validateActivities(wrap()).ok);command.erase("cell");command["seconds"]=10;CHECK(!validateActivities(wrap()).ok);
}
TEST_CASE(closing_one_intention_preserves_other_pending_life_and_records_what_never_started){
    const Json old={{"id","old"},{"revision",1}},other={{"id","other"},{"revision",2}};
    const Json plan={{"active",{{"id","running"},{"intention",old},{"started_ms",1}}},{"entries",Json::array({
        {{"id","obsolete"},{"intention",old},{"expires_ms",100}},{{"id","free"},{"expires_ms",100}},
        {{"id","different-goal"},{"intention",other},{"expires_ms",100}},{{"id","expired"},{"intention",other},{"expires_ms",10}}
    })}};
    const auto result=pruneActivityIntentions(plan,10,[&](const Json& binding){return binding==other;});
    CHECK(result.stopActive);CHECK(result.pending.size()==2);CHECK(result.pending[0].at("id")=="free"&&result.pending[1].at("id")=="different-goal");
    CHECK(result.removed.size()==2);CHECK(result.removed[0].at("status")=="not_started_intention_changed");CHECK(result.removed[1].at("status")=="not_started_expired");
    CHECK(!result.removed[0].contains("started_ms"));CHECK(plan.at("entries").size()==4&&plan.at("active").at("id")=="running");
}
TEST_CASE(background_closure_keeps_goal_identity_and_rejects_contradictory_batches){
    const Json close={{"id","old"},{"expected_revision",1},{"outcome","released"},{"note","先放下"},{"source_ids",Json::array({"experience"})}};
    Json plan={{"version",1},{"commands",Json::array()},{"close_threads",Json::array({close})}};CHECK(validateActivities(plan.dump()).ok);
    plan["commands"]=Json::array({{{"kind","wait"},{"seconds",1},{"intention",{{"id","old"},{"revision",1}}}}});CHECK(validateActivities(plan.dump()).code=="activity_closed_intention_still_queued");
    plan["commands"]=Json::array();plan["carry_forward"]=Json::array({{{"id","old"},{"expected_revision",1},{"kind","interest"},{"topic","old"},{"note","new"},{"stance","curious"},{"source_ids",Json::array({"experience"})}}});CHECK(validateActivities(plan.dump()).code=="activity_personal_conflict");
    plan.erase("carry_forward");plan["close_threads"]=nullptr;CHECK(validateActivities(plan.dump()).ok);plan["close_threads"]=Json::array({close,close});CHECK(!validateActivities(plan.dump()).ok);
    CHECK(personalCompletionObservation(Json{{"kind","tool_observation"},{"tool","pocket.press"},{"result",{{"applied",true},{"completed",false}}}}));
    CHECK(!personalCompletionObservation(Json{{"kind","tool_intent"},{"tool","activity.enqueue"},{"result",{{"accepted",1},{"completed",false}}}}));
    CHECK(!personalCompletionObservation(Json{{"kind","personal_attempt"},{"tool","self.observe"},{"result",{{"completed",true}}}}));
}
TEST_CASE(finite_action_default_is_a_program_constant_without_inventing_required_choices){
    Json input={{"version",1},{"commands",Json::array({{{"kind","move_to"},{"x",.3},{"y",.4}}})}};
    const auto omitted=validateActivities(input.dump());CHECK(omitted.ok&&omitted.value.at("commands")[0].at("seconds")==0);CHECK(!input.at("commands")[0].contains("seconds"));
    input["commands"][0]["seconds"]=nullptr;CHECK(validateActivities(input.dump()).ok);
    input["commands"][0]["seconds"]=3;CHECK(!validateActivities(input.dump()).ok);
    input["commands"][0].erase("seconds");input["commands"][0].erase("x");CHECK(!validateActivities(input.dump()).ok);
    for(const auto* kind:{"wait","pose","rest"}){Json command={{"kind",kind}};if(std::string_view(kind)=="pose")command["state"]="idle";input["commands"]=Json::array({command});CHECK(!validateActivities(input.dump()).ok);input["commands"][0]["seconds"]=nullptr;CHECK(!validateActivities(input.dump()).ok);}
    OperationReplay replay;const Json plain={{"commands",Json::array({{{"kind","create_drop"},{"template","seed"},{"name","a seed"},{"description","small"},{"tint","mint"}}})}};
    auto explicitZero=plain;explicitZero["commands"][0]["seconds"]=0;replay.completed("activity.enqueue",plain,{{"ok",true},{"result",{{"accepted",1},{"queued",1},{"completed",false}}}},"first");replay.afterReviewRejection();CHECK(replay.reuse("activity.enqueue",explicitZero).has_value());
    explicitZero["commands"][0]["seconds"]=1;CHECK(!replay.reuse("activity.enqueue",explicitZero));
}
TEST_CASE(puzzle_progress_is_an_actual_step_even_when_the_larger_game_is_unfinished){
    const Json progress={{"applied",true},{"completed",false},{"game",{{"id","one"},{"revision",2},{"status","active"},{"moves",1},{"lit_count",6}}}};
    CHECK(toolObservationKind("pocket.press",progress)=="tool_observation");CHECK(toolObservationKind("activity.enqueue",progress)=="tool_intent");
    CHECK(toolObservationKind("habitat.act",Json{{"accepted",true},{"completed",false}})=="tool_intent");
    const auto view=compactObservation(Json{{"kind","tool_observation"},{"tool","pocket.press"},{"result",progress}},false);CHECK(view.at("result").at("applied").get<bool>());CHECK(view.at("result").at("game").at("lit_count")==6);CHECK(view.at("result").at("completed")==false);
    Json record={{"kind","tool_observation"},{"tool","pocket.press"},{"result",progress}};CHECK(activityConsiderationKey(record).empty());record["result"]["game"]["status"]="won";CHECK(!activityConsiderationKey(record).empty());
    record={{"kind","tool_observation"},{"tool","habitat.create"},{"result",{{"object",{{"id","seed-1"},{"creator","model"}}}}}};CHECK(!activityConsiderationKey(record).empty());record["result"]["object"]["creator"]="environment";CHECK(activityConsiderationKey(record).empty());
}
TEST_CASE(scheduled_visual_evidence_preserves_sources_without_becoming_base64_text){
    const Json image={{"mime_type","image/jpeg"},{"base64",std::string(30000,'A')}};
    const Json raw={{"kind","tool_observation"},{"tool","television.observe_sequence"},{"created_at",42},{"result",{{"content","真实的文字说明"},{"source","materials/movie.mp4"},{"image",image},{"frames",Json::array({Json{{"index",0},{"frame_time_seconds",2},{"image",image}}})}}}};
    const auto context=referencedObservationContext(raw);CHECK(context.dump().find("base64")==std::string::npos);CHECK(context.at("result").at("content")==raw.at("result").at("content"));CHECK(context.at("result").at("frames")[0].at("frame_time_seconds")==2);CHECK(context.at("result").at("frames")[0].at("image_available_in_source").get<bool>());CHECK(!context.at("result").at("image_content_in_context").get<bool>());CHECK(context.at("kind")=="tool_observation");CHECK(raw.at("result").at("image")==image);
    const Json ordinary={{"kind","user_message"},{"text","image is a word in this original text"}};CHECK(referencedObservationContext(ordinary)==ordinary);
    CHECK(revisitableObservationTool("workspace.view_image"));CHECK(revisitableObservationTool("reading.open"));CHECK(!revisitableObservationTool("self.observe"));CHECK(!revisitableObservationTool("memory.read"));
    CHECK(continuityObservationTool("workspace.view_image"));CHECK(continuityObservationTool("atelier.write"));CHECK(continuityObservationTool("computer.open_application"));CHECK(!continuityObservationTool("self.observe"));CHECK(!continuityObservationTool("body.observe"));CHECK(!continuityObservationTool("unknown.diagnostics"));
    Json trace={{"http_status",200},{"visual_inputs",Json::array({Json{{"source_id","picture"},{"attached",true},{"delivery_state","prepared"}}})}};CHECK(observedImageDelivery(trace,"picture")=="prepared_only");trace["visual_inputs"][0]["delivery_state"]="acknowledged";CHECK(observedImageDelivery(trace,"picture")=="acknowledged");trace["visual_inputs"][0]["delivery_state"]="unknown";CHECK(observedImageDelivery(trace,"picture")=="unknown");CHECK(observedImageDelivery(trace,"missing")=="no_saved_receipt");const auto unknown=compactObservation(raw,false,"no_saved_receipt");CHECK(!unknown.at("result").contains("image_delivered_to_model"));CHECK(unknown.at("result").at("image_delivery_status")=="no_saved_receipt");
}
TEST_CASE(private_activity_consideration_is_sourced_deduplicated_and_time_bounded){
    auto saved=emptyActivityConsideration();CHECK(activityConsiderationDue(saved,1000,false));
    const Json authored={{"kind","tool_observation"},{"tool","atelier.append"},{"result",{{"written",true},{"completed",true},{"piece",{{"id","story-1"},{"revision",2}}}}}};
    const auto key=activityConsiderationKey(authored);CHECK(!key.empty());CHECK(!activityAlreadyConsidered(saved,key));saved=markActivitiesConsidered(saved,Json::array({key,key}),1000);CHECK(saved.at("considered").size()==1);CHECK(activityAlreadyConsidered(saved,key));CHECK(!activityConsiderationDue(saved,899999,false));CHECK(activityConsiderationDue(saved,901000,false));CHECK(activityConsiderationDue(saved,1001,true));CHECK(!activityConsiderationDue(saved,999,true));
    auto newer=authored;newer["result"]["piece"]["revision"]=3;CHECK(activityConsiderationKey(newer)!=key);newer["kind"]="tool_intent";CHECK(activityConsiderationKey(newer).empty());
    const Json reading={{"kind","tool_observation"},{"tool","reading.open"},{"result",{{"scope","article_lead_summary"},{"text","真实摘要"},{"source_url","https://example.test/summary/Light"},{"revision","4"},{"retrieved_ms",1000}}}};auto cached=reading;cached["result"]["cache_hit"]=true;CHECK(activityConsiderationKey(reading)==activityConsiderationKey(cached));CHECK(!activityConsiderationKey(reading).empty());
    CHECK(activityConsiderationKey(Json{{"kind","tool_observation"},{"tool","widget.move"},{"result",{{"completed",true}}}}).empty());
    auto invalid=saved;invalid["version"]="future";CHECK(!validActivityConsideration(invalid));CHECK(!activityConsiderationDue(invalid,999999,true));CHECK(markActivitiesConsidered(invalid,Json::array({"new"}),999999)==invalid);
    Json many=Json::array();for(int i=0;i<150;++i)many.push_back("key-"+std::to_string(i));CHECK(markActivitiesConsidered(saved,many,2000).at("considered").size()==128);
}
TEST_CASE(review_repair_reuses_exact_prior_operations_without_reexecuting_or_inventing_completion){
    OperationReplay replay;const Json args={{"commands",Json::array({Json{{"kind","wait"},{"seconds",1}}})}},receipt={{"ok",true},{"result",{{"accepted",1},{"queued",1},{"completed",false}}},{"source_id","receipt-1"}};
    CHECK(!replay.reuse("activity.enqueue",args));replay.completed("activity.enqueue",args,receipt,"call-1");replay.completed("activity.enqueue",args,receipt,"call-1");CHECK(replay.executionFacts().size()==1);CHECK(replay.executionFacts()[0].at("result").at("queued")==1);CHECK(!replay.reuse("activity.enqueue",args));
    replay.afterReviewRejection();const auto reused=replay.reuse("activity.enqueue",args);CHECK(reused&&reused->at("reused_after_review").get<bool>());CHECK(reused->at("source_id")=="receipt-1");CHECK(!reused->at("result").at("completed").get<bool>());replay.completed("activity.enqueue",args,*reused,"call-2");CHECK(replay.executionFacts().size()==1);CHECK(replay.reuse("activity.enqueue",args)->at("error").at("code")=="operation_already_recorded_after_review");
    auto different=args;different["commands"][0]["seconds"]=2;CHECK(!replay.reuse("activity.enqueue",different));replay.afterReviewRejection();CHECK(replay.reuse("activity.enqueue",args)->at("source_id")=="receipt-1");
    OperationReplay multiple;multiple.completed("habitat.create",Json::object(),receipt,"first");auto second=receipt;second["source_id"]="receipt-2";multiple.completed("habitat.create",Json::object(),second,"second");multiple.afterReviewRejection();CHECK(multiple.reuse("habitat.create",Json::object())->at("source_id")=="receipt-1");CHECK(multiple.reuse("habitat.create",Json::object())->at("source_id")=="receipt-2");
    CHECK(!replayProtectedOperation("reading.open"));CHECK(!replayProtectedOperation("workspace.read"));CHECK(replayProtectedOperation("memory.remember"));OperationReplay nextTurn;CHECK(!nextTurn.reuse("activity.enqueue",args));
    OperationReplay numeric;const Json integerPosition={{"x",1},{"y",.5}},floatPosition={{"x",1.0},{"y",.5}};numeric.completed("habitat.arrange",integerPosition,receipt,"numeric-1");numeric.afterReviewRejection();CHECK(numeric.reuse("habitat.arrange",floatPosition)->at("source_id")=="receipt-1");
    OperationReplay large;large.completed("habitat.arrange",Json{{"x",9007199254740993LL}},receipt,"large-1");large.afterReviewRejection();CHECK(!large.reuse("habitat.arrange",Json{{"x",9007199254740992LL}}));
}
TEST_CASE(native_execution_identity_survives_retry_reordering_and_keeps_real_repetition_separate){
    NativeExecutionIds original,retry;const Json a={{"path","a.txt"},{"content","one"}},b={{"path","b.txt"},{"content","two"}};
    const auto a0=original.next("workspace.write",a),a1=original.next("workspace.write",a),b0=original.next("workspace.write",b);CHECK(a0&&a1&&b0);CHECK(*a0!=*a1&&*a0!=*b0);CHECK(a0->size()<=32);
    CHECK(retry.next("workspace.write",b)==b0);CHECK(retry.next("workspace.write",a)==a0);CHECK(retry.next("workspace.write",a)==a1);
    NativeExecutionIds integer,floating;CHECK(integer.next("test",Json{{"n",1}})==floating.next("test",Json{{"n",1.0}}));NativeExecutionIds largeA,largeB;CHECK(largeA.next("test",Json{{"n",9007199254740993LL}})!=largeB.next("test",Json{{"n",9007199254740992LL}}));
    CHECK(savedNativeExecutionFormat(Json{{"version",1}})==1);CHECK(savedNativeExecutionFormat(Json{{"version",2}})==2);CHECK(!savedNativeExecutionFormat(Json{{"version",3}}));CHECK(!savedNativeExecutionFormat(Json{{"version","2"}}));
    Json deep=1;for(int i=0;i<66;++i)deep=Json::array({deep});NativeExecutionIds bounded;CHECK(!bounded.next("test",deep));
}
TEST_CASE(deferred_creativity_uses_real_capacity_and_old_visible_draft_tails){
    Json state={{"piece_count",3},{"maximum",4}};CHECK(creativeSlotAvailable(state));state["can_create"]=false;CHECK(!creativeSlotAvailable(state));CHECK(!creativeSlotAvailable(Json::object()));
    state["pieces"]=Json::array();for(int i=0;i<6;++i)state["pieces"].push_back({{"id","finished-"+std::to_string(i)},{"stage","finished"},{"revision",1}});
    state["continuations"]=Json::array({Json{{"id","older-draft"},{"stage","draft"},{"revision",4},{"tail","这仍是实际草稿末尾"}}});const auto visible=visibleContinuationDrafts(state);CHECK(visible.size()==1&&visible[0].at("id")=="older-draft"&&visible[0].at("revision")==4);CHECK(!visible[0].contains("tail"));
    state["pieces"].push_back(state["continuations"][0]);CHECK(visibleContinuationDrafts(state).size()==1);state["continuations"]=Json::array();state["pieces"]=Json::array({Json{{"id","done"},{"stage","finished"},{"revision",4}}});CHECK(visibleContinuationDrafts(state).empty());
}
TEST_CASE(empty_autonomous_choices_back_off_without_becoming_missing_content_errors){
    auto cadence=emptyPlanningCadence();CHECK(validPlanningCadence(cadence));cadence=planningOutcome(cadence,true,true,1000,30000);CHECK(cadence.at("quiet")==1&&cadence.at("failures")==0&&cadence.at("next_ms")==61000);
    for(int i=0;i<8;++i)cadence=planningOutcome(cadence,true,true,1000+i,30000);CHECK(cadence.at("quiet")==5);CHECK(cadence.at("next_ms").get<std::int64_t>()-cadence.at("last_ms").get<std::int64_t>()==900000);
    CHECK(restoredPlanningDue(cadence,2000,30000)==cadence.at("next_ms").get<std::int64_t>());CHECK(restoredPlanningDue(cadence,0,30000)==30000);
    cadence=planningOutcome(cadence,true,false,3000,30000);CHECK(cadence.at("quiet")==0&&cadence.at("failures")==0&&cadence.at("next_ms")==33000);
    cadence=planningOutcome(cadence,false,false,4000,30000);CHECK(cadence.at("failures")==1&&cadence.at("next_ms")==64000);
    const auto slow=planningOutcome(emptyPlanningCadence(),true,true,0,3600000);CHECK(slow.at("next_ms")==3600000);auto future=slow;future["version"]=2;CHECK(!validPlanningCadence(future));CHECK(planningOutcome(future,true,true,0,30000)==future);
}
TEST_CASE(activity_narrative_visibility_preserves_physical_results_and_original_snapshots){
    const Json source={{"id","private-source"},{"revision",1},{"kind","user_message"},{"scope","ha.archive"}};
    const Json command={{"kind","move_to"},{"x",.4},{"y",.6},{"status","completed"},{"purpose","PRIVATE_REASON"},{"intention",{{"id","PRIVATE_WISH"},{"revision",1}}},{"attempt_source_id","private-attempt"},{"narrative_sources",Json::array({source})}};
    const Json raw={{"active",command},{"entries",Json::array({command})},{"recent",Json::array({command})}};
    const auto snapshot=activityBodyContext(raw,true);CHECK(snapshot.at("next")[0].contains("narrative_sources"));
    const auto hidden=activityNarrativeProjection(snapshot,[](const Json&){return false;});
    CHECK(hidden.dump().find("PRIVATE_")==std::string::npos);CHECK(hidden.dump().find("private-attempt")==std::string::npos);CHECK(hidden.dump().find("narrative_sources")==std::string::npos);
    CHECK(hidden.at("active").at("kind")=="move_to"&&hidden.at("recent")[0].at("status")=="completed");CHECK(hidden.at("next")[0].at("x")==.4);
    const auto restored=activityNarrativeProjection(snapshot,[&](const Json& ref){return ref==source;});CHECK(restored.at("active").at("purpose")=="PRIVATE_REASON");CHECK(snapshot.at("active").contains("narrative_sources")&&raw.at("active")==command);
    auto legacy=command;legacy.erase("narrative_sources");const auto unknown=activityNarrativeProjection(Json{{"active",legacy}},[](const Json&){return true;});CHECK(!unknown.at("active").contains("purpose")&&!unknown.at("active").contains("intention"));CHECK(unknown.at("active").at("status")=="completed");
    auto supplied=command;supplied.erase("status");supplied.erase("attempt_source_id");CHECK(!validateActivities(Json{{"version",1},{"commands",Json::array({supplied})}}.dump()).ok);
}
TEST_CASE(only_real_domain_failures_with_known_targets_become_failed_experiences){
    const Json args={{"id","own-piece"},{"expected_revision",1},{"content","PRIVATE_DRAFT_CONTENT"}},target={{"id","own-piece"},{"revision",2},{"stage","draft"}};
    Json receipt={{"ok",false},{"error",{{"code","atelier_revision_conflict"},{"message","DO_NOT_ARCHIVE_UNTRUSTED_MESSAGE"}}}};
    const auto attempt=failedDomainAttempt("atelier.append",args,receipt,target);CHECK(attempt.is_object());CHECK(attempt.at("attempted").get<bool>()&&!attempt.at("succeeded").get<bool>()&&!attempt.at("completed").get<bool>());CHECK(attempt.at("target").at("revision")==2&&attempt.at("expected_revision")==1);
    CHECK(attempt.dump().find("PRIVATE_DRAFT_CONTENT")==std::string::npos&&attempt.dump().find("DO_NOT_ARCHIVE_UNTRUSTED_MESSAGE")==std::string::npos);
    const Json record={{"kind","tool_attempt"},{"tool","atelier.append"},{"result",attempt}};CHECK(!personalCompletionObservation(record));
    const auto anchor=personal::terminalAnchor(record,Json{{"id","attempt-source"}});CHECK(anchor.at("key")=="authored_piece:own-piece"&&anchor.at("weight")==.25);
    const auto success=personal::terminalAnchor(Json{{"kind","tool_observation"},{"tool","atelier.write"},{"result",{{"written",true},{"piece",target}}}},Json{{"id","work-source"}});CHECK(success.at("key")==anchor.at("key")&&success.at("weight")==1);
    const auto compact=compactObservation(record,false);CHECK(compact.at("context_role")=="previous_failed_attempt"&&compact.at("result").at("target")==attempt.at("target"));
    for(const auto* code:{"invalid_arguments","tool_denied","cancelled","provider_unavailable","model_call_budget","atelier_piece_unavailable"}){receipt["error"]["code"]=code;CHECK(failedDomainAttempt("atelier.append",args,receipt,target).is_null());}
    receipt["error"]["code"]="atelier_revision_conflict";CHECK(failedDomainAttempt("unknown.tool",args,receipt,target).is_null());CHECK(failedDomainAttempt("atelier.append",args,receipt,Json(nullptr)).is_null());
    auto equal=target;equal["revision"]=1;CHECK(failedDomainAttempt("atelier.append",args,receipt,equal).is_null());auto wrong=target;wrong["id"]="some-other-piece";CHECK(failedDomainAttempt("atelier.append",args,receipt,wrong).is_null());
    receipt["ok"]=true;CHECK(failedDomainAttempt("atelier.append",args,receipt,target).is_null());
}
TEST_CASE(subjective_appraisal_has_its_own_clock_and_does_not_move_the_body_or_jump_mood){
    RoleState state;state.restore(Json::object(),1000);const auto initial=state.snapshot();std::string issue;
    CHECK(state.appraise(.8,"experienced-event",1000,issue));auto value=state.snapshot();
    for(const auto* key:{"mood","energy","attention","openness","activity","desired_activity"})CHECK(value.at(key)==initial.at(key));
    CHECK(value.at("target_valence")==.8&&value.at("appraisal_source_id")=="experienced-event");
    CHECK(state.propose("idle",-.5,"chosen-reserve",1500,issue));const auto openness=state.snapshot().at("target_openness");
    CHECK(state.propose("resting",std::nullopt,"physical-rest",2000,issue));state.tick(2000);value=state.snapshot();
    CHECK(value.at("target_valence")==.8&&value.at("appraisal_source_id")=="experienced-event");CHECK(value.at("mood").get<double>()>.5&&value.at("mood").get<double>()<=.500751);CHECK(value.at("target_openness")==openness);
    CHECK(value.at("desired_activity")=="resting");CHECK(state.propose("idle",0,"physical-wake",3000,issue));state.contact(600999);state.tick(601001);
    CHECK(state.snapshot().at("target_valence")==0);CHECK(state.snapshot().at("appraisal_age_ms").get<std::int64_t>()>600000);
    CHECK(state.appraise(.8,"experienced-event",601002,issue));CHECK(state.snapshot().at("target_valence")==0); // same old appraisal cannot rearm itself
    CHECK(!state.appraise(2,"source",601003,issue));CHECK(!state.appraise(.2,"",601003,issue));
}
TEST_CASE(appraisal_pause_restart_and_legacy_migration_preserve_existing_values){
    RoleState state;state.restore(Json{{"version",2},{"mood",.43},{"target_mood",.42},{"target_valence",-.6},{"evidence","old-experience"},{"stimulus_age_ms",120000}},1000);
    auto before=state.snapshot();CHECK(before.at("version")==4&&before.at("mood")==.43&&before.at("target_valence")==-.6);CHECK(before.at("appraisal_source_id")==""&&before.at("appraisal_age_ms")==120000);
    state.tick(86401000,false);const auto paused=state.snapshot();CHECK(paused.at("appraisal_age_ms")==before.at("appraisal_age_ms"));CHECK(paused.at("mood")==before.at("mood"));
    RoleState restored;restored.restore(paused,172801000);const auto after=restored.snapshot();for(const auto* key:{"mood","energy","attention","target_valence","appraisal_valence","appraisal_source_id","appraisal_age_ms"})CHECK(after.at(key)==paused.at(key));
}
TEST_CASE(background_appraisal_is_optional_and_uses_the_same_bounded_tool_contract){
    Json plan={{"version",1},{"commands",Json::array()},{"appraisal",{{"source_id","actual-work"},{"valence",-.3}}}};CHECK(validateActivities(plan.dump()).ok);CHECK(ayt::serializer::validateJsonDocument(plan.at("appraisal").dump(),appraisalParameters().dump()));
    plan["appraisal"]["mood"]=1;CHECK(!validateActivities(plan.dump()).ok);plan["appraisal"].erase("mood");plan["appraisal"]["valence"]=1.1;CHECK(!validateActivities(plan.dump()).ok);
    plan["appraisal"]=nullptr;const auto omitted=validateActivities(plan.dump());CHECK(omitted.ok&&!omitted.value.contains("appraisal"));
}
TEST_CASE(final_dialogue_and_native_functions_share_optional_appraisal_without_call_phase_effects){
    const Json appraisal={{"source_id","actual-experience"},{"valence",.2}};Json turn={{"version",1},{"kind","silence"},{"text",nullptr},{"calls",Json::array()},{"appraisal",appraisal}};
    const auto checked=validateTurn(turn.dump());CHECK(checked.ok&&turnHasPrivateUpdates(checked.value));
    NativeToolBridge bridge(Json::array());const auto native=bridge.candidate({{"final","ha_turn_silence",Json{{"appraisal",appraisal}}.dump()}},0);CHECK(native.ok&&native.value.at("appraisal")==appraisal&&turnHasPrivateUpdates(native.value));
    turn["kind"]="call";turn["calls"]=Json::array({{{"id","a"},{"tool","state.observe"},{"args",Json::object()}}});CHECK(validateTurn(turn.dump()).error=="appraisal_requires_final");
    turn["kind"]="silence";turn["calls"]=Json::array();turn["appraisal"]=nullptr;const auto empty=validateTurn(turn.dump());CHECK(empty.ok&&!empty.value.contains("appraisal")&&!turnHasPrivateUpdates(empty.value));
}
TEST_CASE(appraisals_need_original_encounters_or_world_observations_rather_than_internal_snapshots){
    CHECK(appraisalExperienceRecord(Json{{"kind","user_message"}}));CHECK(appraisalExperienceRecord(Json{{"kind","tool_attempt"}}));
    CHECK(appraisalExperienceRecord(Json{{"kind","tool_observation"},{"tool","atelier.read"}}));CHECK(appraisalExperienceRecord(Json{{"kind","tool_observation"},{"tool","pocket.observe"}}));
    CHECK(!appraisalExperienceRecord(Json{{"kind","tool_intent"},{"tool","habitat.act"}}));CHECK(!appraisalExperienceRecord(Json{{"kind","personal_attempt"}}));CHECK(!appraisalExperienceRecord(Json{{"kind","inference"}}));
    CHECK(!appraisalExperienceRecord(Json{{"kind","tool_observation"},{"tool","body.observe"}}));CHECK(!appraisalExperienceRecord(Json{{"kind","tool_observation"},{"tool","state.observe"}}));CHECK(!appraisalExperienceRecord(Json{{"kind","tool_observation"},{"tool","activity.status"}}));
}
TEST_CASE(legacy_private_focus_stays_preserved_without_becoming_unverified_model_context){
    ayt::test::ScratchDirectory scratch("legacy-focus-projection");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}}};
    {Application app(cfg);CHECK(app.initialize());}
    {ayt::storage::RecordStore store;CHECK(store.open(cfg.dataRoot/"archive/records.sqlite",{openVaultCodec(cfg.dataRoot),true}));const auto rows=store.query({{"ha.life"}},{"ha.life","current"});CHECK(static_cast<bool>(rows));const auto& row=rows.records.at(0);auto value=Json::parse(row.payload);value["version"]=2;value["focus"]="UNVERIFIED_PRIVATE_FOCUS";value["evidence"]="physical-event-without-a-memory";value["target_valence"]=.3;value["mood"]=.46;for(const auto* key:{"appraisal_source_id","appraisal_valence","appraisal_age_ms"})value.erase(key);CHECK(static_cast<bool>(store.commit({{"ha.life","current",row.revision,1,value.dump(),"legacy-fixture"}},"legacy-fixture",1)));}
    {Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/message",R"({"request_id":"legacy-focus-turn","text":"你好"})"}).status,202);CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));const auto state=Json::parse(app.request({"GET","/api/state",""}).body);CHECK(state.at("continuous_state").at("focus")=="UNVERIFIED_PRIVATE_FOCUS");CHECK(state.at("continuous_state").at("mood").get<double>()>.45&&state.at("continuous_state").at("mood").get<double>()<.47);const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id",state.at("state").at("lastTraceId")}}.dump()}).body);CHECK(!trace.at("continuous_state").contains("focus"));CHECK(!trace.at("continuous_state").contains("appraisal_source_id"));CHECK(trace.at("continuous_state").at("target_valence")==.3);}
}
TEST_CASE(shared_channel_state_carries_body_values_without_inheriting_desktop_private_focus){
    ayt::test::ScratchDirectory scratch("channel-state-projection");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.profiles={{"mock","Mock",true,{}}};
    auto policy=std::make_shared<ChannelPolicy>();policy->selectedProfile="mock";policy->identity={{"channel","qq"},{"sender_user_id","20001"}};policy->stage={{"id",Json::parse(readFile(cfg.characterRoot/"stages.json")).at("initial")},{"changed_at",0}};
    policy->roleState={{"energy",.42},{"activity","resting"},{"focus","DESKTOP_ONLY_FOCUS"},{"appraisal_source_id","desktop-only-source"},{"appraisal_valence",.3},{"appraisal_age_ms",1000}};cfg.channel=policy;
    Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/message",R"({"request_id":"channel-state-turn","text":"你好"})"}).status,202);CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));
    const auto state=Json::parse(app.request({"GET","/api/state",""}).body);const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id",state.at("state").at("lastTraceId")}}.dump()}).body);const auto observed=trace.at("continuous_state");CHECK(observed.at("energy")==.42&&observed.at("activity")=="resting");CHECK(!observed.contains("focus")&&!observed.contains("appraisal_source_id"));CHECK(policy->roleState.at("focus")=="DESKTOP_ONLY_FOCUS");
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("HibernationAgent",argc,argv);}
