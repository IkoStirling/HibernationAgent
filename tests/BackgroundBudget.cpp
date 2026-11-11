#include <Hibernation/BackgroundBudget.h>
#include <Hibernation/ActivityPlan.h>
#include <Hibernation/CacheContext.h>
#include <httplib.h>
#include <thread>
#include <AYTestFixtures.h>
using namespace hibernation::background;
namespace {bool rejected(const Json& value){try{Policy::read(value);return false;}catch(const std::exception&){return true;}}}
TEST_SUITE(BackgroundBudget)
TEST_CASE(defaults_and_strict_live_configuration){
    const auto policy=Policy::read(Json::object());CHECK(policy.enabled&&policy.calls==120&&policy.inputTokens==500000);
    CHECK(rejected({{"enabled",1}}));CHECK(rejected({{"maxCallsPerDay",0}}));CHECK(rejected({{"maxCallsPerDay",1.5}}));CHECK(rejected({{"maxInputTokensPerDay",999}}));CHECK(rejected({{"unexpected",true}}));
    CHECK(Policy::read(policy.json()).json()==policy.json());
}
TEST_CASE(only_uncached_input_is_charged_and_cache_unknowns_stay_conservative){
    auto state=reserve(fresh(100000),3);state=observe(state,{{"input_tokens",10000},{"cached_input_tokens",9900},{"uncached_input_tokens",100}});
    CHECK(state.at("input_tokens")==100);CHECK(state.at("total_input_tokens")==10000);CHECK(state.at("cached_input_tokens")==9900);
    state=observe(state,{{"input_tokens",500},{"cached_input_tokens",400}});CHECK(state.at("input_tokens")==200);CHECK(state.at("cached_input_tokens")==10300);
    state=observe(state,{{"input_tokens",300}});CHECK(state.at("input_tokens")==500);CHECK(state.at("unknown_cache_input_tokens")==300);CHECK(state.at("reported_calls")==3);
    const auto old=state;state=observe(state,{{"input_tokens",nullptr},{"uncached_input_tokens",nullptr}});CHECK(state==old);
    state=observe(state,{{"input_tokens",100},{"cached_input_tokens",1000},{"uncached_input_tokens",1}});CHECK(state.at("input_tokens")==600);CHECK(state.at("unknown_cache_input_tokens")==400);
}
TEST_CASE(legacy_reclassification_requires_matching_accounting_window_and_preserves_calls){
    Json legacy={{"version",1},{"day_index",0},{"calls",14},{"input_tokens",1200},{"reported_calls",12},{"last_seen_ms",100000}};auto state=normalize(legacy,100000);
    CHECK(state.at("input_tokens")==1200&&state.at("unknown_cache_input_tokens")==1200);
    const Json baseline={{"connections",{{"a",{{"purposes",{{"activity",{{"input_tokens",800},{"cached_input_tokens",600},{"uncached_input_tokens",200}}}}}}}}}};
    auto current=baseline;current["connections"]["a"]["purposes"]["activity"]={{"input_tokens",2000},{"cached_input_tokens",1700},{"uncached_input_tokens",300}};
    CHECK(recoverLegacyCacheSplit(state,baseline,current));CHECK(state.at("calls")==14&&state.at("reported_calls")==12);CHECK(state.at("input_tokens")==100&&state.at("total_input_tokens")==1200&&state.at("cached_input_tokens")==1100);
    auto mismatch=normalize(legacy,100000);current["connections"]["a"]["purposes"]["activity"]["input_tokens"]=1999;CHECK(!recoverLegacyCacheSplit(mismatch,baseline,current));CHECK(mismatch.at("input_tokens")==1200);
}
TEST_CASE(reservations_count_calls_without_fabricating_tokens){
    Policy policy;policy.calls=2;auto state=fresh(100000);CHECK(canReserve(policy,state,2));CHECK(!canReserve(policy,state,0));
    state=reserve(state,1);CHECK(canReserve(policy,state,1));CHECK(!canReserve(policy,state,2));
    state=observe(state,{{"input_tokens",nullptr}});CHECK(state.at("input_tokens")==0&&state.at("reported_calls")==0);
    auto view=status(policy,state,100000);CHECK(view.at("unknown_usage_calls")==1&&view.at("reported_calls")==0);
    state=reserve(state,1);CHECK(!canReserve(policy,state,1));CHECK(reason(policy,state)=="daily_calls");
    policy.enabled=false;CHECK(canReserve(policy,state,100));state=reserve(state,3);CHECK(state.at("calls")==5);
    policy.enabled=true;CHECK(!canReserve(policy,state,1));CHECK(status(policy,state,100000).at("remaining_calls")==0);
}
TEST_CASE(observed_input_cap_applies_to_the_next_request){
    Policy policy;policy.inputTokens=1000;auto state=reserve(fresh(100000),1);CHECK(canReserve(policy,state,1));
    state=observe(state,{{"input_tokens",1200}});CHECK(state.at("input_tokens")==1200&&state.at("reported_calls")==1);
    CHECK(!canReserve(policy,state,1));CHECK(reason(policy,state)=="daily_input_tokens");
    const auto old=state;state=observe(state,{{"input_tokens",-1}});state=observe(state,{{"input_tokens",1.5}});CHECK(state==old);
    CHECK(sum(std::numeric_limits<std::uint64_t>::max(),1)==std::numeric_limits<std::uint64_t>::max());
}
TEST_CASE(utc8_midnight_rolls_once_and_restart_or_rollback_does_not_refill){
    const std::int64_t midnight=dayMs-offsetMs;Policy policy;policy.calls=1;
    auto state=reserve(fresh(midnight-1),1);CHECK(status(policy,state,midnight-1).at("day")=="1970-01-01");
    state=normalize(Json::parse(state.dump()),midnight-1);CHECK(!canReserve(policy,state,1));
    state=normalize(state,midnight);CHECK(canReserve(policy,state,1));CHECK(status(policy,state,midnight).at("day")=="1970-01-02");
    state=reserve(state,1);state=normalize(state,midnight-5000);CHECK(!canReserve(policy,state,1));CHECK(status(policy,state,midnight-5000).at("clock_reversed")==true);
    state=normalize(Json::parse(state.dump()),midnight+5000);CHECK(!canReserve(policy,state,1));
    state=normalize(state,midnight+dayMs);CHECK(canReserve(policy,state,1));
    CHECK(dayIndex(std::numeric_limits<std::int64_t>::max())>0);
}
TEST_CASE(only_explicit_background_purposes_receive_observed_usage){
    for(const auto* name:{"solitude","outer_event","task_complete","story_end","activity","activity_review","reaction","reaction_review","background_review"})CHECK(purpose(name));
    for(const auto* name:{"dialogue","review","qq_dialogue","qq_review","unknown"})CHECK(!purpose(name));
}
TEST_CASE(background_generation_observes_actor_usage_before_reserving_reviewer){
    httplib::Server server;std::atomic<int> requests=0;
    server.Post("/v1/chat/completions",[&](const auto& request,auto& response){++requests;const auto body=Json::parse(request.body);const Json content=body.at("model")=="activity"?Json{{"version",1},{"commands",Json::array()}}:Json{{"version",1},{"reactions",Json::array()}};response.set_content(Json{{"choices",Json::array({{{"message",{{"role","assistant"},{"content",content.dump()}}},{"finish_reason","stop"}}})},{"usage",{{"prompt_tokens",1200},{"completion_tokens",10}}}}.dump(),"application/json");});
    const auto port=server.bind_to_any_port("127.0.0.1");std::thread listener([&]{server.listen_after_bind();});server.wait_until_ready();
    struct Guard{httplib::Server& server;std::thread& thread;~Guard(){server.stop();thread.join();}}guard{server,listener};
    hibernation::Profile actor;actor.id="actor";actor.model.protocol=ayt::inference::Protocol::ChatCompletions;actor.model.origin="http://127.0.0.1:"+std::to_string(port);actor.model.basePath="/v1";actor.model.supportsStreaming=false;actor.model.useSystemProxy=false;actor.model.timeout=std::chrono::seconds(3);auto reviewer=actor;reviewer.id="reviewer";reviewer.model.model="reviewer";
    const auto collect=[](auto& generator){std::optional<hibernation::ReactionBatch> batch;for(int i=0;i<500&&!batch;++i){batch=generator.poll();if(!batch)std::this_thread::sleep_for(std::chrono::milliseconds(5));}return batch;};
    for(const bool activity:{true,false}){
        Policy policy;policy.inputTokens=1000;auto state=fresh(100000);int attempts=0,observations=0;bool validObservation=true;std::function<void()> cancelAfterObservation;std::atomic<bool> reached=false;const auto before=requests.load();
        const auto reserveCall=[&]{++attempts;if(!canReserve(policy,state,1))return false;state=reserve(state,1);return true;};
        const auto observeUsage=[&](const hibernation::Profile& profile,const std::string& role,const ayt::inference::Usage& usage){validObservation=validObservation&&profile.id=="actor"&&role=="actor";++observations;state=observe(state,hibernation::cacheUsageJson(usage));if(cancelAfterObservation)cancelAfterObservation();reached.store(true);};
        actor.model.model=activity?"activity":"reaction";const Json persona={{"displayName","fixture"}};std::optional<hibernation::ReactionBatch> batch;
        if(activity){hibernation::ActivityGenerator generator;generator.start(actor,reviewer,persona,Json::object(),"budget",reserveCall,{},observeUsage);batch=collect(generator);}
        else{hibernation::ReactionGenerator generator;generator.start(actor,reviewer,persona,Json::object(),Json::array(),"budget",reserveCall,{},observeUsage);batch=collect(generator);}
        CHECK(batch&&batch->status=="model_call_budget");CHECK(requests.load()==before+1);CHECK(attempts==2&&observations==1&&validObservation);CHECK(state.at("calls")==1&&state.at("input_tokens")==1200&&state.at("reported_calls")==1);CHECK(batch->usage.at("background_budget_observed")==true&&batch->usage.at("model_cache_observed")==true);
        policy.inputTokens=500000;state=fresh(100000);attempts=0;observations=0;reached=false;const auto beforeCancel=requests.load();
        const auto awaitObserver=[&]{for(int i=0;i<500&&!reached.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));};
        if(activity){hibernation::ActivityGenerator generator;cancelAfterObservation=[&]{generator.cancel();};generator.start(actor,reviewer,persona,Json::object(),"cancel",reserveCall,{},observeUsage);awaitObserver();generator.stop();CHECK(!generator.poll());}
        else{hibernation::ReactionGenerator generator;cancelAfterObservation=[&]{generator.cancel();};generator.start(actor,reviewer,persona,Json::object(),Json::array(),"cancel",reserveCall,{},observeUsage);awaitObserver();generator.stop();CHECK(!generator.poll());}
        CHECK(reached.load()&&requests.load()==beforeCancel+1&&attempts==1&&observations==1);CHECK(state.at("calls")==1&&state.at("input_tokens")==1200&&state.at("reported_calls")==1);
    }
}
TEST_SUITE_END
