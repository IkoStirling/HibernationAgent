#include <Hibernation/CacheContext.h>
#include <Hibernation/ActivityPlan.h>
#include <Hibernation/ReviewPrefix.h>
#include <AYTestFixtures.h>
#include <thread>
using namespace hibernation;
using Json=nlohmann::json;
namespace {std::size_t commonPrefix(const std::string& a,const std::string& b){std::size_t i=0;while(i<a.size()&&i<b.size()&&a[i]==b[i])++i;return i;}}
TEST_SUITE(CacheContext)
TEST_CASE(stable_context_precedes_volatile_state_without_changing_evidence){
    Json a={{"event_kind","user_message"},{"authoritative_state",{{"energy",.4}}},{"event_id","first"},{"body",{{"observed_at_ms",1000}}},{"authorized_context",Json::array({Json{{"id","history"},{"source","archive@1"},{"data",{{"text",std::string(6000,'h')}}}}})},{"input","hello"}};
    auto b=a;b["authoritative_state"]["energy"]=.5;b["event_id"]="second";b["body"]["observed_at_ms"]=2000;b["input"]="new input";
    const auto one=cacheOrderedEvent(a),two=cacheOrderedEvent(b);CHECK(Json::parse(one)==a&&Json::parse(two)==b);CHECK(commonPrefix(one,two)>6000);CHECK(commonPrefix(a.dump(),b.dump())<100);
    Json review={{"approved_receipts",Json::array({1})},{"candidate",{{"text","a"}}},{"event_context",one}};auto next=review;next["approved_receipts"]=Json::array({2});next["candidate"]["text"]="b";CHECK(commonPrefix(cacheOrderedReview(review),cacheOrderedReview(next))>6000);CHECK(Json::parse(cacheOrderedReview(review))==review);
    Json planning={{"body",{{"observed_at_ms",1000}}},{"memories",std::string(6000,'m')},{"time_context",{{"now_ms",1000}}}};auto later=planning;later["body"]["observed_at_ms"]=2000;later["time_context"]["now_ms"]=2000;CHECK(commonPrefix(cacheOrderedBackground(planning),cacheOrderedBackground(later))>6000);CHECK(Json::parse(cacheOrderedBackground(planning))==planning);
}
TEST_CASE(cache_ratios_distinguish_missing_reports_from_real_zero_hits){
    ayt::inference::Usage usage;usage.inputTokens=100;usage.outputTokens=10;auto unknown=cacheUsageJson(usage);CHECK(unknown.at("cached_input_tokens").is_null());CHECK(unknown.at("cache_hit_ratio").is_null());
    usage.cachedInputTokens=80;usage.uncachedInputTokens=20;auto known=cacheUsageJson(usage);CHECK(known.at("cache_hit_ratio")==.8);
    auto total=addCacheUsage(Json::object(),unknown);total=addCacheUsage(total,known);CHECK(total.at("calls")==2);CHECK(total.at("input_tokens")==200);CHECK(total.at("cache_reported_calls")==1);CHECK(total.at("cache_hit_ratio")==.8);
    usage.cachedInputTokens=0;usage.uncachedInputTokens=100;total=addCacheUsage(total,cacheUsageJson(usage));CHECK(total.at("cache_reported_calls")==2);CHECK(total.at("cache_hit_ratio")==.4);
}
TEST_CASE(nested_review_evidence_retains_prefix_and_all_current_facts){
    Json context={{"memories",std::string(12000,'m')},{"self",std::string(6000,'s')},{"evidence",Json::array({Json{{"id","source@1"},{"text","actual experience"}}})},{"body",{{"observed_at_ms",1000},{"x",.2}}},{"state",{{"energy",.5}}}};
    Json a={{"context",context},{"candidate",{{"text","first"}}}},b=a;
    b["context"]["body"]["observed_at_ms"]=2000;b["context"]["body"]["x"]=.8;b["context"]["state"]["energy"]=.4;b["candidate"]["text"]="second";
    CHECK(commonPrefix(a.dump(),b.dump())<200);
    CHECK(commonPrefix(cacheOrderedReview(a),cacheOrderedReview(b))>18000);
    CHECK(Json::parse(cacheOrderedReview(a))==a&&Json::parse(cacheOrderedReview(b))==b);
    b["context"]["evidence"][0]["id"]="new-source@2";
    CHECK(commonPrefix(cacheOrderedReview(a),cacheOrderedReview(b))>18000);
    CHECK(Json::parse(cacheOrderedReview(b)).at("context").at("evidence")[0].at("id")=="new-source@2");
}
TEST_CASE(nested_event_and_qq_mutations_do_not_restore_alphabetical_order){
    Json event={{"authorized_context",Json::array({Json{{"id","archive@1"},{"data",{{"text",std::string(12000,'h')}}}}})},{"event_kind","user_message"},{"authoritative_state",{{"energy",.3}}},{"channel_identity",{{"sender","owner"}}}};
    auto next=event;next["authoritative_state"]["energy"]=.7;
    Json a={{"event_context",event.dump()},{"candidate",{{"text","one"}}}},b={{"event_context",next.dump()},{"candidate",{{"text","two"}}}};
    CHECK(commonPrefix(cacheOrderedReview(a),cacheOrderedReview(b))>12000);
    CHECK(Json::parse(Json::parse(cacheOrderedReview(b)).at("event_context").get<std::string>())==next);
    CHECK(commonPrefix(cacheOrderedEvent(event),cacheOrderedEvent(next))>12000);
    event["authorized_context"]=Json::array();const auto revoked=Json::parse(cacheOrderedEvent(event));
    CHECK(revoked.at("authorized_context").empty());CHECK(cacheOrderedEvent(event).find(std::string(100,'h'))==std::string::npos);
    CHECK(Json::parse(cacheOrderedReview(Json{{"event_context","not-json"}})).at("event_context")=="not-json");
}
TEST_CASE(stable_activity_declarations_do_not_authorize_unavailable_commands){
    Profile actor;actor.id="mock";Json context={{"available_commands",Json::array({"rest"})},{"declared_commands",Json::array({"wait","rest"})}};
    ActivityGenerator generator;generator.start(actor,std::nullopt,Json{{"displayName","fixture"}},context,"cache-test");
    std::optional<ActivityBatch> result;
    for(int i=0;i<1000&&!result;++i){result=generator.poll();if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(result.has_value());if(result)CHECK(result->status=="activity_unavailable_kind");
    context["available_commands"]=Json::array({"wait"});generator.start(actor,std::nullopt,Json{{"displayName","fixture"}},context,"cache-test-allowed");result.reset();
    for(int i=0;i<1000&&!result;++i){result=generator.poll();if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(result.has_value());if(result)CHECK(result->status=="ok");
}
TEST_CASE(tool_receipts_append_until_the_bounded_window_requires_compaction){
    const auto receipt=Json{{"ok",true},{"source_id","observed@1"},{"result",{{"content",std::string(8000,'t')}}}}.dump();
    std::vector<ayt::inference::Message> messages={{"system","rules"},{"user","input"},{"tool",receipt}};
    CHECK(!compactCachedToolMessages(messages,4096));CHECK(messages[2].text==receipt);
    messages.push_back({"tool",Json{{"ok",true},{"result",{{"content",std::string(40000,'x')}}}}.dump()});
    CHECK(compactCachedToolMessages(messages,4096));
    CHECK(messages[0].text=="rules"&&messages[1].text=="input");
    CHECK(Json::parse(messages[2].text).at("source_id")=="observed@1");
    CHECK(messages[2].text.size()<=4096&&messages[3].text.size()<=4096);
}
TEST_CASE(event_route_changes_follow_identical_authorized_history_without_adding_evidence){
    Json original={{"event_kind","user_message"},{"conversation_delivery",{{"mode","direct"}}},{"authorized_context",Json::array({{{"id","allowed-source"},{"scope","ha.archive"},{"revision",7},{"data",{{"text",std::string(12000,'h')}}}}})},{"input","hello"},{"authoritative_state",{{"energy",.3}}}};
    auto next=original;next["event_kind"]="outer_event";next["input"]="local event";next["authoritative_state"]["energy"]=.8;
    const auto a=cacheOrderedEvent(original),b=cacheOrderedEvent(next);CHECK(Json::parse(a)==original&&Json::parse(b)==next);CHECK(commonPrefix(a,b)>12000);CHECK(a.size()==original.dump().size()&&b.size()==next.dump().size());CHECK(a.find("authorized_context")<a.find("event_kind"));
    next["authorized_context"]=Json::array();CHECK(cacheOrderedEvent(next).find(std::string(100,'h'))==std::string::npos);CHECK(Json::parse(cacheOrderedEvent(next))==next);
}
TEST_CASE(cache_counters_distinguish_zero_partial_and_unknown_reports){
    auto total=addCacheUsage(Json::object(),{{"output_tokens",10}});CHECK(total.at("input_tokens_known")==false&&total.at("output_tokens_known")==true);CHECK(total.at("input_reported_calls")==0&&total.at("output_reported_calls")==1&&total.at("complete_usage_reported_calls")==0);
    total=addCacheUsage(total,{{"input_tokens",0},{"output_tokens",0},{"cached_input_tokens",0},{"uncached_input_tokens",0}});CHECK(total.at("input_tokens_known")==true&&total.at("input_tokens")==0);CHECK(total.at("input_reported_calls")==1&&total.at("output_reported_calls")==2&&total.at("complete_usage_reported_calls")==1);CHECK(total.at("cache_reported_calls")==1&&total.at("cache_hit_ratio").is_null());
    total=addCacheUsage(total,{{"input_tokens",nullptr},{"output_tokens",-1},{"cached_input_tokens",false},{"uncached_input_tokens",1.5}});CHECK(total.at("calls")==3&&total.at("usage_reported_calls")==2);CHECK(total.at("input_reported_calls")==1&&total.at("output_tokens")==10);
}
TEST_CASE(legacy_reporting_details_stay_unknown_and_known_zero_is_not_invented){
    Json legacy={{"calls",3},{"usage_reported_calls",2},{"cache_reported_calls",0},{"input_tokens",0},{"output_tokens",10},{"cached_input_tokens",0},{"uncached_input_tokens",0}};
    auto total=addCacheUsage(legacy,Json::object());CHECK(total.at("input_reported_calls").is_null()&&total.at("output_reported_calls").is_null()&&total.at("complete_usage_reported_calls").is_null());CHECK(total.at("input_tokens_known")==false&&total.at("output_tokens_known")==true);CHECK(total.at("output_tokens")==10&&total.at("calls")==4);
    total=addCacheUsage(total,{{"input_tokens",0}});CHECK(total.at("input_tokens_known")==true&&total.at("input_reported_calls").is_null());CHECK(total.at("input_tokens")==0&&total.at("calls")==5);
}
TEST_CASE(lifetime_buckets_retain_overflow_usage_without_growing_identifier_lists){
    Json state={{"version",1},{"since_ms",1},{"connections",Json::object()}};const Json usage={{"input_tokens",10},{"output_tokens",2}};
    for(int i=0;i<30;++i)state=addModelCacheUsage(std::move(state),"actor","purpose-"+std::to_string(i),usage);
    CHECK(state.at("connections").at("actor").at("total").at("calls")==30);CHECK(state.at("connections").at("actor").at("purposes").size()==25);CHECK(state.at("connections").at("actor").at("purposes").at("$other_purposes").at("calls")==6);
    for(int i=1;i<40;++i)state=addModelCacheUsage(std::move(state),"profile-"+std::to_string(i),"dialogue",usage);
    CHECK(state.at("connections").size()==33);CHECK(state.at("connections").at("$other_profiles").at("total").at("calls")==8);CHECK(state.at("connections").at("$other_profiles").at("aggregation")=="additional_profiles");
    std::uint64_t calls=0,input=0;for(const auto& connection:state.at("connections")){calls+=connection.at("total").at("calls").get<std::uint64_t>();input+=connection.at("total").at("input_tokens").get<std::uint64_t>();std::uint64_t grouped=0;for(const auto& value:connection.at("purposes"))grouped+=value.at("calls").get<std::uint64_t>();CHECK(grouped==connection.at("total").at("calls"));}
    CHECK(calls==69&&input==690);CHECK(state.at("since_ms")==1&&state.at("version")==1);
}
TEST_CASE(usage_totals_saturate_instead_of_wrapping_to_zero){
    const auto maximum=std::numeric_limits<std::uint64_t>::max();Json total={{"calls",maximum},{"input_tokens",maximum},{"output_tokens",maximum}};total=addCacheUsage(total,{{"input_tokens",1},{"output_tokens",2}});CHECK(total.at("calls")==maximum&&total.at("input_tokens")==maximum&&total.at("output_tokens")==maximum);CHECK(total.at("counter_saturated")==true);
    total=addCacheUsage(Json{{"cached_input_tokens",maximum},{"uncached_input_tokens",1}},{{"cached_input_tokens",1},{"uncached_input_tokens",0}});CHECK(total.at("cache_counter_saturated")==true&&total.at("cache_hit_ratio").is_null());
}
TEST_SUITE_END

namespace {
Json prefixRefs(const std::string& id="source"){return Json::array({{{"scope","ha.archive"},{"id",id},{"revision",1},{"kind","user_message"}}});}
ayt::inference::Request prefixRequest(const std::string& text="SOURCE_SECRET",const std::string& candidate="CANDIDATE_ONE"){
    Json event={{"authorized_context",Json::array({{{"id","source"},{"source","archive@1"},{"data",{{"text",text}}}}})},{"input","current input"}};
    ayt::inference::Request request;request.outputSchema="{\"type\":\"object\"}";request.messages={{"system","Only audit the current candidate."},{"user",Json{{"event_context",event.dump()},{"current_capabilities",{{"computer",false}}},{"authoritative_state",{{"energy",.5}}},{"candidate",{{"text",candidate}}}}.dump()}};return request;
}
std::string prefixWire(const ayt::inference::Request& r){return session::encode(r.messages).dump();}
}
TEST_SUITE(ReviewPrefix)
TEST_CASE(actor_compatibility_ignores_review_only_policy_and_keeps_actor_limits){
    auto a=ContextControl::read({{"reviewLayout","events"},{"reviewAttempts",2}});auto b=ContextControl::read({{"reviewLayout","prefix"},{"reviewAttempts",1},{"reviewReasoningEffort","low"}});
    CHECK(session::actorControl(a)==session::actorControl(b));b.historyBytes+=1;CHECK(session::actorControl(a)!=session::actorControl(b));
}
TEST_CASE(cross_turn_prefix_keeps_definitions_without_candidates_or_verdicts){
    auto allow=[](const Json&){return true;};auto first=prefixRequest(std::string(8000,'s'));auto saved=review_prefix::prepare(first,Json::object(),{{"revision",1}},prefixRefs(),allow,1000);
    CHECK(saved.persist);CHECK(saved.state.dump().find("CANDIDATE_ONE")==std::string::npos);for(const auto& view:saved.state.at("views").at("history"))CHECK(!Json::parse(view.at("text").get<std::string>()).contains("review_snapshot"));
    auto second=prefixRequest(std::string(8000,'s'),"CANDIDATE_TWO");auto next=review_prefix::prepare(second,saved.state,{{"revision",1}},prefixRefs(),allow,2000);
    CHECK(next.metadata.at("reused")==true);CHECK(prefixWire(second).find("CANDIDATE_ONE")==std::string::npos);CHECK(prefixWire(second).find("CANDIDATE_TWO")!=std::string::npos);
    CHECK(first.messages.size()==second.messages.size());for(std::size_t i=0;i+1<first.messages.size();++i)CHECK(first.messages[i].text==second.messages[i].text);
    CHECK(Json::parse(second.messages.back().text).at("review_snapshot").at("data").at("authoritative_state").at("energy")==.5);
}
TEST_CASE(revoked_sources_are_removed_before_the_next_review_request){
    auto first=prefixRequest();auto saved=review_prefix::prepare(first,Json::object(),Json::object(),prefixRefs(),[](const Json&){return true;},1000);
    auto nextRequest=prefixRequest("PUBLIC_SOURCE","NEXT");auto next=review_prefix::prepare(nextRequest,saved.state,Json::object(),prefixRefs("replacement"),[](const Json& ref){return ref.at("id")!="source";},2000);
    CHECK(next.metadata.at("reason")=="source_changed");CHECK(next.metadata.at("reused")==false);CHECK(prefixWire(nextRequest).find("SOURCE_SECRET")==std::string::npos);
    bool rejected=false;try{auto bad=prefixRequest();review_prefix::prepare(bad,Json::object(),Json::object(),prefixRefs(),[](const Json&){return false;},3000);}catch(...){rejected=true;}CHECK(rejected);
}
TEST_CASE(live_plugin_state_is_never_retained_as_a_capability_definition){
    const auto allow=[](const Json&){return true;};auto first=prefixRequest();auto frame=Json::parse(first.messages[1].text);frame["current_capabilities"]["plugins"]=Json::array({{{"id","television"},{"enabled",true},{"current",{{"title","OLD_PRIVATE_RUNTIME_TITLE"}}}}});first.messages[1].text=frame.dump();auto saved=review_prefix::prepare(first,Json::object(),Json::object(),prefixRefs(),allow,1000);
    CHECK(saved.state.at("views").at("history").dump().find("OLD_PRIVATE_RUNTIME_TITLE")==std::string::npos);CHECK(prefixWire(first).find("OLD_PRIVATE_RUNTIME_TITLE")!=std::string::npos);
    auto next=prefixRequest();frame["current_capabilities"]["plugins"][0]["current"]["title"]="NEW_CURRENT_TITLE";next.messages[1].text=frame.dump();auto result=review_prefix::prepare(next,saved.state,Json::object(),prefixRefs(),allow,2000);
    CHECK(result.metadata.at("reused")==true);CHECK(prefixWire(next).find("OLD_PRIVATE_RUNTIME_TITLE")==std::string::npos);CHECK(Json::parse(next.messages.back().text).at("review_snapshot").at("data").at("current_plugin_state").at("television").at("title")=="NEW_CURRENT_TITLE");
}
TEST_CASE(fences_signatures_time_and_capacity_reset_reuse_without_dropping_current_input){
    const auto allow=[](const Json&){return true;};auto first=prefixRequest();auto saved=review_prefix::prepare(first,Json::object(),{{"access",1}},prefixRefs(),allow,1000);
    auto changed=prefixRequest();auto reset=review_prefix::prepare(changed,saved.state,{{"access",2}},prefixRefs(),allow,2000);CHECK(reset.metadata.at("reused")==false);
    changed=prefixRequest();changed.messages.front().text="Different actual reviewer policy.";reset=review_prefix::prepare(changed,saved.state,{{"access",1}},prefixRefs(),allow,2000);CHECK(reset.metadata.at("reason")=="signature_changed");CHECK(reset.metadata.at("reused")==false);
    changed=prefixRequest();reset=review_prefix::prepare(changed,saved.state,{{"access",1}},prefixRefs(),allow,86402000);CHECK(reset.metadata.at("reason")=="expired");
    changed=prefixRequest(std::string(9000,'x'));reset=review_prefix::prepare(changed,saved.state,{{"access",1}},prefixRefs(),allow,2000,2000);CHECK(!reset.persist);CHECK(prefixWire(changed).find(std::string(9000,'x'))!=std::string::npos);
}
TEST_CASE(background_review_reuses_evidence_and_keeps_current_plan_independent){
    auto request=[](int state){ayt::inference::Request r;r.messages={{"system","Review desktop planning."},{"user",Json{{"context",{{"memories",Json::array({{{"id","source"},{"text",std::string(6000,'m')}}})},{"available_commands",Json::array({"wait"})},{"state",state}}},{"candidate",{{"commands",Json::array({{{"kind","wait"},{"seconds",state}}})}}}}.dump()}};return r;};
    const auto allow=[](const Json&){return true;};auto a=request(1);auto p=review_prefix::prepare(a,Json::object(),Json::object(),prefixRefs(),allow,1000);auto b=request(2);auto q=review_prefix::prepare(b,p.state,Json::object(),prefixRefs(),allow,2000);
    CHECK(q.metadata.at("reused")==true);CHECK(Json::parse(b.messages.back().text).at("review_snapshot").at("data").at("context").at("state")==2);for(const auto& view:q.state.at("views").at("history")){const auto data=Json::parse(view.at("text").get<std::string>());CHECK(data.contains("review_evidence")||data.contains("review_capability"));}
}
TEST_SUITE_END
