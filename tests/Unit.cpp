#include <Hibernation/Application.h>
#include <Hibernation/TurnProtocol.h>
#include <Hibernation/RoleState.h>
#include <Hibernation/Vault.h>
#include <Hibernation/PluginRuntime.h>
#include <Hibernation/Review.h>
#include <Hibernation/TextPlayback.h>
#include <Hibernation/MemoryContext.h>
#include <Hibernation/ReactionCache.h>
#include <Hibernation/WidgetAppearance.h>
#include <Hibernation/SmallWorld.h>
#include <AYTestFixtures.h>
#include <fstream>
using namespace hibernation;
namespace {
Json valid(){return {{"visible_reply","允许没有固定结论。"},{"actions",Json::array()},{"memories",Json::array()},{"state_update",Json::object()},{"stage_proposal",nullptr}};}
ProposalContext context(){ProposalContext c;c.eventKind="user_message";c.eventId="e1";c.stage="initial";c.nextStage="exploring";c.sources={{"e1",{"e1",1,"user_message"}},{"e2",{"e2",1,"story_end"}}};return c;}
}
TEST_SUITE(RolePolicy)
TEST_CASE(behavior_changes_gradually_and_does_not_override_autonomy){
    RoleState state;state.restore(Json::object(),1000);const auto initial=state.snapshot();std::string issue;
    for(int i=0;i<80;++i)state.contact(1000);CHECK(state.snapshot().at("social_load")==initial.at("social_load"));CHECK(state.snapshot().at("willingness")==initial.at("willingness"));
    CHECK(state.propose("withdrawing",-1,"experienced-boundary",1000,issue,-1,"暂时留在自己的问题上"));CHECK(state.snapshot().at("mood")==initial.at("mood"));state.contact(1000);CHECK(state.snapshot().at("desired_activity")=="withdrawing");
    auto previous=initial;for(int i=1;i<=2400;++i){state.tick(1000+i*250);const auto now=state.snapshot();for(const auto* key:{"energy","mood","openness","attention","willingness","social_load"}){const double x=now.at(key),y=previous.at(key);CHECK(x>=0&&x<=1);CHECK(std::abs(x-y)<=0.00626);}previous=now;}
    const auto after=state.snapshot();CHECK(after.at("conversation_mode")=="quiet");CHECK(after.at("behavior").at("preferred_response")=="silence_or_brief");CHECK(after.at("focus")=="暂时留在自己的问题上");
    state.tick(1000000,false);for(const auto* key:{"energy","mood","openness","attention","willingness","social_load"})CHECK(state.snapshot().at(key)==after.at(key));
    RoleState restored;restored.restore(after,86401000);for(const auto* key:{"energy","mood","openness","attention","willingness","social_load","focus","conversation_mode"})CHECK(restored.snapshot().at(key)==after.at(key));
    CHECK(!state.propose("resting",0,"source",1000001,issue,2));CHECK(characterConduct().find("不要为了证明独立")!=std::string::npos);CHECK(reviewerConduct().find("允许自然的短答")!=std::string::npos);
}
TEST_CASE(legacy_state_migrates_without_jumping_or_resetting_activity_dwell){
    RoleState state;state.restore({{"version",1},{"energy",0.31},{"mood",0.4},{"updated_ms",10000},{"entered_ms",5000},{"activity","resting"},{"desired_activity","reflecting"}},86410000);
    CHECK(state.snapshot().at("energy")==0.31);CHECK(state.snapshot().at("mood")==0.4);CHECK(state.snapshot().at("version")==2);state.tick(86410001);CHECK(state.snapshot().at("activity")=="reflecting");
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
    {Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/model",R"({"id":"other"})"}).status,200);send(app,"day-two","昨天那个钟塔问题呢？");auto state=Json::parse(app.request({"GET","/api/state",""}).body);CHECK(state.at("model").at("id")=="other");CHECK(state.at("stage").at("id")=="initial");const auto traceId=state.at("state").at("lastTraceId");const auto trace=Json::parse(app.request({"POST","/api/trace",Json{{"id",traceId}}.dump()}).body);CHECK(trace.at("time_context").at("offline_ms").get<std::int64_t>()>=86400000);CHECK(!trace.at("time_context").at("offline_experiences_inferred").get<bool>());CHECK(trace.at("time_context").at("elapsed_since_previous_ms").get<std::int64_t>()>=86400000);CHECK(trace.at("continuous_state").at("energy").get<double>()<0.171);CHECK(trace.at("persona_version")==4);CHECK(Json::parse(app.request({"GET","/api/memories",""}).body).at("memories").size()==2);}
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
    auto settings=Json::parse(readFile(std::filesystem::path(HA_PROJECT_ROOT)/"config/example.json"));settings["dataRoot"]=(scratch.path()/"data").generic_string();settings["characterRoot"]=(std::filesystem::path(HA_PROJECT_ROOT)/"character").generic_string();settings["uiRoot"]=(std::filesystem::path(HA_PROJECT_ROOT)/"ui").generic_string();settings["pluginRoot"]=pluginRoot.generic_string();const auto file=configDir/"config.json";std::ofstream(file)<<settings.dump();Application app(loadConfig(file));CHECK(app.initialize());
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
    CHECK(static_cast<bool>(ayt::serializer::validateJsonDocument(turn.dump(),schema)));auto args=normalizeToolArguments(turn.at("calls")[0].at("args"),parameters);CHECK(!args.contains("limit"));CHECK(args.at("query")=="x");
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
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("HibernationAgent",argc,argv);}
