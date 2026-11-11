#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace hibernation::video {
// App policy, independent of rendering. Only host playback telemetry advances coverage.
class WatchSession {
public:
    using Json=nlohmann::json;
    Json state=Json::object(), receipts=Json::array(), notes=Json::array();
    Json playback=Json::object();
    std::string boundSource;
    std::uint64_t epoch=0;
    std::int64_t sampleAt=0,lastWake=0;
    bool input=false,paused=false,occupied=false;
    double previous=0;
    std::string previousState;

    bool exists()const{return state.is_object()&&!state.empty();}
    bool active()const{return exists()&&state.value("status","")=="watching";}
    bool guarded()const{return exists()&&state.value("status","")!="stopped";}
    void invalidate(){++epoch;if(exists()){state["generation"]=epoch;state["notice_pending"]=false;}}
    void resetCoverage(double position){state["coverage"]={{"from",position},{"to",position}};state["cursor"]=position;state["notice_pending"]=false;state["unanswered_notices"]=0;invalidate();}
    bool web()const{return state.value("source_kind",std::string("local"))=="webpage";}
    bool sameSource(const std::string& source)const{
        if(web())return source==boundSource;
        // Windows media paths are case insensitive; URL sources are never accepted here.
        auto lower=[](std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');std::replace(s.begin(),s.end(),'\\','/');return s;};
        return lower(source)==lower(boundSource);
    }
    bool playable()const{return playback.value("mode","")== (web()?"webpage":"direct")&&sameSource(playback.value("source",std::string{}))&&(!web()||playback.value("media_id",std::string{})==state.value("media_id",std::string{}))&&(previousState=="playing"||previousState=="paused"||previousState=="ended");}
    bool start(const std::string& mount,const std::string& path,const std::string& inputPath,const std::string& id,double seconds,std::int64_t now){
        const auto oldSource=boundSource;const auto oldState=state;boundSource=inputPath;state["source_kind"]="local";
        if(!playable()||now-sampleAt>3000){boundSource=oldSource;state=oldState;return false;}
        state={{"source_kind","local"},{"id",id},{"mount",mount},{"path",path},{"status","watching"},{"window_seconds",seconds},{"notice_pending",false},{"failure_count",0}};
        resetCoverage(previous);lastWake=0;return true;
    }
    bool startWeb(const std::string& id,double seconds,std::int64_t now){
        const auto source=playback.value("source",std::string{}),media=playback.value("media_id",std::string{});
        if(playback.value("mode",std::string{})!="webpage"||(!source.starts_with("https://")&&!source.starts_with("http://"))||source.size()>768||media.empty()||now-sampleAt>3000||(previousState!="playing"&&previousState!="paused"&&previousState!="ended"))return false;
        boundSource=source;state={{"source_kind","webpage"},{"media_id",media},{"id",id},{"mount","web"},{"path",source},{"status","watching"},{"window_seconds",seconds},{"notice_pending",false},{"failure_count",0}};
        resetCoverage(previous);lastWake=0;return true;
    }
    void observe(const Json& value,std::int64_t now){
        // Polling cached telemetry must not refresh its age. A sampled frame can
        // arrive after newer presentation telemetry: never turn it into a seek.
        if(value.contains("sampled_at_ms")){
            if(!value.at("sampled_at_ms").is_number_integer())return;
            const auto sampled=value.at("sampled_at_ms").get<std::int64_t>();
            if(sampled<=sampleAt||sampled<=0||sampled>now||now-sampled>3000)return;
            now=sampled;
        }
        const auto p=value.value("position_seconds",0.0);if(!std::isfinite(p)||p<0||p>86400)return;
        const double elapsed=sampleAt?double(now-sampleAt)/1000:0;
        const auto current=value.value("state",std::string{});
        playback=value;
        if(exists()&&active()){
            if(value.value("mode","")!=(web()?"webpage":"direct")||!sameSource(value.value("source",std::string{}))||(web()&&value.value("media_id",std::string{})!=state.value("media_id",std::string{}))){state["status"]="suspended";state["reason"]="source_changed";invalidate();}
            else if(current=="failed"||current=="stopped"||current=="idle"||current=="opening"||current=="unavailable"){
                state["status"]="suspended";state["reason"]="playback_unavailable";invalidate();
            }else if(value.value("seeking",false)||elapsed<0||elapsed>3||p<previous-0.06||p-previous>elapsed*std::clamp(value.value("playback_rate",1.0),0.1,4.0)+0.5){resetCoverage(p);state["reason"]="seek_or_telemetry_gap";}
            else if(previousState=="playing")state["coverage"]["to"]=std::max(state.at("coverage").at("to").get<double>(),p);
            else if(current!="playing"&&std::abs(p-previous)>0.06){resetCoverage(p);state["reason"]="seek_while_paused";}
            if(current=="paused"&&previousState!="paused"){state["unanswered_notices"]=0;invalidate();}
        }
        previous=p;previousState=current;sampleAt=now;
    }
    bool resume(std::int64_t now){
        if(!exists()||state.value("status","")=="stopped"||!playable()||now-sampleAt>3000)return false;
        // Pausing the task does not observe the intervening video; resume from now.
        state["status"]="watching";state.erase("reason");state["failure_count"]=0;resetCoverage(previous);lastWake=0;return true;
    }
    void control(const std::string& action){if(!exists())return;state["status"]=action=="stop"?"stopped":"paused";state["notice_pending"]=false;invalidate();}
    void attention(bool hasInput,bool runtimePaused,bool busy=false){if((hasInput&&!input)||(runtimePaused&&!paused)){if(exists())state["unanswered_notices"]=0;invalidate();}input=hasInput;paused=runtimePaused;occupied=busy;}
    bool permits(const std::string& mount,const std::string& path,double from,double to,std::int64_t now)const{
        if(!guarded())return true;
        if(!active()||paused||input||!playable()||now-sampleAt>3000)return false;
        const auto c=state.at("coverage");
        return mount==state.at("mount").get<std::string>()&&path==state.at("path").get<std::string>()&&from>=c.at("from").get<double>()&&to<=c.at("to").get<double>()&&(to>from||(web()&&to==from));
    }
    Json next(std::int64_t now)const{
        if(!active()||paused||input||!playable()||previousState=="paused"||now-sampleAt>3000)return nullptr;
        const double from=state.at("cursor"),end=state.at("coverage").at("to"),seconds=state.at("window_seconds");
        if(end-from<seconds&&!(previousState=="ended"&&end-from>=0.5))return nullptr;
        return {{"from",from},{"to",std::min(end,from+seconds)},{"mount",state.at("mount")},{"path",state.at("path")}};
    }
    Json notice(std::int64_t now){
        if(occupied)return nullptr;
        const auto window=next(now);
        if(window.is_null()||(state.value("notice_pending",false)&&lastWake&&now-lastWake<60000))return nullptr;
        const int unanswered=state.value("unanswered_notices",0);if(unanswered>=3){state["status"]="paused";state["reason"]="three_unconsumed_opportunities";invalidate();return {{"kind","video.watch_changed"},{"session",state}};}
        state["unanswered_notices"]=unanswered+1;lastWake=now;state["notice_pending"]=true;
        return {{"kind","video.watch_ready"},{"wake_model",true},{"session_id",state.at("id")},{"generation",epoch},{"window",window},{"instruction","陪看到了下一次观察时机。用video.watch_next取得实际画面和回执，再按需要用video.note记下进展、疑问或感想；网页只采当前帧，不代表看过两次采样之间的全部内容。可以安静看，不必逐段发消息。"}};
    }
    Json commit(const Json& window,const std::string& receiptId,std::uint64_t expected,std::int64_t now,bool advance){
        if(expected!=epoch||!permits(window.at("mount"),window.at("path"),window.at("from"),window.at("to"),now))return nullptr;
        Json receipt={{"id",receiptId},{"session_id",state.at("id")},{"mount",window.at("mount")},{"path",window.at("path")},{"window",{{"from",window.at("from")},{"to",window.at("to")}}},{"kind",web()?"sampled_web_frames":"sampled_storyboard"}};
        if(web())receipt["source_url"]=boundSource;
        receipts.push_back(receipt);if(receipts.size()>64)receipts.erase(receipts.begin());
        if(advance){state["cursor"]=window.at("to");state["notice_pending"]=false;state["failure_count"]=0;state["unanswered_notices"]=0;}
        if(previousState=="ended"&&state.at("coverage").at("to").get<double>()-state.at("cursor").get<double>()<0.5){state["status"]="completed";invalidate();}
        return receipt;
    }
    void failed(){if(!active())return;int n=state.value("failure_count",0)+1;state["failure_count"]=n;if(n>=3){state["status"]="paused";state["reason"]="analysis_failed_three_times";invalidate();}}
    Json addNote(const std::string& receiptId,const std::string& category,const std::string& text,const std::string& id){
        if(text.empty()||text.size()>1200||(category!="character"&&category!="event"&&category!="question"&&category!="impression"))return nullptr;
        for(const auto& r:receipts)if(r.at("id")==receiptId){Json n={{"id",id},{"category",category},{"text",text},{"source",r}};notes.push_back(n);if(notes.size()>64)notes.erase(notes.begin());return n;}
        return nullptr;
    }
    Json snapshot()const{return {{"version",1},{"session",state},{"receipts",receipts},{"notes",notes}};}
    bool restore(const Json& value){
        try{
            if(!value.is_object()||value.value("version",0)!=1)return false;
            auto candidate=value.at("session"),r=value.at("receipts"),n=value.at("notes");
            if(!candidate.is_object()||!r.is_array()||r.size()>64||!n.is_array()||n.size()>64||value.dump().size()>200000)return false;
            if(!candidate.empty()){
                const double c=candidate.at("cursor"),f=candidate.at("coverage").at("from"),t=candidate.at("coverage").at("to"),seconds=candidate.at("window_seconds");
                if(!std::isfinite(c)||!std::isfinite(f)||!std::isfinite(t)||f<0||c<f||c>t||t>86400||seconds<10||seconds>120||candidate.at("path").get<std::string>().size()>1024)return false;
                if(candidate.value("status","")!="stopped"&&candidate.value("status","")!="completed"){candidate["status"]="suspended";candidate["reason"]="restart_requires_resume";}
                candidate["notice_pending"]=false;
            }
            for(const auto& row:r)if(!row.at("id").is_string()||!row.at("window").is_object())return false;
            for(const auto& row:n)if(!row.at("text").is_string()||row.at("text").get<std::string>().size()>1200||!row.at("source").is_object())return false;
            state=std::move(candidate);receipts=std::move(r);notes=std::move(n);boundSource.clear();sampleAt=lastWake=0;epoch=std::max(epoch,state.value("generation",std::uint64_t(0)));invalidate();return true;
        }catch(...){return false;}
    }
};
}
