#include <Hibernation/DirectReview.h>
#include <Hibernation/LayeredReview.h>
#include <Hibernation/DeepSeekPricing.h>
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
TEST_CASE(layered_routing_is_explicit_and_mixed_or_unknown_operations_still_need_review){
    auto batch=[](const Json& calls){return Json{{"kind","call"},{"calls",calls}};};
    CHECK(layered_review::programBatch(batch(Json::array({Json{{"tool","workspace.write"}},Json{{"tool","widget.move"}}}))));
    for(const auto* name:{"memory.remember","state.propose","stage.propose","computer.launch","napcat.send_text","plugin.future_read"})
        CHECK(!layered_review::programBatch(batch(Json::array({Json{{"tool","workspace.read"}},Json{{"tool",name}}}))));
    Json queue={{"tool","activity.enqueue"},{"args",{{"commands",Json::array({Json{{"kind","move_to"}},Json{{"kind","rest"}}})}}}};
    CHECK(layered_review::localTool(queue));queue["args"]["commands"].push_back({{"kind","future_network_action"}});CHECK(!layered_review::localTool(queue));
    CHECK(!layered_review::programBatch({{"kind","reply"},{"calls",Json::array()}}));
    const Json declaration={{"name","memory.remember"},{"parameters",{{"source_ids","required"}}}};
    const auto selected=layered_review::proposedTools(Json::array({declaration,Json{{"name","workspace.read"}}}),batch(Json::array({Json{{"tool","memory.remember"}}})));
    CHECK(selected==Json::array({declaration}));
    const auto policy=ContextControl::read({{"reviewPolicy","layered"},{"reviewLayout","events"},{"reviewReasoningEffort","none"},{"reviewInitialTokens",1024}});
    CHECK(ContextControl::read(policy.describe()).describe()==policy.describe());
    CHECK(ContextControl::read({{"preset","full"},{"reviewPolicy","layered"}}).reviewPolicy=="classic");
    for(const auto& invalid:std::vector<Json>{{{"reviewPolicy","all_allowed"}},{{"reviewReasoningEffort","invalid"}},{{"reviewInitialTokens",0}}}){bool denied=false;try{ContextControl::read(invalid);}catch(...){denied=true;}CHECK(denied);}
}
TEST_CASE(review_events_append_exact_prefix_and_revoke_old_authority_without_rewriting_it){
    layered_review::Events events;Json frame={{"target","tools"},{"event_context",Json{{"authorized_context",Json::array({Json{{"id","old-source"}}})}}.dump()},{"authoritative_state",{{"energy",.8}}},{"candidate",{{"calls",Json::array({"first"})}}}};
    ayt::inference::Request first;first.messages={{"system","fixed policy"},{"user",frame.dump()}};events.append(first);auto prefix=session::encode(first.messages);
    frame["event_context"]=Json{{"authorized_context",Json::array()}}.dump();frame["authoritative_state"]=nullptr;frame["candidate"]["calls"]=Json::array({"second"});
    ayt::inference::Request next;next.messages={{"system","fixed policy"},{"user",frame.dump()}};events.append(next);
    CHECK(session::encode(std::vector<ayt::inference::Message>(next.messages.begin(),next.messages.begin()+first.messages.size()))==prefix);
    auto authority=Json::parse(next.messages[1].text).at("review_baseline").at("authority");
    authority=authority.patch(Json::parse(next.messages[next.messages.size()-2].text).at("review_event").at("patch"));
    CHECK(authority.at("event_context").at("authorized_context").empty()&&authority.at("authoritative_state").is_null());
    CHECK(!authority.contains("candidate")&&Json::parse(next.messages.back().text).at("review_candidate").at("candidate")==frame.at("candidate"));
    const auto sent=session::encode(next.messages);events.retry(next,true);
    CHECK(session::encode(std::vector<ayt::inference::Message>(next.messages.begin(),next.messages.end()-1))==sent);
    CHECK(Json::parse(next.messages.back().text).at("review_retry").at("reason")=="output_incomplete");
    const auto retried=session::encode(next.messages);ayt::inference::Request third;third.messages={{"system","fixed policy"},{"user",frame.dump()}};events.append(third);
    CHECK(session::encode(std::vector<ayt::inference::Message>(third.messages.begin(),third.messages.begin()+next.messages.size()))==retried);
}
TEST_CASE(review_event_baseline_preserves_the_other_cache_layout_and_exact_deduplication){
    layered_review::Events events;
    const Json caps={{"plugins",Json::array({Json{{"id","widget"},{"enabled",true},{"current",{{"pose","idle"}}}}})}};
    const Json frame={{"conversation_delivery",{{"channel","desktop"}}},{"current_capabilities",caps},{"event_context",Json{{"current_capabilities",caps},{"event_kind","user_message"},{"input","hi"}}.dump()},{"approved_receipts",Json::array({Json{{"ok",true},{"data","changing receipt"}}})},{"candidate",{{"text","hi"}}},{"target","reply"}};
    ayt::inference::Request request;request.messages={{"system","fixed"},{"user",frame.dump()}};events.append(request);
    const auto& baseline=request.messages[1].text;
    CHECK(baseline.find("conversation_delivery")<baseline.find("current_capabilities"));
    CHECK(baseline.find("current_capabilities")<baseline.find("event_context")&&baseline.find("event_context")<baseline.find("approved_receipts"));
    const auto authority=Json::parse(baseline).at("review_baseline").at("authority");
    CHECK(!authority.at("event_context").contains("current_capabilities"));
    CHECK(authority.at("current_plugin_state").at("widget").at("pose")=="idle"&&!authority.at("current_capabilities").at("plugins")[0].contains("current"));
}
TEST_CASE(review_event_capacity_and_images_start_a_complete_fresh_view){
    layered_review::Events events;Json frame={{"event_context",Json::object().dump()},{"candidate",std::string(1500,'x')}};
    ayt::inference::Request request;request.messages={{"system","policy"},{"user",frame.dump()}};events.append(request,2200);
    frame["candidate"]=std::string(1500,'y');request.messages={{"system","policy"},{"user",frame.dump()}};events.append(request,2200);
    CHECK(events.describe().at("rebases")==1&&request.messages.size()==3);
    CHECK(session::encode(request.messages).dump().find(std::string(1000,'x'))==std::string::npos);
    request.messages={{"system","policy"},{"user",frame.dump()}};request.messages[1].images.push_back({"data:image/png;base64,AAAA"});events.append(request);
    CHECK(request.messages.size()==2&&request.messages[1].images.size()==1&&Json::parse(request.messages[1].text).at("candidate")==frame.at("candidate"));
}
TEST_CASE(cost_summary_distinguishes_actor_and_review_retries_without_inventing_usage){
    const Json usage={{"input_tokens",80},{"output_tokens",12}};
    const auto summary=layered_review::usageSummary({{"turns",Json::array({Json{{"usage",usage}}})},{"reviews",Json::array({Json{{"target","tools"},{"usage",usage}},Json{{"target","reply"}}})},{"reviews_skipped",Json::array({Json::object()})}});
    CHECK(summary.at("actor").at("output_tokens")==12&&summary.at("tool_review").at("input_tokens")==80);
    CHECK(summary.at("reply_review").at("calls")==1&&summary.at("reply_review").at("unknown_usage_calls")==1&&summary.at("program_batches")==1);
    const Json attachment={{"enabled",true},{"settings",{{"mode","force-economy"}}}};
    const auto effective=pricing::apply(attachment,"https://api.deepseek.com","deepseek-flash",{{"reviewPolicy","layered"},{"reviewLayout","events"},{"reviewReasoningEffort","none"},{"reviewAttempts",2}},8,2,0);
    CHECK(effective.context.at("reviewPolicy")=="layered"&&effective.context.at("reviewLayout")=="events"&&effective.context.at("reviewAttempts")==2&&effective.context.at("reviewReasoningEffort")=="none");
}
TEST_SUITE_END
