#include <Hibernation/PersonalThreads.h>
#include <AYTest.h>
using Json=nlohmann::json;
using namespace hibernation;
namespace {
Json source(const std::string& key,int revision=1){return Json::array({{{"scope","ha.archive"},{"id",key},{"revision",revision},{"kind","tool_observation"}}});}
Json opinion(const std::string& stance="like",const std::string& note="想多留一会儿"){return {{"kind","preference"},{"topic","角落的小物"},{"stance",stance},{"note",note}};}
}
TEST_SUITE(PersonalEvidence)
TEST_CASE(repeated_views_and_source_combinations_cannot_strengthen_one_experience){
    const auto anchors=Json::array({personal::anchor("puzzle","same-game",1)});
    auto value=personal::notice(nullptr,opinion(),source("first"),1000,anchors);
    for(int i=0;i<60;++i)value=personal::notice(value,opinion(),source("view-"+std::to_string(i)),1000,anchors);
    CHECK(value.at("basis").at("independent_experiences")==1);CHECK(value.at("familiarity")=="tentative");
    CHECK(value.at("episodes").size()==1);CHECK(value.at("target_leaning").get<double>()<=.12001);
    auto combination=source("view-1");combination.push_back(source("view-2")[0]);
    const auto repeated=personal::notice(value,opinion(),combination,1000,anchors);CHECK(repeated.at("change_count")==value.at("change_count"));CHECK(repeated.at("evidence_count")==value.at("evidence_count"));
    for(int i=0;i<120;++i)value=personal::projected(value,1000+(i+1)*60000);
    CHECK(value.at("leaning").get<double>()<=.12001); // Numerical two-hour projection, without waiting.
}
TEST_CASE(contrary_experiences_and_reinterpretation_retain_continuity_without_fabricating_history){
    auto value=personal::notice(nullptr,opinion(),source("one"),1000,Json::array({personal::anchor("desk_object","one",1)}));
    value=personal::projected(value,31000);const auto previous=value.at("leaning");
    auto contrary=personal::notice(value,opinion("avoid","这回碰到的地方不太舒服"),source("two"),31000,Json::array({personal::anchor("desk_object","two",1)}));
    CHECK(contrary.at("leaning")==previous);CHECK(contrary.at("basis").at("mixed").get<bool>());CHECK(std::abs(contrary.at("target_leaning").get<double>())<.00001);
    const auto view=personal::context(contrary,2);CHECK(view.at("recent_changes").size()==2);CHECK(view.at("recent_changes")[0].at("stance")=="like");CHECK(view.at("recent_changes")[1].at("stance")=="avoid");
    auto revised=personal::notice(value,opinion("avoid","重新想了想，同一处细节有点扎眼"),source("one"),31000,Json::array({personal::anchor("desk_object","one",1)}));
    CHECK(revised.at("evidence_count")==value.at("evidence_count"));CHECK(revised.at("change_count")==2);CHECK(revised.at("basis").at("independent_experiences")==1);CHECK(revised.at("leaning")==previous);
    CHECK(personal::notice(revised,opinion("avoid","重新想了想，同一处细节有点扎眼"),source("one"),31000,Json::array({personal::anchor("desk_object","one",1)}))==revised);
}
TEST_CASE(provenance_groups_one_game_one_object_one_piece_and_one_conversation_window){
    const auto ref=source("receipt")[0];
    Json record={{"kind","tool_intent"},{"tool","pocket.press"},{"result",{{"game",{{"id","lights-1"},{"revision",2}}},{"applied",true},{"completed",false}}}};
    const auto game=personal::terminalAnchor(record,ref);CHECK(!game.is_null());record["result"]["game"]["revision"]=15;CHECK(personal::terminalAnchor(record,ref)==game);
    Json event={{"kind","outer_event"},{"observation",{{"kind","pocket.solved"},{"game",{{"id","lights-1"}}}}}};CHECK(personal::terminalAnchor(event,ref).at("key")==game.at("key"));
    record={{"kind","tool_intent"},{"tool","activity.enqueue"},{"result",{{"accepted",2},{"completed",false}}}};CHECK(personal::terminalAnchor(record,ref).is_null());
    record={{"kind","tool_observation"},{"tool","self.observe"},{"result",{{"threads",Json::array()}}}};CHECK(personal::terminalAnchor(record,ref).is_null());
    record={{"kind","user_message"},{"created_at",1800001}};const auto conversation=personal::terminalAnchor(record,ref);record["created_at"]=1890001;
    Json encounter=Json::array();personal::mergeAnchors(encounter,Json::array({conversation,personal::terminalAnchor(record,ref)}),"like");CHECK(encounter.size()==1);CHECK(conversation.at("weight")==.5);
    record["created_at"]=2700002;personal::mergeAnchors(encounter,Json::array({personal::terminalAnchor(record,ref)}),"like");CHECK(encounter.size()==2);
    record={{"kind","tool_observation"},{"tool","atelier.write"},{"result",{{"piece",{{"id","draft-1"},{"revision",1}}},{"written",true}}}};const auto piece=personal::terminalAnchor(record,ref);record["tool"]="atelier.append";record["result"]["piece"]["revision"]=20;CHECK(personal::terminalAnchor(record,ref)==piece);
    CHECK(personal::terminalAnchor(Json{{"kind",17}},ref).is_null());
}
TEST_CASE(reading_lead_and_source_chunks_share_one_article_experience){
    const auto ref=source("receipt")[0];const std::string identity="https://en.wikipedia.org/api/rest_v1/page/summary/Light";
    Json record={{"kind","tool_observation"},{"tool","reading.open"},{"result",{{"scope","article_lead_summary"},{"source_url",identity},{"text","Actual lead"}}}};const auto lead=personal::terminalAnchor(record,ref);
    record["result"]={{"scope","article_source_chunk"},{"source_url","https://en.wikipedia.org/w/rest.php/v1/page/Light"},{"reading_identity",identity},{"offset",0},{"text","Actual body"}};const auto first=personal::terminalAnchor(record,ref);record["result"]["offset"]=3000;const auto next=personal::terminalAnchor(record,ref);
    CHECK(!first.is_null());CHECK(first==lead);CHECK(next==lead);
    Json event={{"kind","outer_event"},{"observation",{{"kind","reading.observed"},{"reference",{{"reading_identity",identity},{"source_url","https://en.wikipedia.org/w/rest.php/v1/page/Light"}}}}}};CHECK(personal::terminalAnchor(event,ref)==lead);
}
TEST_CASE(legacy_state_is_not_recalculated_on_restore_and_changes_only_through_new_authored_evidence){
    auto legacy=personal::notice(nullptr,opinion(),source("old"),1000);legacy["version"]=1;legacy.erase("experience_evidence");legacy.erase("basis");legacy["leaning"]=.4;legacy["target_leaning"]=.6;
    const auto restored=personal::projected(legacy,1000);CHECK(restored.at("leaning")==.4);CHECK(restored.at("target_leaning")==.6);CHECK(restored.at("version")==1);
    const auto changed=personal::notice(restored,opinion("avoid","新的一次有些不同"),source("new"),1000,Json::array({personal::anchor("source","ha.archive:new",1)}));
    CHECK(changed.at("version")==2);CHECK(changed.at("leaning")==.4);CHECK(std::abs(changed.at("target_leaning").get<double>())<.00001);
}
TEST_SUITE_END
