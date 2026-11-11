#pragma once
#include <Hibernation/ContextControl.h>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <regex>
namespace hibernation::pricing {
using Json=nlohmann::json;
inline constexpr auto sourceUrl="https://api-docs.deepseek.com/zh-cn/quick_start/pricing/";
inline constexpr auto calendarUrl="https://www.beijing.gov.cn/cs/gncs/zcwj/202603/t20260327_4568275.html";
inline std::string plainHtml(const std::string& html){
    auto text=std::regex_replace(html,std::regex("<[^>]*>")," ");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{{"&nbsp;"," "},{"&#160;"," "},{"&amp;","&"}}){std::size_t at=0;while((at=text.find(from,at))!=text.npos){text.replace(at,from.size(),to);at+=to.size();}}
    return std::regex_replace(text,std::regex("[\\r\\n\\t ]+")," ");
}
inline Json parseOfficialHtml(const std::string& html,std::int64_t fetched){
    if(html.size()>1024*1024)throw std::runtime_error("pricing_document_limit");
    const auto begin=html.find("<table"),end=html.find("</table>",begin);if(begin==html.npos||end==html.npos)throw std::runtime_error("pricing_format_changed");
    const auto table=html.substr(begin,end-begin+8);const std::regex rowPattern("<tr[^>]*>[\\s\\S]*?</tr>"),modelPattern("deepseek-[a-z0-9-]+"),pricePattern("([0-9]+(?:\\.[0-9]+)?)\\s*元");
    std::vector<std::string> models;std::vector<std::vector<double>> rows;
    for(auto it=std::sregex_iterator(table.begin(),table.end(),rowPattern);it!=std::sregex_iterator();++it){
        const auto row=plainHtml(it->str());if(models.empty())for(auto m=std::sregex_iterator(row.begin(),row.end(),modelPattern);m!=std::sregex_iterator();++m)models.push_back(m->str());
        std::vector<double> prices;for(auto p=std::sregex_iterator(row.begin(),row.end(),pricePattern);p!=std::sregex_iterator();++p){const auto v=std::stod((*p)[1]);if(!std::isfinite(v)||v<=0||v>10000)throw std::runtime_error("pricing_value_invalid");prices.push_back(v);}if(!prices.empty()){
            const auto index=rows.size();if(row.find(index%2?"高峰时段":"空闲时段")==row.npos||(index==0&&row.find("缓存命中")==row.npos)||(index==2&&row.find("缓存未命中")==row.npos)||(index==4&&row.find("输出")==row.npos))throw std::runtime_error("pricing_row_changed");rows.push_back(prices);
        }
    }
    if(models.empty()||models.size()>16||rows.size()!=6)throw std::runtime_error("pricing_format_changed");
    Json rates=Json::object();for(std::size_t m=0;m<models.size();++m){Json price=Json::object();for(std::size_t r=0;r<6;++r){if(rows[r].size()!=models.size())throw std::runtime_error("pricing_column_changed");price[r%2?"peak":"off_peak"][r<2?"input_cache_hit":r<4?"input_cache_miss":"output"]=rows[r][m];}rates[models[m]]=price;}
    // Parse only the explanatory paragraph, not arbitrary times elsewhere.
    Json windows=Json::array();std::string rule;const std::regex paragraphs("<p[^>]*>[\\s\\S]*?</p>");
    for(auto p=std::sregex_iterator(html.begin(),html.end(),paragraphs);p!=std::sregex_iterator();++p){const auto text=plainHtml(p->str());if(text.find("北京时间周一至周五")!=text.npos&&text.find("中国法定节假日")!=text.npos&&text.find("周末")!=text.npos){if(!rule.empty())throw std::runtime_error("pricing_rule_ambiguous");rule=text;}}
    if(rule.empty())throw std::runtime_error("pricing_rule_changed");
    const std::regex timePattern("([0-2]?[0-9]):([0-5][0-9])\\s*[-–—]\\s*([0-2]?[0-9]):([0-5][0-9])");int last=-1;
    for(auto t=std::sregex_iterator(rule.begin(),rule.end(),timePattern);t!=std::sregex_iterator();++t){const auto start=std::stoi((*t)[1])*60+std::stoi((*t)[2]),stop=std::stoi((*t)[3])*60+std::stoi((*t)[4]);if(start<0||start>=stop||stop>1440||start<last)throw std::runtime_error("pricing_window_invalid");windows.push_back({start,stop});last=stop;}
    if(windows.empty()||windows.size()>8)throw std::runtime_error("pricing_rule_changed");
    Json aliases=Json::object();if(rates.contains("deepseek-flash")&&html.find("旧模型名")!=html.npos){for(const auto* alias:{"deepseek-v4-flash","deepseek-v4-flash-vision-exp"})if(html.find(alias)!=html.npos)aliases[alias]="deepseek-flash";}
    return {{"version",1},{"source",sourceUrl},{"currency","CNY"},{"unit","per_million_tokens"},{"fetched_ms",fetched},{"models",rates},{"aliases",aliases},{"utc_offset_minutes",480},{"peak_windows",windows},{"rule_text",rule},{"calendar_source",calendarUrl},{"calendar_year",2026}};
}
inline std::string beijingDate(std::int64_t ms){
    using namespace std::chrono;const year_month_day date{floor<days>(sys_time<milliseconds>{milliseconds{ms}}+hours{8})};char out[16];std::snprintf(out,sizeof(out),"%04d-%02u-%02u",int(date.year()),unsigned(date.month()),unsigned(date.day()));return out;
}
inline std::string classify(const Json& data,std::int64_t ms){
    using namespace std::chrono;const auto local=sys_time<milliseconds>{milliseconds{ms}}+hours{8};const auto day=floor<days>(local);const auto weekday=std::chrono::weekday{day}.c_encoding();
    if(weekday==0||weekday==6)return "off_peak";
    const auto minute=duration_cast<minutes>(local-day).count();bool inWindow=false;for(const auto& window:data.at("peak_windows"))if(minute>=window.at(0).get<int>()&&minute<window.at(1).get<int>())inWindow=true;if(!inWindow)return "off_peak";
    const auto date=beijingDate(ms);if(!date.starts_with("2026-"))return "unknown_calendar";
    // Official 2026 holiday leave ranges; makeup Saturdays/Sundays stay off-peak.
    for(const auto& [start,end]:std::vector<std::pair<std::string,std::string>>{{"2026-01-01","2026-01-03"},{"2026-02-15","2026-02-23"},{"2026-04-04","2026-04-06"},{"2026-05-01","2026-05-05"},{"2026-06-19","2026-06-21"},{"2026-09-25","2026-09-27"},{"2026-10-01","2026-10-07"}})if(date>=start&&date<=end)return "off_peak";
    return "peak";
}
inline Json status(const Json& snapshot,const std::string& model,std::int64_t now,int maxAgeHours=48){
    Json out={{"period","unknown"},{"source",sourceUrl},{"checked_ms",now},{"timezone","Asia/Shanghai"},{"date",beijingDate(now)},{"next_change_ms",nullptr},{"price",nullptr},{"last_error",snapshot.value("last_error",std::string{})}};
    try{const auto& data=snapshot.at("data");const auto fetched=data.at("fetched_ms").get<std::int64_t>();out["fetched_ms"]=fetched;out["rule_text"]=data.at("rule_text");out["calendar_source"]=calendarUrl;out["calendar_year"]=2026;
        if(!snapshot.value("source_valid",false)||data.at("source")!=sourceUrl||data.at("version")!=1||now<fetched||now-fetched>static_cast<std::int64_t>(maxAgeHours)*3600000){out["reason"]="pricing_missing_or_stale";return out;}
        const auto key=data.at("aliases").value(model,model);if(!data.at("models").contains(key)){out["reason"]="model_not_in_official_table";return out;}
        const auto period=classify(data,now);out["period"]=period;out["model"]=model;out["rates"]=data.at("models").at(key);out["currency"]="CNY";out["unit"]="per_million_tokens";
        if(period=="peak"||period=="off_peak")out["price"]=out.at("rates").at(period);else out["reason"]="holiday_calendar_year_not_available";
        using namespace std::chrono;const auto today=floor<days>(sys_time<milliseconds>{milliseconds{now}}+hours{8});std::vector<std::int64_t> candidates;
        for(int d=0;d<9;++d){const auto midnight=duration_cast<milliseconds>((today+days{d}-hours{8}).time_since_epoch()).count();candidates.push_back(midnight);for(const auto& w:data.at("peak_windows"))for(const auto& m:w)candidates.push_back(midnight+m.get<std::int64_t>()*60000);}
        std::sort(candidates.begin(),candidates.end());for(const auto next:candidates)if(next>now&&classify(data,next)!=period){out["next_change_ms"]=next;break;}
    }catch(...){out["period"]="unknown";out["price"]=nullptr;out["reason"]="pricing_missing_or_invalid";}return out;
}
struct EffectiveCalls {
    Json context=Json::object(),pricing=Json::object();std::size_t maxToolRounds=6,maxRewrites=2;bool deferPrefetch=false;
};
inline EffectiveCalls apply(const Json& attachment,const std::string& origin,const std::string& model,const Json& baseline,std::size_t rounds,std::size_t rewrites,std::int64_t now){
    EffectiveCalls out{baseline,Json::object(),rounds,rewrites,false};const auto enabled=attachment.value("enabled",false);const auto cfg=attachment.value("settings",Json::object());const auto mode=cfg.value("mode",std::string("auto"));
    out.pricing=status(attachment.value("snapshot",Json::object()),model,now,cfg.value("maxAgeHours",48));out.pricing["enabled"]=enabled;out.pricing["mode"]=mode;out.pricing["applied"]=false;
    const bool fixture=attachment.value("snapshot",Json::object()).value("fixture",false);out.pricing["fixture"]=fixture;
    const bool provider=fixture?origin.starts_with("http://127.0.0.1:"):origin=="https://api.deepseek.com";out.pricing["provider_matches"]=provider;
    const bool economy=enabled&&provider&&(mode=="force-economy"||(mode=="auto"&&out.pricing.at("period")=="peak"));
    if(economy){auto policy=ContextControl::read({{"preset","economy"}});const auto prior=ContextControl::read(baseline);policy.protocolRepairs=prior.protocolRepairs;policy.reviewPolicy=prior.reviewPolicy;policy.reviewLayout=prior.reviewLayout;policy.reviewReasoningEffort=prior.reviewReasoningEffort;policy.reviewInitialTokens=prior.reviewInitialTokens;if(prior.reviewPolicy=="layered")policy.reviewAttempts=prior.reviewAttempts;policy.reviewReadOnlyTools=baseline.value("reviewReadOnlyTools",false);policy.reviewSilence=baseline.value("reviewSilence",false);policy.historyBytes=std::min(prior.historyBytes,policy.historyBytes);policy.reviewHistoryBytes=std::min(prior.reviewHistoryBytes,policy.reviewHistoryBytes);out.context=policy.describe();out.maxToolRounds=std::min(rounds,static_cast<std::size_t>(cfg.value("peakMaxToolRounds",3)));out.maxRewrites=std::min(rewrites,static_cast<std::size_t>(cfg.value("peakMaxRewrites",1)));out.deferPrefetch=cfg.value("deferPrefetch",true);out.pricing["applied"]=true;}
    out.pricing["effective_context"]=ContextControl::read(out.context).describe();out.pricing["effective_max_tool_rounds"]=out.maxToolRounds;out.pricing["effective_max_rewrites"]=out.maxRewrites;out.pricing["defer_prefetch"]=out.deferPrefetch;return out;
}
}
