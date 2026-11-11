#include <Hibernation/Application.h>
#include <Hibernation/SpeechStyle.h>
#include <Hibernation/PluginRuntime.h>
#include <AYSerializer/JsonSchema.h>
#include <AYTestFixtures.h>
#include <algorithm>
#include <httplib.h>
#include <thread>
#include <mutex>
#include <atomic>
using namespace hibernation;
namespace {
Json styleFixture(){return {{"enabled",true},{"activePreset","alpha"},{"allowModelSwitch",false},{"presets",Json::array({{{"id","alpha"},{"name","第一种表达"},{"instructions","ACTIVE_ALPHA_ONLY: shorter natural sentences."}},{{"id","beta"},{"name","第二种表达"},{"instructions","INACTIVE_BETA_ONLY: restrained hesitation."}}})}};}
bool invalidStyle(const Json& value){try{speechStyle::normalize(value);return false;}catch(const std::exception&){return true;}}
bool hasStyleTool(const Json& tools,const char* name){return std::any_of(tools.begin(),tools.end(),[&](const auto& tool){return tool.at("name")==name;});}
}
TEST_SUITE(SpeechStyle)
TEST_CASE(default_configuration_is_disabled_and_round_trips){
    const auto cfg=speechStyle::normalize(speechStyle::defaults());CHECK(cfg.at("enabled")==false);CHECK(cfg.at("activePreset")=="natural");CHECK(cfg.at("allowModelSwitch")==false);CHECK(speechStyle::normalize(cfg)==cfg);CHECK(speechStyle::preference(cfg).at("instructions")=="");
}
TEST_CASE(preference_exposes_only_the_selected_instructions){
    auto cfg=speechStyle::normalize(styleFixture());const auto first=speechStyle::preference(cfg).dump();CHECK(first.find("ACTIVE_ALPHA_ONLY")!=std::string::npos);CHECK(first.find("INACTIVE_BETA_ONLY")==std::string::npos);
    cfg["activePreset"]="beta";const auto second=speechStyle::preference(cfg).dump();CHECK(second.find("INACTIVE_BETA_ONLY")!=std::string::npos);CHECK(second.find("ACTIVE_ALPHA_ONLY")==std::string::npos);
    cfg["enabled"]=false;CHECK(speechStyle::preference(cfg).at("instructions")=="");
}
TEST_CASE(snapshot_projection_ignores_catalog_and_blanks_disabled_instructions){
    const auto expected=speechStyle::preference(styleFixture());Json snapshot={{"preference",expected},{"catalog",styleFixture().at("presets")},{"configurationSignature","private-config-signature"},{"selectedPreset","alpha"}};CHECK(speechStyle::preference(snapshot)==expected);CHECK(speechStyle::preference(snapshot).dump().find("INACTIVE_BETA_ONLY")==std::string::npos);CHECK(speechStyle::preference(snapshot).dump().find("private-config-signature")==std::string::npos);
    snapshot["preference"]["enabled"]=false;CHECK(speechStyle::preference(snapshot).at("instructions")=="");
}
TEST_CASE(malformed_flags_unknown_selection_and_duplicate_ids_are_rejected){
    auto cfg=styleFixture();cfg["enabled"]="true";CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["allowModelSwitch"]=1;CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["activePreset"]="missing";CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["presets"][1]["id"]="alpha";CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["presets"][0]["instructions"]=Json::array();CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["presets"][0]["id"]="../unsafe";CHECK(invalidStyle(cfg));
}
TEST_CASE(configuration_limits_bound_utf8_names_instructions_and_total_catalog){
    auto cfg=styleFixture();cfg["presets"][0]["name"]=std::string(160,'a');cfg["presets"][0]["instructions"]=std::string(2048,'a');CHECK(!invalidStyle(cfg));cfg["presets"][0]["name"]=std::string(161,'a');CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["presets"][0]["instructions"]=std::string(2049,'a');CHECK(invalidStyle(cfg));
    cfg=styleFixture();cfg["presets"][0]["instructions"]="line one\nline two\tend";CHECK(!invalidStyle(cfg));cfg["presets"][0]["instructions"]=std::string("a\0b",3);CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["presets"][0]["name"]="   ";CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["presets"][0]["name"]=std::string("\xc3\x28",2);CHECK(invalidStyle(cfg));
    cfg=styleFixture();cfg["presets"]=Json::array();CHECK(invalidStyle(cfg));cfg=styleFixture();for(int i=2;i<17;++i)cfg["presets"].push_back({{"id","extra"+std::to_string(i)},{"name","extra"},{"instructions",""}});CHECK(invalidStyle(cfg));
    cfg=speechStyle::defaults();cfg["presets"][0]["instructions"]="must stay neutral";CHECK(invalidStyle(cfg));cfg=styleFixture();cfg["extra"]="unknown";CHECK(invalidStyle(cfg));
    cfg=styleFixture();cfg["presets"]=Json::array();for(int i=0;i<16;++i)cfg["presets"].push_back({{"id","item"+std::to_string(i)},{"name","item"},{"instructions",std::string(1024,'x')}});cfg["activePreset"]="item0";CHECK(invalidStyle(cfg));
}
TEST_CASE(qq_channel_dto_carries_the_active_preference_to_actor_and_reviewer){
    httplib::Server server;std::mutex mutex;Json captured=Json::object();std::atomic<int> calls=0;
    server.Post("/v1/chat/completions",[&](const auto& request,auto& response){const auto body=Json::parse(request.body);const bool review=body.at("model")=="review";{std::lock_guard lock(mutex);captured[review?"review":"actor"]=body;}++calls;const Json content=review?Json{{"decision","approve"},{"reason","ok"},{"guidance",""}}:Json{{"version",1},{"kind","reply"},{"text","窗边很安静。"},{"calls",Json::array()}};response.set_content(Json{{"choices",Json::array({{{"message",{{"role","assistant"},{"content",content.dump()}}},{"finish_reason","stop"}}})}}.dump(),"application/json");});
    const auto port=server.bind_to_any_port("127.0.0.1");std::thread listener([&]{server.listen_after_bind();});server.wait_until_ready();struct Guard{httplib::Server& server;std::thread& thread;~Guard(){server.stop();thread.join();}}guard{server,listener};
    ayt::test::ScratchDirectory scratch("speech-style-channel");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";
    Profile actor;actor.id="actor";actor.label="Actor";actor.model.protocol=ayt::inference::Protocol::ChatCompletions;actor.model.origin="http://127.0.0.1:"+std::to_string(port);actor.model.basePath="/v1";actor.model.model="actor";actor.model.supportsJsonObject=true;actor.model.supportsStreaming=false;actor.model.useSystemProxy=false;actor.model.timeout=std::chrono::seconds(3);auto review=actor;review.id="review";review.label="Review";review.model.model="review";cfg.profiles={{"mock","Mock",true,{}},actor,review};cfg.reviewEnabled=true;cfg.reviewProfileId="review";
    auto policy=std::make_shared<ChannelPolicy>();policy->selectedProfile="actor";policy->identity={{"channel","qq"},{"sender_user_id","20001"}};policy->stage={{"id",Json::parse(readFile(cfg.characterRoot/"stages.json")).at("initial")},{"changed_at",0}};policy->reserveModelCalls=[](int amount){return amount==1;};policy->expressionPreference=speechStyle::preference(styleFixture());cfg.channel=policy;
    Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/message",R"({"request_id":"speech-style-channel-turn","text":"你好"})"}).status,202);CHECK(ayt::test::waitUntil([&]{app.tick();return !Json::parse(app.request({"GET","/api/chat",""}).body).at("busy").get<bool>();}));CHECK(!Json::parse(app.request({"GET","/api/chat",""}).body).at("call_failed").get<bool>());CHECK(calls.load()==2);
    std::lock_guard lock(mutex);for(const auto* role:{"actor","review"}){CHECK(captured.contains(role));const auto wire=captured.at(role).dump();CHECK(wire.find("expression_preference")!=std::string::npos);CHECK(wire.find("ACTIVE_ALPHA_ONLY")!=std::string::npos);CHECK(wire.find("INACTIVE_BETA_ONLY")==std::string::npos);}
}
#ifdef HA_TEST_PLUGIN_ROOT
TEST_CASE(native_plugin_exposes_opted_in_switching_and_rolls_back_invalid_updates){
    PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{});std::string issue;Json cfg={{"speech-style",styleFixture()}};CHECK(runtime.configure(cfg,issue));CHECK(runtime.enabled("speech-style"));auto tools=runtime.catalog();CHECK(hasStyleTool(tools,"speech_style.observe"));CHECK(!hasStyleTool(tools,"speech_style.select"));
    auto observation=runtime.invoke("speech_style.observe",Json::object(),{});CHECK(observation.value("ok",false));CHECK(observation.dump().find("ACTIVE_ALPHA_ONLY")!=std::string::npos);CHECK(observation.dump().find("INACTIVE_BETA_ONLY")==std::string::npos);
    cfg["speech-style"]["allowModelSwitch"]=true;CHECK(runtime.configure(cfg,issue));tools=runtime.catalog();CHECK(hasStyleTool(tools,"speech_style.select"));
    const auto select=std::find_if(tools.begin(),tools.end(),[](const auto& tool){return tool.at("name")=="speech_style.select";});CHECK(select!=tools.end());CHECK(ayt::serializer::validateJsonDocument(R"({"presetId":"beta"})",select->at("parameters").dump()));CHECK(!ayt::serializer::validateJsonDocument(R"({"presetId":"beta","instructions":"override"})",select->at("parameters").dump()));
    CHECK(runtime.invoke("speech_style.select",{{"presetId","beta"}},{}).value("ok",false));const auto selected=speechStyle::preference(runtime.liveSnapshots().at("speech-style"));CHECK(selected.dump().find("INACTIVE_BETA_ONLY")!=std::string::npos);CHECK(selected.dump().find("ACTIVE_ALPHA_ONLY")==std::string::npos);
    runtime.event({{"kind","runtime.paused"},{"paused",true}});CHECK(!runtime.invoke("speech_style.select",{{"presetId","alpha"}},{}).value("ok",true));CHECK(speechStyle::preference(runtime.liveSnapshots().at("speech-style"))==selected);runtime.event({{"kind","runtime.paused"},{"paused",false}});
    CHECK(!runtime.invoke("speech_style.select",{{"presetId","alpha"}},[]{return true;}).value("ok",true));CHECK(speechStyle::preference(runtime.liveSnapshots().at("speech-style"))==selected);
    const auto unchanged=runtime.invoke("speech_style.select",{{"presetId","beta"}},{});CHECK(unchanged.value("ok",false)&&unchanged.at("result").at("changed")==false);
    CHECK(!runtime.invoke("speech_style.select",{{"presetId","missing"}},{}).value("ok",true));CHECK(speechStyle::preference(runtime.liveSnapshots().at("speech-style"))==selected);
    auto bad=cfg;bad["speech-style"]["activePreset"]="missing";CHECK(!runtime.configure(bad,issue));CHECK(runtime.enabled("speech-style"));CHECK(speechStyle::preference(runtime.liveSnapshots().at("speech-style"))==selected);
    cfg["speech-style"]["enabled"]=false;CHECK(runtime.configure(cfg,issue));CHECK(!hasStyleTool(runtime.catalog(),"speech_style.observe"));CHECK(!hasStyleTool(runtime.catalog(),"speech_style.select"));runtime.stop();
}
TEST_CASE(restart_selection_is_restored_only_for_the_same_owner_configuration){
    Json cfg={{"speech-style",styleFixture()}};cfg["speech-style"]["allowModelSwitch"]=true;std::string issue;Json saved;
    {PluginRuntime first(HA_TEST_PLUGIN_ROOT,{});CHECK(first.configure(cfg,issue));CHECK(first.invoke("speech_style.select",{{"presetId","beta"}},{}).value("ok",false));saved=first.snapshots();}
    {PluginRuntime same(HA_TEST_PLUGIN_ROOT,{});CHECK(same.configure(cfg,issue));same.restore(saved);CHECK(speechStyle::preference(same.liveSnapshots().at("speech-style")).at("activePreset")=="beta");}
    cfg["speech-style"]["presets"][0]["name"]="New owner configuration";
    {PluginRuntime changed(HA_TEST_PLUGIN_ROOT,{});CHECK(changed.configure(cfg,issue));changed.restore(saved);CHECK(speechStyle::preference(changed.liveSnapshots().at("speech-style")).at("activePreset")=="alpha");}
}
#endif
TEST_SUITE_END
