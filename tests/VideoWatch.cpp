#include "../plugins/video-analysis/WatchSession.h"
#include <iostream>
#include <stdexcept>
using hibernation::video::WatchSession;
using Json=nlohmann::json;
int checks=0;
void check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;}
Json playback(double position,const char* state="playing",const char* source="E:/materials/clip.mp4"){return {{"mode","direct"},{"source",source},{"state",state},{"position_seconds",position}};}
int main(){try{
 WatchSession w;std::int64_t now=10000;
 w.observe(playback(0),now);check(w.start("materials","clip.mp4","e:\\materials\\clip.mp4","session-1",10,now),"start matches Windows paths");
 check(!w.start("materials","other.mp4","E:/materials/other.mp4","wrong",10,now)&&w.state.at("id")=="session-1"&&w.sameSource("E:/materials/clip.mp4"),"failed start preserves existing session binding");
 check(w.next(now).is_null(),"no future window on start");check(!w.permits("materials","clip.mp4",0,10,now),"future frame request denied");
 for(int i=1;i<=40;++i){now+=250;w.observe(playback(i*.25),now);}
 auto window=w.next(now);check(!window.is_null()&&window.at("to")==10,"actual playback advances ready window");
 w.attention(false,false,true);check(w.notice(now).is_null()&&!w.next(now).is_null(),"foreground work defers background wakes without blocking current tool call");w.attention(false,false,false);
 const auto epoch=w.epoch;check(!w.notice(now).is_null(),"one opportunity emitted");check(w.notice(now+100).is_null(),"no per-frame model wakes");
 check(!w.permits("materials","clip.mp4",0,10.1,now),"tiny future overrun denied");check(!w.permits("drafts","clip.mp4",0,10,now),"different mount denied");
 auto receipt=w.commit(window,"view-1",epoch,now,true);check(!receipt.is_null()&&w.state.at("cursor")==10,"successful observation advances cursor");
 check(w.addNote("fake","event","invented","n0").is_null(),"invented source denied");
 auto note=w.addNote("view-1","impression","这盏灯好像一直亮着。","n1");check(!note.is_null()&&note.at("source").at("window").at("to")==10,"notes retain exact source window");
 check(w.addNote("view-1","fact","test","n2").is_null(),"unknown note category rejected");
 now+=250;w.observe(playback(30),now);check(w.epoch!=epoch&&w.state.at("cursor")==30,"forward seek resets coverage without granting skipped frames");
 check(!w.permits("materials","clip.mp4",10,30,now),"skipped range inaccessible");
 for(int i=1;i<=40;++i){now+=250;w.observe(playback(30+i*.25),now);}
 auto stale=w.next(now);auto before=w.epoch;w.attention(true,false);check(w.next(now).is_null(),"user input yields");check(w.commit(stale,"stale",before,now,true).is_null(),"input cancels stale worker result");w.attention(false,false);
 before=w.epoch;now+=250;w.observe(playback(40,"paused"),now);check(before!=w.epoch&&w.next(now).is_null(),"playback pause invalidates pending analysis and wake");
 now+=250;w.observe(playback(40),now);check(!w.next(now).is_null(),"playback resume retains prior continuous coverage");
 w.control("pause");check(!w.permits("materials","clip.mp4",30,40,now),"task pause rejects analysis");
 now+=250;w.observe(playback(45),now);check(w.resume(now)&&w.state.at("cursor")==45,"task resume excludes unattended playback");
 now+=250;w.observe(playback(44),now);check(w.state.at("coverage").at("from")==44,"backward seek reanchors");
 now+=5000;w.observe(playback(49),now);check(w.state.at("coverage").at("from")==49,"telemetry gap cannot create observed coverage");
 before=w.epoch;now+=250;w.observe(playback(0,"playing","E:/materials/other.mp4"),now);check(w.state.at("status")=="suspended"&&w.epoch!=before,"source switch suspends task");
 check(!w.resume(now),"cannot resume against another film");
 auto saved=w.snapshot();WatchSession restored;check(restored.restore(saved)&&restored.notes.size()==1,"state and notes survive serialization");check(restored.state.at("status")=="suspended"&&restored.next(now).is_null(),"restart does not auto-watch");check(restored.epoch>w.epoch,"restart invalidates pre-restart wake generations");
 now+=250;restored.observe(playback(0),now);restored.boundSource="E:/materials/clip.mp4";check(restored.resume(now),"explicit resume binds restored source");
 for(int i=1;i<=8;++i){now+=250;restored.observe(playback(i*.25,i==8?"ended":"playing"),now);}
 auto tail=restored.next(now);check(!tail.is_null()&&tail.at("to")==2,"short end remainder can be observed");
 check(!restored.commit(tail,"tail",restored.epoch,now,true).is_null()&&restored.state.at("status")=="completed","end completes only after final observation");
 restored.control("stop");check(!restored.guarded(),"explicit stop releases preview mode");
 auto broken=saved;broken["session"]["window_seconds"]=-1;check(!restored.restore(broken),"corrupt persisted bounds rejected");
 WatchSession ordered;auto stamped=playback(0);stamped["sampled_at_ms"]=now;ordered.observe(stamped,now);
 check(ordered.start("materials","clip.mp4","E:/materials/clip.mp4","ordered",10,now),"timestamped telemetry starts");
 const auto sampledAt=now;ordered.observe(stamped,now+2000);check(ordered.sampleAt==sampledAt,"cached telemetry does not renew freshness");
 auto newer=playback(.5);newer["sampled_at_ms"]=now+500;ordered.observe(newer,now+510);const auto orderedEpoch=ordered.epoch;
 auto delayed=playback(.25);delayed["sampled_at_ms"]=now+250;ordered.observe(delayed,now+520);
 check(ordered.previous==.5&&ordered.epoch==orderedEpoch,"late sampled frame does not rewind or interrupt viewing");
 auto realSeek=playback(12);realSeek["sampled_at_ms"]=now+1000;ordered.observe(realSeek,now+1010);
 check(ordered.previous==12&&ordered.epoch!=orderedEpoch,"newer real seek still resets coverage");
 WatchSession failure;now+=250;failure.observe(playback(0),now);check(failure.start("materials","clip.mp4","E:/materials/clip.mp4","f",10,now),"failure session starts");failure.failed();failure.failed();failure.failed();check(failure.state.at("status")=="paused","three processing failures stop retries");
 WatchSession ignored;now+=250;ignored.observe(playback(0),now);check(ignored.start("materials","clip.mp4","E:/materials/clip.mp4","ignored",10,now),"ignored session starts");
 for(int i=1;i<=40;++i){now+=250;ignored.observe(playback(i*.25),now);}for(int i=0;i<4;++i){if(i)for(int j=0;j<60;++j){now+=1000;ignored.observe(playback(10),now);}const auto reminder=ignored.notice(now);if(i<3)check(!reminder.is_null(),"bounded reminder emitted");}check(ignored.state.at("status")=="paused","ignored opportunities stop after three reminders");

 WatchSession web;now+=10000;
 auto wp=[](double p,const char* state="playing",const char* url="https://www.bilibili.com/video/BVTest/") {return Json{{"mode","webpage"},{"source",url},{"media_id","media-A"},{"state",state},{"position_seconds",p}};};
 web.observe(wp(0),now);check(web.startWeb("web-1",10,now)&&web.web(),"web watch binds actual browser telemetry");
 check(!web.sameSource("https://www.bilibili.com/video/bvtest/"),"web source matching preserves case-sensitive paths");
 for(int i=1;i<=21;++i){now+=500;web.observe(wp(i*.5),now);}
 now+=500;web.observe(wp(10.5,"buffering"),now);check(web.active()&&web.next(now).is_null(),"temporary website buffering defers sampling without dropping task");
 now+=500;web.observe(wp(10.5),now);check(!web.next(now).is_null(),"website continues after buffering without manual restart");
 auto opportunity=web.notice(now);check(!opportunity.is_null()&&opportunity.at("window").at("mount")=="web","web advancing playback emits scheduled opportunity");
 auto sampled=Json{{"mount","web"},{"path","https://www.bilibili.com/video/BVTest/"},{"from",10.0},{"to",10.5}};
 auto wr=web.commit(sampled,"web-view",web.epoch,now,true);check(!wr.is_null()&&wr.at("kind")=="sampled_web_frames"&&wr.at("window").at("from")==10.0,"web receipt covers sampled frames rather than unsampled interval");
 check(web.next(now).is_null(),"web success consumes opportunity until new progress");
 check(!web.addNote("web-view","impression","灯换了颜色。","web-note").is_null(),"web note retains actual receipt");
 now+=500;web.observe(wp(11,"paused"),now);check(web.next(now).is_null(),"web pause does not repeatedly wake model");
 now+=500;web.observe(wp(60,"paused"),now);check(web.state.at("cursor")==60,"web seek discards skipped coverage");
 now+=500;web.observe(wp(60),now);for(int i=1;i<=2;++i){now+=500;web.observe(wp(60+i*.5,i==2?"ended":"playing"),now);}
 check(!web.next(now).is_null(),"web ended tail schedules final observation");
 auto finalFrame=Json{{"mount","web"},{"path","https://www.bilibili.com/video/BVTest/"},{"from",61.0},{"to",61.0}};
 check(!web.commit(finalFrame,"web-end",web.epoch,now,true).is_null()&&web.state.at("status")=="completed","web ended still frame completes sampled watching");
 now+=500;web.observe(wp(0),now);check(web.startWeb("web-2",10,now),"web restarts explicitly");
 now+=500;web.observe(wp(0,"playing","https://www.bilibili.com/video/OTHER/"),now);check(web.state.at("status")=="suspended","web navigation suspends bound task");
 auto resumed=web.snapshot();WatchSession persisted;check(persisted.restore(resumed)&&persisted.web()&&persisted.notes.size()==1,"web state and sourced notes survive restart");
 check(persisted.state.at("status")=="suspended"&&!persisted.resume(now),"web restart requires fresh matching telemetry");
 now+=500;web.observe(playback(0),now);check(web.start("materials","clip.mp4","E:/materials/clip.mp4","local-after-web",10,now)&&!web.web(),"can start local watching after a web task");
 std::cout<<checks<<" watch session checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
