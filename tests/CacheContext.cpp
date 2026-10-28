#include <Hibernation/CacheContext.h>
#include <AYTestFixtures.h>
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
TEST_SUITE_END
