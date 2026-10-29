#include <Hibernation/SessionProjection.h>
#include <Hibernation/FailureContinuation.h>
#include <AYTestFixtures.h>
using namespace hibernation;
using Json=nlohmann::json;
TEST_SUITE(SessionProjection)
TEST_CASE(delta_preserves_current_state_nulls_removals_and_source_order){
    const Json prior={{"event_kind","user_message"},{"event_id","one"},{"input","old"},{"body",{{"x",.1},{"old",true},{"catalog",std::string(6000,'c')}}},{"nullable","present"},{"authorized_context",Json::array({Json{{"id","z"},{"source","archive@1"},{"data",{{"text","first"}}}},Json{{"id","a"},{"source","archive@1"},{"data",{{"text","second"}}}}})}};
    auto now=prior;now["event_id"]="two";now["input"]="new";now["body"]["x"]=.9;now["body"].erase("old");now["nullable"]=nullptr;
    std::vector<session::Message> history={{"user",cacheOrderedEvent(prior)}};
    const auto old=session::rows(history);const auto wire=session::delta(now,prior,old,"epoch");
    CHECK(session::state(prior).patch(wire.at("projection_patch"))==session::state(now));CHECK(wire.at("authorized_context").empty());
    CHECK(wire.dump().size()<1000);CHECK(session::expand(now,wire.dump())==now);
    auto next=now;next["authorized_context"]=Json::array();const auto joined=session::joinRows(next,old);
    CHECK(joined.at("authorized_context")[0].at("id")=="z"&&joined.at("authorized_context")[1].at("id")=="a");
}
TEST_CASE(retained_request_messages_stay_byte_identical_and_bounded){
    session::Message call{"assistant",""};call.toolCalls.push_back({"call-1","ha_state_DOT_observe","{}"});
    session::Message receipt{"tool","{\"ok\":true}"};receipt.toolCallId="call-1";
    const Json old={{"event_id","first"},{"input","one"},{"authorized_context",Json::array()},{"body",{{"catalog",std::string(6000,'s')},{"x",0}}}};
    std::vector<session::Message> history={{"user",cacheOrderedEvent(old)},call,receipt,{"assistant","approved"}};
    const auto restored=session::decode(session::encode(history),32768);CHECK(session::encode(restored)==session::encode(history));
    session::Prepared run;run.maxBytes=32768;run.fullFrame=old;run.fullFrame["event_id"]="second";run.fullFrame["input"]="two";run.fullFrame["body"]["x"]=1;
    std::vector<session::Message> request={{"system","rules"},{"user","placeholder"}};session::append(run,request,history,old,"epoch");
    CHECK(run.reused&&run.eventIndex==5);CHECK(session::encode(std::vector<session::Message>(request.begin()+1,request.begin()+5))==session::encode(history));
    bool refused=false;try{session::decode(session::encode(history),128);}catch(...){refused=true;}CHECK(refused);
    history[0].images.push_back({"data:image/png;base64,AAAA"});refused=false;try{session::encode(history);}catch(...){refused=true;}CHECK(refused);
}
TEST_CASE(projection_configuration_is_bounded_and_full_mode_opts_out){
    const auto control=ContextControl::read(Json::object());CHECK(control.sessionProjection&&control.sessionBytes==131072);
    CHECK(!ContextControl::read({{"preset","full"}}).sessionProjection);CHECK(!ContextControl::read({{"sessionProjection",false}}).sessionProjection);
    for(const auto& v:{Json{{"sessionBytes",100}},Json{{"sessionMaxTurns",65}},Json{{"sessionProjection","true"}}}){bool rejected=false;try{ContextControl::read(v);}catch(...){rejected=true;}CHECK(rejected);}
}
TEST_CASE(old_segments_are_never_recompressed_with_the_current_receipts){
    const auto old=Json{{"ok",true},{"result",{{"content",std::string(20000,'o')}}}}.dump();
    const auto current=Json{{"ok",true},{"result",{{"content",std::string(20000,'n')}}}}.dump();
    std::vector<session::Message> request={{"user","old event"},{"tool",old},{"user","current event"},{"tool",current}};
    CHECK(compactCachedToolMessages(request,1024,2));CHECK(request[1].text==old);CHECK(request[3].text.size()<=1024);
}
TEST_CASE(narrative_references_never_become_content_access_on_restore){
    CHECK(session::referenceCanRead({{"access","content"}}));CHECK(!session::referenceCanRead({{"access","narrative"}}));
    for(const auto& value:{Json::object(),Json{{"access","unknown"}}}){bool failed=false;try{session::referenceCanRead(value);}catch(...){failed=true;}CHECK(failed);}
}
TEST_CASE(failed_reply_retains_exact_input_and_only_program_receipts_not_rejected_prose){
    session::Prepared run;run.fullFrame={{"event_id","user-1"},{"input","remember blue"},{"conversation_focus",nullptr}};run.failureFrame=run.fullFrame;run.failureFrame["conversation_focus"]={{"subject","shared"}};
    run.failureInput={"user",cacheOrderedEvent(run.fullFrame)};
    run.capturedMessages={{"assistant","REJECTED_REPLY_MUST_NOT_SURVIVE"},{"user","REVIEW_GUIDANCE_MUST_NOT_SURVIVE"}};
    failure_continuation::completed(run,"workspace.write",{{"path","blue.md"}},{{"ok",true},{"source_id","receipt-1"},{"result",{{"written",true}}}},"request-1.call-1");
    failure_continuation::completed(run,"activity.enqueue",Json::object(),{{"ok",true},{"result",{{"accepted",true},{"completed",false}}}},"request-1.call-2");
    const auto event=failure_continuation::outcome(run,"user-1","trace-1","review_rejected");const auto messages=failure_continuation::messages(run,event);const auto wire=session::encode(messages).dump();
    CHECK(messages.front().text==run.failureInput.text&&messages.front().role=="user");CHECK(wire.find("REJECTED_REPLY_MUST_NOT_SURVIVE")==std::string::npos&&wire.find("REVIEW_GUIDANCE_MUST_NOT_SURVIVE")==std::string::npos);
    CHECK(messages.size()==5&&Json::parse(messages[1].text).at("operation_receipt").at("receipt").at("result").at("written")==true);
    CHECK(Json::parse(messages[2].text).at("operation_receipt").at("receipt").at("result").at("completed")==false);
    CHECK(Json::parse(messages[3].text).at("runtime_event").at("changes").at("conversation_focus")==run.failureFrame.at("conversation_focus"));
    CHECK(Json::parse(messages.back().text).at("turn_outcome").at("reply_delivered")==false);
    for(const auto& message:messages)CHECK(message.role=="user"&&message.toolCalls.empty()&&message.toolCallId.empty()&&message.images.empty());
    const auto invalidated=failure_continuation::outcome(run,"user-1","trace-1","context_authorization_changed",false);CHECK(invalidated.at("context_invalidated")==true&&invalidated.at("completed_operations").empty());
    const Json record={{"id","failure-1"},{"data",{{"kind","turn_outcome"},{"text",std::string(3000,'f')}}}};
    const auto reviewed=reviewEventContext(Json{{"event_id","new-user"},{"previous_turn_outcome_id","failure-1"},{"authorized_context",Json::array({record})}}.dump(),Json{{"text","new reply"}},1024);
    CHECK(reviewed.at("authorized_context")==Json::array({record}));
}
TEST_SUITE_END
