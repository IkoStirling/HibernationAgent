#include <Hibernation/IdleEconomy.h>
#include <Hibernation/BackgroundBudget.h>
#include <Hibernation/ActivityPlan.h>
#include <AYSerializer/JsonSchema.h>
#include <AYTestFixtures.h>
using namespace hibernation;
TEST_SUITE(IdleEconomy)
TEST_CASE(a_day_without_machine_input_keeps_sparse_work_and_protects_return_budget){
    auto state=background::fresh(100000);background::Policy policy;policy.autoEconomy=true;std::int64_t last=0;int admitted=0;
    for(std::int64_t t=60000;t<=86400000;t+=60000){const auto mode=idle_economy::band(t/1000);const auto view=background::status(policy,state,100000+t);
        if(idle_economy::due(mode,last,t)&&idle_economy::reserveAvailable(view)){last=t;++admitted;state=background::reserve(state,2);for(int i=0;i<2;++i)state=background::observe(state,{{"input_tokens",20000},{"cached_input_tokens",18000},{"uncached_input_tokens",2000}});}}
    CHECK(admitted>20&&admitted<=56);CHECK(state.at("calls").get<int>()<=112);CHECK(state.at("input_tokens").get<int>()<500000);CHECK(state.at("total_input_tokens").get<int>()>500000);
    CHECK(idle_economy::due(0,last,last+1));CHECK(!idle_economy::due(2,last,last+1000));
}
TEST_CASE(return_reserve_does_not_raise_or_clear_any_budget){
    background::Policy policy;auto state=background::fresh(100000);state=background::reserve(state,113);const auto old=state;CHECK(!idle_economy::reserveAvailable(background::status(policy,state,100000)));CHECK(state==old);CHECK(background::canReserve(policy,state,1));
    state=background::fresh(100000);state=background::observe(state,{{"input_tokens",310000},{"uncached_input_tokens",310000},{"cached_input_tokens",0}});CHECK(!idle_economy::reserveAvailable(background::status(policy,state,100000)));CHECK(background::canReserve(policy,state,1));
}
TEST_CASE(away_schema_limits_background_batch_without_restricting_direct_actions){
    const Json available=Json::array({"wait","rest","creative_note","move_to","pursue_intention"});const auto quiet=idle_economy::commands(available,2);CHECK(quiet.size()==4);CHECK(idle_economy::commands(available,0)==available);
    Json batch={{"version",1},{"commands",Json::array({{{"kind","wait"},{"seconds",1}},{{"kind","wait"},{"seconds",1}}})}};
    CHECK(!ayt::serializer::validateJsonDocument(batch.dump(),activitySchemaFor(quiet,1)));CHECK(ayt::serializer::validateJsonDocument(batch.dump(),activitySchemaFor(available)));
}
TEST_CASE(small_background_contexts_keep_required_factual_sources_and_normal_dialogue){
    CHECK(idle_economy::historyBytes(80000,1000,true,false)==16000);CHECK(idle_economy::historyBytes(80000,1000,false,true)==12000);
    CHECK(idle_economy::historyBytes(80000,25000,true,false)==25000);CHECK(idle_economy::historyBytes(80000,1000,false,false)==80000);
}
TEST_CASE(active_presence_can_use_background_allowance_without_spending_away_reserve){
    background::Policy policy;auto state=background::fresh(100000);state=background::reserve(state,18);state=background::observe(state,{{"input_tokens",303699},{"uncached_input_tokens",303699},{"cached_input_tokens",0}});
    const auto budget=background::status(policy,state,100000);const auto unchanged=state;
    CHECK(idle_economy::reserveAvailable(budget,0));CHECK(!idle_economy::reserveAvailable(budget,1));CHECK(!idle_economy::reserveAvailable(budget,2));CHECK(state==unchanged);
    state["input_tokens"]=450000;CHECK(!idle_economy::reserveAvailable(background::status(policy,state,100000),0));
    state["input_tokens"]=0;state["calls"]=118;CHECK(!idle_economy::reserveAvailable(background::status(policy,state,100000),0));
}
TEST_SUITE_END
