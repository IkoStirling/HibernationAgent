#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#ifdef HA_TEST_MEDIA
#include "FFmpegTestMedia.h"
#endif
using Json=nlohmann::json;
namespace {
std::string read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {(std::istreambuf_iterator<char>(input)),{}};}
void substitute(Json& value,const Json& variables){if(value.is_string()){auto text=value.get<std::string>();for(auto it=variables.begin();it!=variables.end();++it){const auto key="${"+it.key()+"}";size_t at=0;const auto replacement=it.value().get<std::string>();while((at=text.find(key,at))!=std::string::npos){text.replace(at,key.size(),replacement);at+=replacement.size();}}value=text;}else if(value.is_structured())for(auto& child:value)substitute(child,variables);}
std::string tone(){const int samples=16000*3;std::string bytes(44+samples*2,'\0');auto u16=[&](size_t at,uint16_t x){std::memcpy(bytes.data()+at,&x,2);};auto u32=[&](size_t at,uint32_t x){std::memcpy(bytes.data()+at,&x,4);};std::memcpy(bytes.data(),"RIFF",4);u32(4,static_cast<uint32_t>(bytes.size()-8));std::memcpy(bytes.data()+8,"WAVEfmt ",8);u32(16,16);u16(20,1);u16(22,1);u32(24,16000);u32(28,32000);u16(32,2);u16(34,16);std::memcpy(bytes.data()+36,"data",4);u32(40,samples*2);for(int n=0;n<samples;++n)u16(44+n*2,static_cast<int16_t>(std::sin(n*440.0*6.283185307/16000)*1500));return bytes;}
}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;const int lifetime=argc==3?std::atoi(argv[2]):7200;if(lifetime<60||lifetime>43200)return 2;const auto root=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(root);httplib::Server server;server.new_task_queue=[]{return new httplib::ThreadPool(4);};server.set_payload_max_length(2*1024*1024);std::atomic<int> calls=0,audioCalls=0;std::mutex mutex;std::string lastPlan,reviewPlan;size_t planAt=0,reviewAt=0;
#ifdef HA_TEST_MEDIA
    std::string issue;if(!ayt::testmedia::generateClip((root/"clip.mp4").string(),true,200,64,48,25,issue))return 5;
#endif
    server.Post("/v1/chat/completions",[&](const auto& request,auto& response){
        ++calls;std::string output;int delay=0,httpStatus=200;const bool reviewing=Json::parse(request.body).value("model","")=="fixture-reviewer";{
            std::lock_guard lock(mutex);{std::ofstream capture(root/(reviewing?"review-last-request.json":"last-request.json"),std::ios::binary);capture<<request.body;}{std::ofstream history(root/(reviewing?"review-requests.jsonl":"requests.jsonl"),std::ios::binary|std::ios::app);history<<request.body<<'\n';}
            auto& last=reviewing?reviewPlan:lastPlan;auto& at=reviewing?reviewAt:planAt;
            const auto planText=read(root/(reviewing?"review-plan.json":"plan.json"));if(planText.empty())output=read(root/"response.json");else try{
                if(planText!=last){last=planText;at=0;}auto plan=Json::parse(planText);const auto& sequence=plan.at("responses");const auto index=std::min(at++,sequence.size()-1);auto item=sequence.at(index);delay=item.value("delay_ms",0);httpStatus=item.value("http_status",200);Json variables={{"event_id",""},{"last_source_id",""},{"last_memory_id",""}};
                const auto body=Json::parse(request.body);for(const auto& message:body.at("messages")){if(!message.at("content").is_string())continue;try{const auto content=Json::parse(message.at("content").get<std::string>());if(content.contains("event_id"))variables["event_id"]=content.at("event_id");if(content.contains("tool_results"))for(const auto& result:content.at("tool_results")){if(result.contains("source_id"))variables["last_source_id"]=result.at("source_id");if(result.value("ok",false)&&result.contains("result")&&result.at("result").is_object()&&result.at("result").contains("id"))variables["last_memory_id"]=result.at("result").at("id");}}catch(...){}}
                auto value=item.at("response");substitute(value,variables);output=value.is_string()?value.get<std::string>():value.dump();
            }catch(...){response.status=500;response.set_content("fixture_plan_invalid","text/plain");return;}
        }
        if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(delay,0,30000)));
        if(httpStatus!=200){response.status=httpStatus;response.set_content(R"({"error":{"code":"fixture_transient"}})","application/json");return;}
        auto envelope=nlohmann::json{{"id","fixture-request"},{"model","fixture-model"},{"choices",nlohmann::json::array({{{"message",{{"role","assistant"},{"content",output}}},{"finish_reason","stop"}}})}};
        response.set_content(envelope.dump(),"application/json");
    });
    server.Get("/v1/search",[](const auto&,auto& response){response.set_content(Json{{"results",Json::array({{{"name","验证城市"},{"country","Fixture"},{"latitude",30.0},{"longitude",120.0}}})}}.dump(),"application/json");});
    server.Get("/v1/forecast",[](const auto&,auto& response){response.set_content(Json{{"current",{{"temperature_2m",38.0},{"weather_code",63},{"relative_humidity_2m",85},{"precipitation",2.0},{"wind_speed_10m",12.0},{"is_day",1},{"time","2026-10-25T08:00"}}}}.dump(),"application/json");});
    server.Post("/v1/audio/speech",[&](const auto&,auto& response){++audioCalls;response.set_content(tone(),"audio/wav");});
    server.Get("/clip.mp4",[&](const auto&,auto& response){const auto bytes=read(root/"clip.mp4");if(bytes.empty()){response.status=404;return;}response.set_content(bytes,"video/mp4");});
    server.Get("/fixture/health",[&](const auto&,auto& response){response.set_content(Json{{"calls",calls.load()},{"audio_calls",audioCalls.load()}}.dump(),"application/json");});
    server.Get("/fixture/page",[](const auto&,auto& response){response.set_content(R"(<html><meta charset="utf-8"><title>网页桥接验证</title><body style="background:#dfead8"><h1>实际的本地测试页面</h1><p id="visits"></p><a href="/fixture/video/watch">测试影片链接</a><input type="password" value="never-expose-password-value"><video controls muted src="/fixture/tone.wav" style="width:280px;height:100px"></video><script>let n=Number(localStorage.fixtureVisits||0)+1;localStorage.fixtureVisits=n;document.querySelector('#visits').innerText='页面访问次数：'+n;</script></body></html>)","text/html; charset=utf-8");});
    server.Get("/fixture/video/watch",[](const auto&,auto& response){response.set_content("<html><meta charset='utf-8'><title>选择后的影片页面</title><body style='background:#dfead8'>这是实际选择的影片页面。</body></html>","text/html; charset=utf-8");});
    server.Get("/fixture/tv-chrome",[](const auto&,auto& response){response.set_content("<html><meta charset='utf-8'><title>电视边框验证</title><body style='margin:0;background:#dfead8;color:#305543'><p>这是嵌在电视内部的页面。</p><video controls muted loop src='/fixture/tone.wav' style='width:100%;height:120px'></video></body></html>","text/html; charset=utf-8");});
    server.Get("/fixture/tone.wav",[](const auto&,auto& response){response.set_content(tone(),"audio/wav");});
    const auto port=server.bind_to_any_port("127.0.0.1");if(port<=0)return 3;std::ofstream(root/"port")<<port;
    std::jthread host([&]{server.listen_after_bind();});std::this_thread::sleep_for(std::chrono::seconds(lifetime));server.stop();host.join();return 0;
}
