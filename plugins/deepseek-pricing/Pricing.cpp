#include "../PluginCommon.h"
#include <Hibernation/DeepSeekPricing.h>
#include <future>
#include <tuple>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class Pricing final:public BasePlugin {
    mutable std::mutex mutex_;PluginJson cfg_=PluginJson::object(),cached_=PluginJson::object();std::future<void> fetch_;std::atomic<bool> stopped_=false,paused_=false;
    std::int64_t next_=0,lastAttempt_=0;std::string error_;bool valid_=false;unsigned generation_=0;
    void fetch(){PluginJson cfg;unsigned generation;{std::lock_guard lock(mutex_);cfg=cfg_;generation=generation_;lastAttempt_=nowMs();next_=lastAttempt_+60000;}
        try{
            ayt::net::http::Request request;request.origin="https://api-docs.deepseek.com";request.path="/zh-cn/quick_start/pricing/";request.method="GET";request.useSystemProxy=cfg.value("useSystemProxy",false);request.proxyOrigin=cfg.value("proxyOrigin",std::string{});request.timeout=std::chrono::seconds(12);request.maxResponseBytes=1024*1024;request.cancelled=[this]{return stopped_.load()||paused_.load();};request.headers={{"User-Agent","HibernationAgent-Pricing/0.1"},{"Accept","text/html"}};
            // Loopback-only transport substitution for local integration tests;
            // a nonofficial source is displayed as a fixture and never persisted
            // as evidence of a real provider quote by production settings.
            if(cfg.contains("fixtureOrigin"))request.origin=cfg.at("fixtureOrigin");
            const auto response=ayt::net::http::perform(request);if(!response||response.httpStatus!=200)throw std::runtime_error("pricing_fetch_unavailable");
            PluginJson data;try{data=pricing::parseOfficialHtml(response.body,nowMs());}catch(...){std::lock_guard lock(mutex_);if(generation==generation_){valid_=false;error_="pricing_format_changed";}return;}
            std::lock_guard lock(mutex_);if(generation!=generation_||stopped_)return;cached_=data;valid_=true;error_.clear();next_=nowMs()+static_cast<std::int64_t>(cfg_.value("refreshMinutes",360))*60000;
        }catch(...){std::lock_guard lock(mutex_);if(generation==generation_)error_="pricing_fetch_unavailable";}
    }
public:
    const char* getName()const override{return "deepseek-pricing";}const char* getVersion()const override{return "0.1.0";}
    const char* getDescription()const override{return "Official DeepSeek price and time-window policy, without model calls";}
    bool bind(PluginHostContext,PluginJson cfg,std::string& issue)override{try{
        configurationTypes(cfg,{"mode","proxyOrigin","fixtureOrigin"},{"useSystemProxy","deferPrefetch"});
        const std::set<std::string> allowed={"enabled","mode","proxyOrigin","useSystemProxy","deferPrefetch","refreshMinutes","maxAgeHours","peakMaxToolRounds","peakMaxRewrites","fixtureOrigin"};for(auto it=cfg.begin();it!=cfg.end();++it)if(!allowed.contains(it.key()))throw std::runtime_error("field");
        const auto mode=cfg.value("mode",std::string("auto"));if(mode!="auto"&&mode!="off"&&mode!="force-economy"&&mode!="force-normal")throw std::runtime_error("mode");
        for(const auto& [name,fallback,lo,hi]:std::vector<std::tuple<const char*,double,double,double>>{{"refreshMinutes",360,15,1440},{"maxAgeHours",48,1,168},{"peakMaxToolRounds",3,2,6},{"peakMaxRewrites",1,0,2}}){if(cfg.contains(name)&&!cfg.at(name).is_number_integer())throw std::runtime_error("integer");number(cfg,name,fallback,lo,hi);}
        if(cfg.contains("fixtureOrigin")){const auto origin=cfg.at("fixtureOrigin").get<std::string>();if(!origin.starts_with("http://127.0.0.1:")||!ayt::net::http::validOrigin(origin))throw std::runtime_error("fixture_origin");}
        std::lock_guard lock(mutex_);if(cfg.value("proxyOrigin",std::string{})!=cfg_.value("proxyOrigin",std::string{})||cfg.value("useSystemProxy",false)!=cfg_.value("useSystemProxy",false)||cfg.value("fixtureOrigin",std::string{})!=cfg_.value("fixtureOrigin",std::string{})){++generation_;next_=0;valid_=false;cached_=PluginJson::object();}cfg_=std::move(cfg);stopped_=false;issue.clear();return true;
    }catch(...){issue="deepseek_pricing_configuration";return false;}}
    PluginJson tools()const override{return PluginJson::array();}
    PluginJson invoke(const std::string&,const PluginJson&,const std::function<bool()>&)override{return toolFailure("tool_not_available");}
    void tick(double)override{if(fetch_.valid()){if(fetch_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;fetch_.get();}bool due=false;{std::lock_guard lock(mutex_);due=!stopped_&&!paused_&&nowMs()>=next_;}if(due)fetch_=std::async(std::launch::async,[this]{fetch();});}
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);return {{"data",cached_},{"source_valid",valid_},{"last_error",error_},{"last_attempt_ms",lastAttempt_},{"next_refresh_ms",next_},{"fixture",cfg_.contains("fixtureOrigin")}};}
    bool restore(const PluginJson& saved)override{if(!saved.is_object())return false;std::lock_guard lock(mutex_);const auto data=saved.value("data",PluginJson::object());if(!saved.value("fixture",false)&&!cfg_.contains("fixtureOrigin")&&data.value("source",std::string{})==pricing::sourceUrl&&data.value("version",0)==1){cached_=data;valid_=saved.value("source_valid",false);next_=data.value("fetched_ms",std::int64_t(0))+static_cast<std::int64_t>(cfg_.value("refreshMinutes",360))*60000;}return true;}
    void event(const PluginJson& event)override{const auto kind=event.value("kind",std::string{});if(kind=="runtime.paused")paused_=event.value("paused",false);if(kind=="runtime.pricing.refresh"){std::lock_guard lock(mutex_);next_=std::max(nowMs(),lastAttempt_+60000);}}
    void shutdown()override{stopped_=true;if(fetch_.valid())fetch_.wait();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Pricing;}
