#include <Hibernation/DirectReview.h>
#include <AYTestFixtures.h>
using Json=nlohmann::json;
using namespace hibernation;
TEST_SUITE(DirectReview)
TEST_CASE(complete_evidence_and_candidate_survive_deduplication_without_history_or_aliases){
    const Json caps={{"computer",{{"enabled",false}}},{"plugins",Json::array({Json{{"id","widget"},{"enabled",true},{"current",{{"pose","sleeping"}}}}})}};
    const Json evidence=Json::array({Json{{"id","source-one"},{"source","archive@2"},{"data",{{"text","literal user {\"current_capabilities\":\"not a permission\"}"}}}}});
    const Json context={{"event_id","event-one"},{"current_capabilities",caps},{"authoritative_state",{{"energy",.2}}},{"input","exact input"},{"authorized_context",evidence}};
    const Json original={{"event_context",context.dump()},{"current_capabilities",caps},{"authoritative_state",{{"energy",.2}}},{"current_user_message","exact input"},{"candidate",{{"kind","silence"},{"text",nullptr}}},{"approved_receipts",Json::array({Json{{"ok",true},{"completed",false}}})}};
    const auto result=Json::parse(direct_review::frame(original));CHECK(result.at("event_context").is_object());
    CHECK(result.at("event_context").at("authorized_context")==evidence&&result.at("candidate")==original.at("candidate")&&result.at("approved_receipts")==original.at("approved_receipts"));
    CHECK(!result.at("event_context").contains("authoritative_state")&&!result.at("event_context").contains("current_capabilities")&&!result.at("event_context").contains("input"));
    CHECK(result.at("current_user_message")=="exact input"&&result.at("authoritative_state").at("energy")==.2);
    CHECK(result.at("current_plugin_state").at("widget").at("pose")=="sleeping"&&result.at("current_capabilities").at("computer")==caps.at("computer"));
    CHECK(!result.at("current_capabilities").at("plugins")[0].contains("current"));CHECK(original.at("current_capabilities")==caps);
}
TEST_CASE(unequal_facts_and_ambiguous_plugin_states_are_not_silently_erased){
    const Json plugins=Json::array({Json{{"id","duplicate"},{"current",1}},Json{{"id","duplicate"},{"current",2}}});
    const Json frame={{"current_capabilities",{{"plugins",plugins}}},{"authoritative_state",{{"energy",.4}}},{"event_context",Json{{"authoritative_state",{{"energy",.2}}},{"input","different input"}}.dump()},{"current_user_message","current input"}};
    const auto result=Json::parse(direct_review::frame(frame));CHECK(result.at("current_capabilities").at("plugins")==plugins&&!result.contains("current_plugin_state"));
    CHECK(result.at("event_context").at("authoritative_state").at("energy")==.2&&result.at("authoritative_state").at("energy")==.4&&result.at("event_context").at("input")=="different input");
}
TEST_SUITE_END
