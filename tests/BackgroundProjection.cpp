#include <Hibernation/BackgroundProjection.h>
#include <Hibernation/ReviewSnapshots.h>
#include <Hibernation/ActivityPlan.h>
#include <thread>
#include <AYTestFixtures.h>
using Json=nlohmann::json;
using namespace hibernation;
TEST_SUITE(BackgroundProjection)
TEST_CASE(history_is_bound_to_policy_model_and_every_retained_source){
    Json state={{"enabled",true}},fence={{"access",1},{"dialogue",2},{"stage",3}};const Json refs=Json::array({Json{{"id","one"},{"scope","ha.archive"},{"revision",1},{"kind","user_message"}}});
    const auto cache=background_projection::bind(state,"profile-a",fence,refs);CHECK(background_projection::reusable(cache,"profile-a",fence,[](const Json&){return true;}));
    CHECK(!background_projection::reusable(cache,"profile-b",fence,[](const Json&){return true;}));fence["access"]=2;CHECK(!background_projection::reusable(cache,"profile-a",fence,[](const Json&){return true;}));fence["access"]=1;
    CHECK(!background_projection::reusable(cache,"profile-a",fence,[](const Json&){return false;}));CHECK(!background_projection::reusable(Json::object(),"profile-a",fence,[](const Json&){return true;}));
}
TEST_CASE(planning_stream_restores_exact_input_and_latest_scene_is_complete){
    review_snapshots::Stream first("planning");Json context={{"declared_commands",Json::array({"wait","rest","eat_nearest"})},{"available_commands",Json::array({"rest"})},{"state",{{"energy",.3}}},{"memories",Json::array({Json{{"kind","conversation"},{"text","纸船叫折角"}}})},{"evidence",Json::array()}};
    ayt::inference::Request request;request.messages={{"system","planning rules"},{"user",context.dump()}};first.append(request);const auto before=session::encode(request.messages);
    review_snapshots::Stream restored("planning");CHECK(restored.restore(first.save()));context["state"]["energy"]=.2;request.messages={{"system","planning rules"},{"user",context.dump()}};restored.append(request);
    CHECK(session::encode(std::vector<session::Message>(request.messages.begin(),request.messages.end()-1))==before);
    const auto current=Json::parse(request.messages.back().text).at("planning_snapshot").at("data");CHECK(current.at("state").at("energy")==.2&&current.at("available_commands")==Json::array({"rest"}));
    CHECK(current.at("declared_commands").contains("$planning_capability"));review_snapshots::Stream wrongDomain;CHECK(!wrongDomain.restore(first.save()));
    auto invalid=first.save();invalid["history"][0]["role"]="assistant";CHECK(!restored.restore(invalid));
}
TEST_CASE(revocation_stops_planning_before_budget_or_external_calls){
    ActivityGenerator generator;Profile actor;actor.model.protocol=ayt::inference::Protocol::Mock;int calls=0;
    generator.start(actor,{},Json::object(),Json::object(),"scope",[&]{++calls;return true;},{},{},Json::object(),[]{return false;});
    std::optional<ActivityBatch> result;for(int i=0;i<100&&!result;++i){result=generator.poll();if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(result&&result->status=="cancelled"&&calls==0&&result->usage.at("context_authorization_changed")==true);
}
TEST_CASE(complete_scene_layout_keeps_full_latest_authority_without_reference_reconstruction){
    review_snapshots::Stream stream("planning",false);
    Json old={{"declared_commands",Json::array({"wait","eat_nearest"})},{"available_commands",Json::array({"eat_nearest"})},{"memories",Json::array({Json{{"text","an earlier observation"}}})}};
    ayt::inference::Request request;request.messages={{"system","latest complete scene only"},{"user",old.dump()}};stream.append(request);const auto first=request.messages;
    auto current=old;current["available_commands"]=Json::array({"wait"});current["memories"]=Json::array();
    request.messages={{"system","latest complete scene only"},{"user",current.dump()}};stream.append(request);
    CHECK(request.messages.size()==first.size()+1&&request.messages[1].text==first[1].text);
    CHECK(Json::parse(request.messages.back().text).at("planning_snapshot").at("data")==current);
}
TEST_SUITE_END
