#include <Hibernation/ReviewSnapshots.h>
#include <AYTestFixtures.h>
using Json=nlohmann::json;
using namespace hibernation;
TEST_SUITE(ReviewSnapshots)
TEST_CASE(stable_capabilities_and_evidence_are_not_repeated_or_rewritten){
    review_snapshots::Stream stream;const Json caps={{"tools",Json::array({"observe","rest"})}};
    const Json evidence={{"id","source-one"},{"source","archive@1"},{"data",{{"text",std::string(3000,'s')}}}};
    const Json context={{"event_kind","user_message"},{"authorized_context",Json::array({evidence})},{"current_capabilities",caps},{"authoritative_state",{{"energy",.7}}}};
    auto frame=Json{{"target","tools"},{"candidate",{{"tool","rest"}}},{"current_capabilities",caps},{"authoritative_state",{{"energy",.7}}},{"event_context",cacheOrderedEvent(context)}};
    ayt::inference::Request first;first.messages={{"system","policy"},{"user",cacheOrderedReview(frame)}};stream.append(first);const auto before=session::encode(first.messages);
    CHECK(first.messages.size()==4);frame["candidate"]={{"tool","observe"}};frame["authoritative_state"]["energy"]=.5;
    ayt::inference::Request next;next.messages={{"system","policy"},{"user",cacheOrderedReview(frame)}};stream.append(next);
    CHECK(next.messages.size()==5&&session::encode(std::vector<session::Message>(next.messages.begin(),next.messages.begin()+4))==before);
    const auto wire=Json::parse(next.messages.back().text).at("review_snapshot");CHECK(wire.at("sequence")==2&&wire.at("data").at("candidate").at("tool")=="observe");
    CHECK(next.messages.back().text.find(std::string(1000,'s'))==std::string::npos);CHECK(stream.describe().at("evidence_views")==1);
}
TEST_CASE(capacity_reset_and_visual_review_do_not_keep_an_old_working_context){
    review_snapshots::Stream stream;Json frame={{"target","reply"},{"candidate",{{"text",std::string(1500,'a')}}}};
    ayt::inference::Request request;request.messages={{"system","policy"},{"user",frame.dump()}};stream.append(request,2200);
    frame["candidate"]["text"]=std::string(1500,'b');request.messages={{"system","policy"},{"user",frame.dump()}};stream.append(request,2200);
    CHECK(stream.describe().at("rebases")==1&&request.messages.size()==2);CHECK(request.messages.back().text.find(std::string(1000,'a'))==std::string::npos);
    stream.clear();CHECK(stream.describe().at("evidence_views")==0);
    request.messages={{"system","policy"},{"user",frame.dump()}};request.messages[1].images.push_back({"data:image/png;base64,AAAA"});const auto original=request.messages[1].text;stream.append(request);
    CHECK(request.messages.size()==2&&request.messages[1].text==original&&request.messages[1].images.size()==1);
}
TEST_CASE(protocol_repair_inputs_remain_in_the_next_exact_prefix_without_replaying_bad_output){
    review_snapshots::Stream stream;ayt::inference::Request request;request.messages={{"system","policy"},{"user",Json{{"candidate","first"}}.dump()}};stream.append(request);stream.repair(request);const auto sent=session::encode(request.messages);
    review_snapshots::Stream restored;CHECK(restored.restore(stream.save()));
    request.messages={{"system","policy"},{"user",Json{{"candidate","next"}}.dump()}};restored.append(request);
    CHECK(session::encode(std::vector<session::Message>(request.messages.begin(),request.messages.end()-1))==sent);
    CHECK(request.messages.back().text.find("next")!=std::string::npos);
}
TEST_CASE(view_only_retention_drops_old_candidates_and_keeps_current_permission_boundary){
    review_snapshots::Stream stream;Json context={{"authorized_context",Json::array({Json{{"id","one"},{"source","archive@1"},{"data",{{"text","allowed evidence"}}}}})}};
    Json frame={{"current_capabilities",{{"tools",Json::array({"rest"})}}},{"event_context",context.dump()},{"candidate",{{"text","OLDER_REJECTED_CANDIDATE"}}}};
    ayt::inference::Request request;request.messages={{"system","policy"},{"user",frame.dump()}};stream.append(request);const auto cap=request.messages[1].text,evidence=request.messages[2].text;
    stream.retainViews();context["authorized_context"]=Json::array();frame["event_context"]=context.dump();frame["candidate"]["text"]="new candidate";
    request.messages={{"system","policy"},{"user",frame.dump()}};stream.append(request);
    CHECK(request.messages[1].text==cap&&request.messages[2].text==evidence&&request.messages.size()==4);
    CHECK(session::encode(request.messages).dump().find("OLDER_REJECTED_CANDIDATE")==std::string::npos);
    CHECK(Json::parse(Json::parse(request.messages.back().text).at("review_snapshot").at("data").at("event_context").get<std::string>()).at("authorized_context").empty());
}
TEST_CASE(review_event_route_does_not_break_the_identical_authorized_prefix){
    Json event={{"event_kind","user_message"},{"conversation_delivery",{{"mode","direct"}}},{"authorized_context",Json::array({{{"id","one-source"},{"scope","ha.archive"},{"revision",3},{"data",{{"text",std::string(18000,'p')}}}}})},{"authoritative_state",{{"energy",.3}}}};auto other=event;other["event_kind"]="task_complete";other["authoritative_state"]["energy"]=.5;
    Json first={{"event_context",event.dump()},{"candidate",{{"text","first"}}}},next={{"event_context",other.dump()},{"candidate",{{"text","second"}}}};
    const auto a=cacheOrderedReview(first),b=cacheOrderedReview(next);std::size_t prefix=0;while(prefix<a.size()&&prefix<b.size()&&a[prefix]==b[prefix])++prefix;CHECK(prefix>18000);auto decodedA=Json::parse(a),decodedB=Json::parse(b);CHECK(Json::parse(decodedA.at("event_context").get<std::string>())==event);CHECK(Json::parse(decodedB.at("event_context").get<std::string>())==other);CHECK(decodedA.at("candidate")==first.at("candidate")&&decodedB.at("candidate")==next.at("candidate"));
    other["authorized_context"]=Json::array();next["event_context"]=other.dump();CHECK(cacheOrderedReview(next).find(std::string(100,'p'))==std::string::npos);
}
TEST_SUITE_END
