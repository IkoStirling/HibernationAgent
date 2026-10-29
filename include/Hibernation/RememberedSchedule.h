#pragma once
#include <Hibernation/Application.h>
#include <Hibernation/TextPlayback.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
namespace hibernation::reminder {
constexpr std::int64_t minute=60000,hour=60*minute,day=24*hour;
struct Lifetime {std::int64_t retain,fade;};
inline Lifetime lifetime(std::string_view memory){
 if(memory=="short")return {12*hour,4*hour};if(memory=="medium")return {7*day,2*day};
 if(memory=="long")return {60*day,21*day};if(memory=="trigger")return {90*day,30*day};
 throw std::runtime_error("schedule_memory");
}
inline bool safeId(const std::string& id){return std::regex_match(id,std::regex("[A-Za-z0-9][A-Za-z0-9_.-]{0,63}"));}
inline std::tm local(std::int64_t ms){const std::time_t t=ms/1000;std::tm out{};
#ifdef _WIN32
 localtime_s(&out,&t);
#else
 localtime_r(&t,&out);
#endif
 return out;
}
inline std::tm utc(std::int64_t ms){const std::time_t t=ms/1000;std::tm out{};
#ifdef _WIN32
 gmtime_s(&out,&t);
#else
 gmtime_r(&t,&out);
#endif
 return out;
}
inline std::int64_t localMs(std::tm value){value.tm_isdst=-1;const auto t=std::mktime(&value);if(t<0)throw std::runtime_error("schedule_time");return static_cast<std::int64_t>(t)*1000;}
inline std::string dateKey(std::int64_t value){const auto t=local(value);std::ostringstream out;out<<std::put_time(&t,"%Y%m%d");return out.str();}
inline int clockMinute(const std::string& value){if(!std::regex_match(value,std::regex("[0-2][0-9]:[0-5][0-9]")))throw std::runtime_error("schedule_clock");const int h=std::stoi(value.substr(0,2));if(h>23)throw std::runtime_error("schedule_clock");return h*60+std::stoi(value.substr(3));}
inline std::int64_t timestamp(const std::string& value){
 static const std::regex pattern("([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2})(?::([0-9]{2}))?(Z|[+-][0-9]{2}:?[0-9]{2})?");std::smatch m;if(!std::regex_match(value,m,pattern))throw std::runtime_error("schedule_time_format");
 std::tm t{};t.tm_year=std::stoi(m[1])-1900;t.tm_mon=std::stoi(m[2])-1;t.tm_mday=std::stoi(m[3]);t.tm_hour=std::stoi(m[4]);t.tm_min=std::stoi(m[5]);t.tm_sec=m[6].matched?std::stoi(m[6]):0;if(t.tm_year<120||t.tm_year>200||t.tm_mon<0||t.tm_mon>11||t.tm_mday<1||t.tm_mday>31||t.tm_hour>23||t.tm_min>59||t.tm_sec>59)throw std::runtime_error("schedule_time");
 std::int64_t result=0;const bool explicitZone=m[7].matched;
 if(!explicitZone)result=localMs(t);else {
#ifdef _WIN32
  result=static_cast<std::int64_t>(_mkgmtime64(&t))*1000;
#else
  result=static_cast<std::int64_t>(timegm(&t))*1000;
#endif
 }
 const auto checked=explicitZone?utc(result):local(result);if(checked.tm_year!=std::stoi(m[1])-1900||checked.tm_mon!=std::stoi(m[2])-1||checked.tm_mday!=std::stoi(m[3])||checked.tm_hour!=std::stoi(m[4])||checked.tm_min!=std::stoi(m[5]))throw std::runtime_error("schedule_time");
 if(explicitZone&&m[7]!="Z"){const auto zone=m[7].str();const int h=std::stoi(zone.substr(1,2)),mins=std::stoi(zone.substr(zone.size()-2));if(h>14||mins>59||(h==14&&mins))throw std::runtime_error("schedule_timezone");result-=(zone[0]=='+'?1:-1)*(h*60+mins)*minute;}
 return result;
}
inline Json whenSchema(){return {{"type","object"},{"additionalProperties",false},{"required",Json::array({"type"})},{"properties",{
 {"type",{{"type","string"},{"enum",Json::array({"at","interval","window","event"})}}},
 {"at",{{"type","string"},{"maxLength",32}}},{"after_minutes",{{"type","integer"},{"minimum",1},{"maximum",129600}}},
 {"every_minutes",{{"type","integer"},{"minimum",5},{"maximum",10080}}},
 {"period",{{"type","string"},{"enum",Json::array({"daily","weekly"})}}},{"start",{{"type","string"},{"maxLength",5}}},{"end",{{"type","string"},{"maxLength",5}}},{"weekday",{{"type","integer"},{"minimum",1},{"maximum",7}}},
 {"event",{{"type","string"},{"enum",Json::array({"user_message","idle","weather_changed","video_finished","task_completed","item_consumed"})}}},
 {"match",{{"type","string"},{"maxLength",160}}},{"condition",{{"type","string"},{"enum",Json::array({"any","rain","snow","clear"})}}}
 }}};}
inline Json normalizeWhen(const Json& input,std::int64_t now){
 const auto type=input.at("type").get<std::string>();Json out={{"type",type}};
 const auto only=[&](std::initializer_list<std::string_view> fields){for(auto it=input.begin();it!=input.end();++it){if(it.key()=="type")continue;if(std::none_of(fields.begin(),fields.end(),[&](auto field){return field==it.key();}))throw std::runtime_error("schedule_when_fields");}};
 if(type=="at"){only({"at","after_minutes"});if(input.contains("at")==input.contains("after_minutes"))throw std::runtime_error("schedule_time_required");out["first_ms"]=input.contains("at")?timestamp(input.at("at")):now+input.at("after_minutes").get<std::int64_t>()*minute;if(out.at("first_ms").get<std::int64_t>()<now+1000||out.at("first_ms").get<std::int64_t>()>now+90*day)throw std::runtime_error("schedule_time_range");}
 else if(type=="interval"){only({"every_minutes","after_minutes"});if(!input.contains("every_minutes"))throw std::runtime_error("schedule_interval_required");out["interval_ms"]=input.at("every_minutes").get<std::int64_t>()*minute;out["first_ms"]=now+input.value("after_minutes",input.at("every_minutes").get<std::int64_t>())*minute;}
 else if(type=="window"){only({"period","start","end","weekday"});out["period"]=input.value("period",std::string("daily"));if(!input.contains("start")||!input.contains("end"))throw std::runtime_error("schedule_window_required");out["start_minute"]=clockMinute(input.at("start"));out["end_minute"]=clockMinute(input.at("end"));if(out.at("start_minute")==out.at("end_minute"))throw std::runtime_error("schedule_empty_window");if(out.at("period")=="weekly"){const auto t=local(now);out["weekday"]=input.value("weekday",t.tm_wday==0?7:t.tm_wday);}else if(input.contains("weekday"))throw std::runtime_error("schedule_weekday_requires_weekly");}
 else if(type=="event"){only({"event","match","condition"});if(!input.contains("event"))throw std::runtime_error("schedule_event_required");out["event"]=input.at("event");out["match"]=input.value("match",std::string{});out["condition"]=input.value("condition",std::string("any"));if(out.at("condition")!="any"&&out.at("event")!="weather_changed")throw std::runtime_error("schedule_weather_condition");}
 else throw std::runtime_error("schedule_when_type");return out;
}
struct Opportunity {std::string key;std::int64_t start=0,end=0;};
inline Opportunity window(const Json& when,std::int64_t now){
 const auto start=when.at("start_minute").get<int>(),end=when.at("end_minute").get<int>();auto today=local(now);today.tm_hour=0;today.tm_min=0;today.tm_sec=0;
 for(int offset=-1;offset<9;++offset){auto date=today;date.tm_mday+=offset;const auto midnight=localMs(date);const auto normalized=local(midnight);if(when.at("period")=="weekly"&&(normalized.tm_wday==0?7:normalized.tm_wday)!=when.at("weekday"))continue;auto a=normalized;a.tm_hour=start/60;a.tm_min=start%60;auto b=normalized;b.tm_mday+=end<=start?1:0;b.tm_hour=end/60;b.tm_min=end%60;const auto finish=localMs(b);if(finish<=now)continue;return {"window:"+dateKey(midnight),localMs(a),finish};}
 throw std::runtime_error("schedule_window_unreachable");
}
inline bool fixed(const Json& when){return when.at("type")=="at"||when.at("type")=="interval";}
inline bool once(const Json& row){return row.at("when").at("type")=="at"||row.at("memory")=="trigger";}
inline std::int64_t grace(const Json& when){if(when.at("type")=="interval")return std::min<std::int64_t>(10*minute,when.at("interval_ms").get<std::int64_t>()/2);return when.at("type")=="at"?15*minute:10*minute;}
inline Opportunity due(const Json& row){const auto n=row.at("next_ms").get<std::int64_t>();return {"time:"+std::to_string(n),n,n+grace(row.at("when"))};}
inline void advance(Json& row,std::int64_t now,std::int64_t windowEnd){
 const auto& when=row.at("when");if(when.at("type")=="window")row["next_ms"]=window(when,std::max(now,windowEnd)+1).start;
 else if(when.at("type")=="interval"){const auto first=when.at("first_ms").get<std::int64_t>(),period=when.at("interval_ms").get<std::int64_t>();row["next_ms"]=first+(std::max<std::int64_t>(0,(now-first)/period)+1)*period;}
 else if(when.at("type")=="event")row["next_ms"]=0;
}
inline Json fresh(const std::string& id,const std::string& topic,const std::string& memory,Json when,Json refs,std::int64_t now){
 const auto life=lifetime(memory);const auto first=when.at("type")=="window"?window(when,now).start:when.value("first_ms",std::int64_t(0));const auto deadline=std::max(now+life.retain,first?first+grace(when):0LL);
 return {{"version",1},{"id",id},{"topic",topic},{"memory",memory},{"when",when},{"references",refs},{"created_ms",now},{"refreshed_ms",now},{"fade_ms",now+life.fade},{"expires_ms",deadline},{"next_ms",first},{"status","active"},{"cooldown_ms",when.at("type")=="event"?(memory=="short"?30*minute:memory=="long"?6*hour:hour):0},{"eligible_ms",0},{"pending",nullptr},{"recent_events",Json::array()},{"wake_count",0},{"last_decision",nullptr},{"updated_ms",now}};
}
inline std::string phase(const Json& row,std::int64_t now){if(now>=row.at("expires_ms").get<std::int64_t>())return "expired";return now>=row.at("fade_ms").get<std::int64_t>()?"fading":"clear";}
inline double chance(const Json& row,std::int64_t now,const Json& state){
 if(phase(row,now)=="expired")return 0;if(fixed(row.at("when")))return 1;if(row.at("when").at("type")=="event"&&row.at("when").at("event")=="user_message"&&!row.at("when").value("match",std::string{}).empty()&&phase(row,now)=="clear")return 1;
 const auto fade=row.at("fade_ms").get<std::int64_t>(),end=row.at("expires_ms").get<std::int64_t>();const double strength=now<=fade?1:std::clamp(1.0-double(now-fade)/std::max<std::int64_t>(1,end-fade),0.1,1.0);
 const auto scalar=[&](const char* key){const auto it=state.find(key);return it!=state.end()&&it->is_number()?std::clamp(it->get<double>(),0.0,1.0):0.5;};
 const double body=(0.6+0.4*scalar("energy"))*(0.6+0.4*scalar("willingness"));return std::clamp((row.at("when").at("type")=="window"?0.8:0.9)*strength*body,0.0,1.0);
}
inline bool eventMatches(const Json& when,const std::string& kind,const std::string& text,const Json& data){
 if(when.at("type")!="event"||when.at("event")!=kind)return false;const auto match=when.value("match",std::string{});if(!match.empty()&&text.find(match)==std::string::npos)return false;
 const auto condition=when.value("condition",std::string("any"));if(condition=="any")return true;const int code=data.value("weather_code",-1);
 if(condition=="clear")return code==0||code==1;if(condition=="snow")return code==71||code==73||code==75||code==77||code==85||code==86;
 return condition=="rain"&&((code>=51&&code<=67)||(code>=80&&code<=82)||code==95||code==96||code==99);
}
inline Json view(const Json& value,std::int64_t revision,std::int64_t now){
 Json out;for(const auto* key:{"id","topic","memory","when","created_ms","fade_ms","expires_ms","next_ms","status","wake_count","last_decision"})out[key]=value.at(key);out["revision"]=revision;out["memory_phase"]=phase(value,now);out["program_inferred_parameters"]=true;out["wake_requires_speech"]=false;return out;
}
inline std::string conduct(){return "定时计划可由你自己创建和修改：只给目的、时间/事件意图以及memory短short/中medium/长long/触发前保留trigger，具体期限、淡化、概率和冷却由程序推断，以真实回执为准。at是明确时刻或延迟，interval是固定周期；window是每日/每周的时间区间，event只使用公布的事件类型，可给准确关键词match。trigger是等到一次成功唤醒后结束，仍有兜底期限。程序不会因你查看计划而刷新记忆，也不删除原档案来模拟遗忘。scheduled_intent表示这次被自己的计划唤醒，先核对当前情形与实际经历；可以行动、调整或安静，不必写日记、汇报或主动叫用户。到期唤醒不表示计划内容已经完成，也不表示用户刚发了新指令。";}
}
