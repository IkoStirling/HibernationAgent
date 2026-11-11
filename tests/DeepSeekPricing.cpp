#include <Hibernation/DeepSeekPricing.h>
#include <Hibernation/PluginRuntime.h>
#include <AYTestFixtures.h>
#include <httplib.h>
#include <thread>
using namespace hibernation;
using namespace hibernation::pricing;
namespace {
std::int64_t timeAt(int year,int month,int day,int hour,int minute=0){using namespace std::chrono;return duration_cast<milliseconds>((sys_days{std::chrono::year{year}/std::chrono::month{static_cast<unsigned>(month)}/std::chrono::day{static_cast<unsigned>(day)}}+hours{hour}+minutes{minute}).time_since_epoch()).count();}
std::string fixture(){return R"(<html><table><tr><th>模型</th><th>deepseek-flash</th><th>deepseek-v4-pro</th></tr>
<tr><td>百万tokens输入（缓存命中）空闲时段</td><td>0.02元</td><td>0.15元</td></tr><tr><td>高峰时段</td><td>0.04元</td><td>0.30元</td></tr>
<tr><td>百万tokens输入（缓存未命中）空闲时段</td><td>1元</td><td>4.5元</td></tr><tr><td>高峰时段</td><td>2元</td><td>9元</td></tr>
<tr><td>百万tokens输出 空闲时段</td><td>4元</td><td>13.5元</td></tr><tr><td>高峰时段</td><td>8元</td><td>27元</td></tr></table>
<p>旧模型名 deepseek-v4-flash、deepseek-v4-flash-vision-exp。</p>
<p>北京时间周一至周五（不含中国法定节假日）9:00 - 12:00、14:00 - 18:00 为高峰时段；其余时段，包括周末及中国法定节假日全天均为空闲时段。</p></html>)";}
Json attachment(std::int64_t now){return {{"enabled",true},{"settings",{{"mode","auto"}}},{"snapshot",{{"source_valid",true},{"data",parseOfficialHtml(fixture(),now)}}}};}
}
TEST_SUITE(DeepSeekPricing)
TEST_CASE(official_table_is_parsed_with_currency_units_and_aliases){
    const auto data=parseOfficialHtml(fixture(),1);CHECK(data.at("models").at("deepseek-flash").at("off_peak").at("input_cache_hit")==0.02);CHECK(data.at("models").at("deepseek-v4-pro").at("peak").at("output")==27);CHECK(data.at("peak_windows")==Json::array({Json::array({540,720}),Json::array({840,1080})}));CHECK(data.at("aliases").at("deepseek-v4-flash")=="deepseek-flash");CHECK(data.at("currency")=="CNY");
    for(const auto& invalid:std::vector<std::string>{"{}",std::regex_replace(fixture(),std::regex("14:00 - 18:00"),"18:00 - 14:00"),std::regex_replace(fixture(),std::regex("缓存未命中"),"changed"),std::regex_replace(fixture(),std::regex("周一至周五"),"changed")}){bool rejected=false;try{parseOfficialHtml(invalid,1);}catch(...){rejected=true;}CHECK(rejected);}
}
TEST_CASE(time_boundaries_use_beijing_calendar_without_host_timezone){
    const auto data=parseOfficialHtml(fixture(),0);
    CHECK(classify(data,timeAt(2026,10,27,0,59))=="off_peak");CHECK(classify(data,timeAt(2026,10,27,1))=="peak");CHECK(classify(data,timeAt(2026,10,27,4))=="off_peak");CHECK(classify(data,timeAt(2026,10,27,6))=="peak");CHECK(classify(data,timeAt(2026,10,27,10))=="off_peak");
    CHECK(classify(data,timeAt(2026,10,31,1))=="off_peak");CHECK(classify(data,timeAt(2026,10,10,1))=="off_peak");CHECK(classify(data,timeAt(2026,10,1,1))=="off_peak");CHECK(classify(data,timeAt(2026,2,16,1))=="off_peak");CHECK(classify(data,timeAt(2027,1,4,1))=="unknown_calendar");CHECK(classify(data,timeAt(2027,1,4,4))=="off_peak");
}
TEST_CASE(next_transition_cache_expiry_and_unknown_models_are_explicit){
    const auto now=timeAt(2026,10,27,1);auto a=attachment(now);auto view=status(a.at("snapshot"),"deepseek-flash",now);CHECK(view.at("next_change_ms")==timeAt(2026,10,27,4));CHECK(view.at("price").at("output")==8);
    CHECK(status(a.at("snapshot"),"unknown",now).at("price").is_null());CHECK(status(a.at("snapshot"),"deepseek-flash",now+49LL*3600000).at("period")=="unknown");CHECK(status(a.at("snapshot"),"deepseek-flash",now-1).at("period")=="unknown");
    a["snapshot"]["source_valid"]=false;CHECK(status(a.at("snapshot"),"deepseek-flash",now).at("period")=="unknown");
}
TEST_CASE(peak_overlays_restore_exact_user_baseline_and_preserve_strict_review){
    const auto peak=timeAt(2026,10,27,1),valley=timeAt(2026,10,27,4);auto a=attachment(peak);const Json baseline={{"preset","balanced"},{"historyBytes",11000},{"reviewReadOnlyTools",true},{"reviewSilence",true}};
    const auto reduced=apply(a,"https://api.deepseek.com","deepseek-flash",baseline,8,2,peak);CHECK(reduced.maxToolRounds==3);CHECK(reduced.maxRewrites==1);CHECK(reduced.deferPrefetch);CHECK(reduced.context.at("reviewReadOnlyTools").get<bool>());CHECK(reduced.context.at("reviewSilence").get<bool>());
    const auto restored=apply(a,"https://api.deepseek.com","deepseek-flash",baseline,8,2,valley);CHECK(restored.context==baseline);CHECK(restored.maxToolRounds==8);CHECK(restored.maxRewrites==2);CHECK(!restored.deferPrefetch);
    a["settings"]["mode"]="force-normal";CHECK(apply(a,"https://api.deepseek.com","deepseek-flash",baseline,8,2,peak).context==baseline);
    a["settings"]["mode"]="force-economy";CHECK(apply(a,"https://api.deepseek.com","deepseek-flash",baseline,8,2,valley).pricing.at("applied").get<bool>());
    CHECK(apply(a,"https://other.example","deepseek-flash",baseline,8,2,peak).context==baseline);a["enabled"]=false;CHECK(apply(a,"https://api.deepseek.com","deepseek-flash",baseline,8,2,peak).context==baseline);
}
TEST_CASE(economy_does_not_silently_reduce_explicit_protocol_recovery){
    const auto peak=timeAt(2026,10,27,1);auto a=attachment(peak);a["settings"]["mode"]="force-economy";
    const auto effective=apply(a,"https://api.deepseek.com","deepseek-flash",{{"protocolRepairs",2},{"reviewPolicy","layered"},{"reviewAttempts",2}},8,2,peak);
    CHECK(effective.context.at("protocolRepairs")==2);CHECK(effective.context.at("reviewAttempts")==2);CHECK(effective.maxToolRounds==3);
}
#ifdef HA_TEST_PLUGIN_ROOT
TEST_CASE(actual_dll_refreshes_asynchronously_and_manual_mode_does_not_need_model_calls){
    httplib::Server server;std::atomic<int> requests=0;server.Get("/zh-cn/quick_start/pricing/",[&](const auto&,auto& response){++requests;response.set_content(fixture(),"text/html");});const auto port=server.bind_to_any_port("127.0.0.1");std::thread listener([&]{server.listen_after_bind();});server.wait_until_ready();
    struct Guard{httplib::Server& server;std::thread& thread;~Guard(){server.stop();thread.join();}}guard{server,listener};
    PluginRuntime runtime(HA_TEST_PLUGIN_ROOT,{});std::string issue;Json cfg={{"deepseek-pricing",{{"enabled",true},{"mode","auto"},{"fixtureOrigin","http://127.0.0.1:"+std::to_string(port)}}}};CHECK(runtime.configure(cfg,issue));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<deadline&&!runtime.liveSnapshots().at("deepseek-pricing").at("source_valid").get<bool>()){runtime.tick(.01);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    const auto state=runtime.liveSnapshots().at("deepseek-pricing");CHECK(state.at("source_valid").get<bool>());CHECK(state.at("fixture").get<bool>());CHECK(requests==1);CHECK(runtime.catalog().empty());
    cfg["deepseek-pricing"]["mode"]="force-normal";CHECK(runtime.configure(cfg,issue));CHECK(runtime.liveSnapshots().at("deepseek-pricing").at("data")==state.at("data"));
    auto invalid=cfg;invalid["deepseek-pricing"]["maxAgeHours"]=-1;CHECK(!runtime.configure(invalid,issue));CHECK(runtime.enabled("deepseek-pricing"));runtime.stop();
}
#endif
TEST_SUITE_END
