#include <Hibernation/Application.h>
#include <AYTestFixtures.h>
#include <fstream>
using namespace hibernation;
namespace {
Json valid(){return {{"visible_reply","允许没有固定结论。"},{"actions",Json::array()},{"memories",Json::array()},{"state_update",Json::object()},{"stage_proposal",nullptr}};}
ProposalContext context(){ProposalContext c;c.eventKind="user_message";c.eventId="e1";c.stage="initial";c.nextStage="exploring";c.sources={{"e1",{"e1",1,"user_message"}},{"e2",{"e2",1,"story_end"}}};return c;}
}
TEST_SUITE(RolePolicy)
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
TEST_CASE(solitude_without_authorized_fragments_skips_safely){
    ayt::test::ScratchDirectory scratch("hibernation-empty-context");Config cfg;cfg.dataRoot=scratch.path();cfg.characterRoot=std::filesystem::path(HA_PROJECT_ROOT)/"character";cfg.recentMessageCount=0;cfg.maxMemoriesInContext=0;cfg.profiles={{"mock","Mock",true,{}}};
    Application app(cfg);CHECK(app.initialize());CHECK_INT_EQ(app.request({"POST","/api/message",Json{{"request_id","empty-context"},{"text","一个暂存的问题"}}.dump()}).status,202);
    auto idle=[&]{app.tick();return !Json::parse(app.request({"GET","/api/state",""}).body).at("busy").get<bool>();};CHECK(ayt::test::waitUntil(idle));
    CHECK_INT_EQ(app.request({"POST","/api/solitude","{}"}).status,202);CHECK(ayt::test::waitUntil(idle));
    const auto state=Json::parse(app.request({"GET","/api/state",""}).body);for(const auto& j:state.at("jobs"))if(j.at("id")=="solitude.once")CHECK(Json::parse(j.at("result").get<std::string>()).at("status")=="no_update");
    CHECK_INT_EQ(Json::parse(app.request({"GET","/api/memories",""}).body).at("memories").size(),1);
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("HibernationAgent",argc,argv);}
