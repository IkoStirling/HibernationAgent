#include <Hibernation/ContextEvents.h>
#include <Hibernation/SessionProjection.h>
#include <AYTestFixtures.h>
using namespace hibernation;
using Json=nlohmann::json;
TEST_SUITE(ContextEvents)
TEST_CASE(updates_append_after_native_receipts_and_preserve_old_input){
    const Json initial={{"event_id","turn-a"},{"conversation_focus",Json::object()},{"expression_preference",{{"activePreset","natural"}}}};
    context_events::Turn events(initial);std::vector<session::Message> wire={{"system","fixed"},{"user",initial.dump()},{"assistant",""},{"tool","{\"ok\":true}"}};wire[2].toolCalls.push_back({"call","observe","{}"});wire[3].toolCallId="call";const auto before=session::encode(wire);
    CHECK(events.update("focus.changed",{{"conversation_focus",{{"subject","character"}}}}));CHECK(!events.update("focus.changed",{{"conversation_focus",{{"subject","character"}}}}));
    CHECK(events.update("expression.changed",{{"expression_preference",nullptr}}));events.flush(wire);
    CHECK(session::encode(std::vector<session::Message>(wire.begin(),wire.begin()+4))==before);CHECK(wire.size()==6&&events.pending()==0&&events.sequence()==2);
    const auto first=Json::parse(wire[4].text).at("runtime_event");CHECK(first.at("source_event_id")=="turn-a"&&first.at("sequence")==1);CHECK(events.current().at("expression_preference").is_null());
}
TEST_CASE(queued_final_review_updates_follow_the_reply_without_retroactive_edits){
    context_events::Turn events({{"event_id","turn-b"},{"conversation_focus",Json::object()}});CHECK(events.update("focus.changed",{{"conversation_focus",{{"questions","avoid_repeated"}}}}));
    const auto postlude=events.take();CHECK(postlude.size()==1&&events.pending()==0);CHECK(events.current().at("conversation_focus").at("questions")=="avoid_repeated");
    events.update("phase.final",{{"turn_constraints",{{"final_only",true}}}});events.rebase(events.current());CHECK(events.pending()==0&&events.current().at("turn_constraints").at("final_only")==true);
}
TEST_CASE(source_grants_and_input_are_outside_the_update_channel){
    context_events::Turn events({{"event_id","turn-c"}});
    for(const auto* field:{"authorized_context","input","event_id","permissions","system"}){bool rejected=false;try{events.update("bad",{{field,"changed"}});}catch(...){rejected=true;}CHECK(rejected);}
    CHECK(events.sequence()==0&&events.current()==Json({{"event_id","turn-c"}}));
}
TEST_SUITE_END
