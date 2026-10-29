#include <Hibernation/RememberedSchedule.h>
#include <iostream>
#include <stdexcept>
using namespace hibernation;
using namespace hibernation::reminder;
static int checks=0;
static void check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;}
template<class F>static void rejects(F f,const char* label){try{f();}catch(...){++checks;return;}throw std::runtime_error(label);}
int main(){try{
 const auto now=timestamp("2026-10-29T10:00:00+08:00");
 check(now==timestamp("2026-10-29T02:00:00Z"),"explicit timezone");
 rejects([]{timestamp("2026-02-30T10:00:00Z");},"invalid date");
 rejects([]{clockMinute("24:00");},"invalid clock");
 check(lifetime("short").retain<lifetime("medium").retain&&lifetime("medium").retain<lifetime("long").retain,"ordered lifetimes");
 rejects([]{lifetime("forever");},"no unbounded memory");
 auto when=normalizeWhen({{"type","at"},{"after_minutes",60}},now);
 auto row=fresh("tea","自己整理茶杯","short",when,Json::array(),now);
 check(row.at("next_ms")==now+hour&&phase(row,now)=="clear","initial due");
 check(chance(row,now,Json::object())==1,"precise promise is reliable");
 check(phase(row,now+5*hour)=="fading"&&phase(row,now+12*hour)=="expired","fade and expiry separate");
 check(chance(row,now+12*hour,Json::object())==0,"expiry wins precise promise");
 auto future=fresh("future","约定","short",normalizeWhen({{"type","at"},{"after_minutes",2880}},now),Json::array(),now);
 check(future.at("expires_ms").get<std::int64_t>()>future.at("next_ms").get<std::int64_t>(),"explicit future survives until due grace");
 auto interval=fresh("cycle","看看花","medium",normalizeWhen({{"type","interval"},{"every_minutes",30}},now),Json::array(),now);
 advance(interval,now+4*hour,0);check(interval.at("next_ms")==now+4*hour+30*minute,"missed periods never catch up in a burst");
 auto t=local(now);t.tm_hour=23;t.tm_min=30;t.tm_sec=0;const auto night=localMs(t);
 auto w=normalizeWhen({{"type","window"},{"start","23:00"},{"end","01:00"}},night);
 const auto a=window(w,night),b=window(w,night+hour);
 check(a.key==b.key&&a.start<night&&a.end>night+hour,"overnight opportunity stable across midnight");
 auto wr=fresh("window","窗边看看","medium",w,Json::array(),night);advance(wr,night,a.end);
 check(wr.at("next_ms").get<std::int64_t>()>a.end,"window advances once beyond its end");
 auto weekly=normalizeWhen({{"type","window"},{"period","weekly"},{"weekday",1},{"start","10:00"},{"end","11:00"}},now);
 check(local(window(weekly,now).start).tm_wday==1,"weekly weekday");
 rejects([&]{normalizeWhen({{"type","at"},{"at","2026-10-29T12:00:00+08:00"},{"after_minutes",3}},now);},"ambiguous timing");
 rejects([&]{normalizeWhen({{"type","window"},{"start","12:00"},{"end","12:00"}},now);},"empty window");
 rejects([&]{normalizeWhen({{"type","event"},{"event","idle"},{"condition","rain"}},now);},"weather constraints");
 rejects([&]{normalizeWhen({{"type","event"},{"event","idle"},{"probability",1}},now);},"model cannot set chance");
 auto event=normalizeWhen({{"type","event"},{"event","user_message"},{"match","纸船"}},now);
 auto ev=fresh("boat","聊纸船","trigger",event,Json::array(),now);
 check(once(ev)&&eventMatches(event,"user_message","我想折纸船",Json::object())&&!eventMatches(event,"user_message","你好",Json::object()),"literal cue and one-shot trigger");
 check(chance(ev,now,Json::object())==1&&chance(ev,now+60*day,Json::object())<1,"fresh exact cue reliable; fading cue may be forgotten");
 auto soft=fresh("soft","看天气","medium",normalizeWhen({{"type","event"},{"event","weather_changed"},{"condition","rain"}},now),Json::array(),now);
 check(eventMatches(soft.at("when"),"weather_changed","",{{"weather_code",61}})&&!eventMatches(soft.at("when"),"weather_changed","",{{"weather_code",0}}),"weather cue");
 check(chance(soft,now,{{"energy",1},{"willingness",1}})>chance(soft,now,{{"energy",0},{"willingness",0}}),"soft recall responds to continuous state");
 auto v=view(soft,7,now);check(!v.contains("references")&&!v.contains("pending")&&v.at("revision")==7,"public view hides source internals and random draw");
 check(safeId("quiet.tea-1")&&!safeId("../outside"),"bounded identifiers");
 std::cout<<"PASS "<<checks<<" remembered schedule policy checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
