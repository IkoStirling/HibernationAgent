#include <Hibernation/CacheContext.h>
#include <Hibernation/ActivityPlan.h>
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
TEST_SUITE_END
