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
    const auto serve=[&](const httplib::Request& request,httplib::Response& response,bool responses){
        ++calls;std::string output;int delay=0,httpStatus=200;const bool reviewing=Json::parse(request.body).value("model","")=="fixture-reviewer";{
            std::lock_guard lock(mutex);{std::ofstream capture(root/(reviewing?"review-last-request.json":"last-request.json"),std::ios::binary);capture<<request.body;}{std::ofstream history(root/(reviewing?"review-requests.jsonl":"requests.jsonl"),std::ios::binary|std::ios::app);history<<request.body<<'\n';}
            auto& last=reviewing?reviewPlan:lastPlan;auto& at=reviewing?reviewAt:planAt;
            const auto planText=read(root/(reviewing?"review-plan.json":"plan.json"));if(planText.empty())output=read(root/"response.json");else try{
                if(planText!=last){last=planText;at=0;}auto plan=Json::parse(planText);const auto& sequence=plan.at("responses");const auto index=std::min(at++,sequence.size()-1);auto item=sequence.at(index);delay=item.value("delay_ms",0);httpStatus=item.value("http_status",200);Json variables={{"event_id",""},{"last_source_id",""},{"last_memory_id",""}};
                const auto body=Json::parse(request.body);const auto receipt=[&](const Json& result){if(result.contains("source_id"))variables["last_source_id"]=result.at("source_id");if(result.value("ok",false)&&result.contains("result")&&result.at("result").is_object()&&result.at("result").contains("id"))variables["last_memory_id"]=result.at("result").at("id");};
                const auto parseContent=[&](const std::string& text){try{const auto content=Json::parse(text);if(content.contains("event_id"))variables["event_id"]=content.at("event_id");if(content.contains("tool_results"))for(const auto& result:content.at("tool_results"))receipt(result);if(content.contains("source_id"))receipt(content);}catch(...){}};
                for(const auto& message:body.at(responses?"input":"messages")){const auto field=message.value("type","")=="function_call_output"?"output":"content";if(!message.contains(field))continue;const auto& content=message.at(field);if(content.is_string())parseContent(content.get<std::string>());else if(content.is_array())for(const auto& part:content)if(part.is_object()&&part.contains("text")&&part.at("text").is_string())parseContent(part.at("text").get<std::string>());}
                auto value=item.at("response");substitute(value,variables);output=value.is_string()?value.get<std::string>():value.dump();
            }catch(...){response.status=500;response.set_content("fixture_plan_invalid","text/plain");return;}
        }
        if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(delay,0,30000)));
        if(httpStatus!=200){response.status=httpStatus;response.set_content(R"({"error":{"code":"fixture_transient"}})","application/json");return;}
        const auto parsed=Json::parse(output,nullptr,false);const bool native=parsed.is_object()&&parsed.contains("fixture_native_calls");Json callsOut=Json::array();
        if(native)for(const auto& call:parsed.at("fixture_native_calls")){const auto args=call.at("arguments").is_string()?call.at("arguments").get<std::string>():call.at("arguments").dump();if(responses)callsOut.push_back({{"type","function_call"},{"id","fc_"+call.at("id").get<std::string>()},{"call_id",call.at("id")},{"name",call.at("name")},{"arguments",args},{"status","completed"}});else callsOut.push_back({{"id",call.at("id")},{"type","function"},{"function",{{"name",call.at("name")},{"arguments",args}}}});}
        if(responses){
            Json outputItems=native?callsOut:Json::array({{{"type","message"},{"role","assistant"},{"status","completed"},{"content",Json::array({{{"type","output_text"},{"text",output}}})}}});
            if(native&&parsed.contains("fixture_native_text"))outputItems.push_back({{"type","message"},{"role","assistant"},{"status","completed"},{"content",Json::array({{{"type","output_text"},{"text",parsed.at("fixture_native_text")}}})}});
            const Json envelope={{"id","fixture-request"},{"object","response"},{"model","fixture-model"},{"status","completed"},{"output",outputItems}};
            if(Json::parse(request.body).value("stream",false)){const Json done={{"type","response.completed"},{"response",envelope}};response.set_content("event: response.completed\ndata: "+done.dump()+"\n\n","text/event-stream");}else response.set_content(envelope.dump(),"application/json");return;
        }
        if(Json::parse(request.body).value("stream",false)){
            if(native){auto fragments=callsOut;for(size_t i=0;i<fragments.size();++i)fragments[i]["index"]=i;const Json delta={{"choices",Json::array({{{"index",0},{"delta",{{"tool_calls",fragments},{"content",parsed.value("fixture_native_text",std::string{})}}},{"finish_reason",nullptr}}})}};const Json finish={{"choices",Json::array({{{"index",0},{"delta",Json::object()},{"finish_reason","tool_calls"}}})}};response.set_content("data: "+delta.dump()+"\n\ndata: "+finish.dump()+"\n\ndata: [DONE]\n\n","text/event-stream");return;}
            const Json delta={{"id","fixture-request"},{"model","fixture-model"},{"choices",Json::array({{{"index",0},{"delta",{{"content",output}}},{"finish_reason",nullptr}}})}};
            const Json finish={{"choices",Json::array({{{"index",0},{"delta",Json::object()},{"finish_reason","stop"}}})}};
            response.set_content("data: "+delta.dump()+"\n\ndata: "+finish.dump()+"\n\ndata: [DONE]\n\n","text/event-stream");return;
        }
        const Json message=native?Json{{"role","assistant"},{"content",parsed.contains("fixture_native_text")?parsed.at("fixture_native_text"):Json(nullptr)},{"tool_calls",callsOut}}:Json{{"role","assistant"},{"content",output}};
        auto envelope=nlohmann::json{{"id","fixture-request"},{"model","fixture-model"},{"choices",nlohmann::json::array({{{"message",message},{"finish_reason",native?"tool_calls":"stop"}}})}};
        response.set_content(envelope.dump(),"application/json");
    };
    server.Post("/v1/chat/completions",[&](const auto& request,auto& response){serve(request,response,false);});
    server.Post("/v1/responses",[&](const auto& request,auto& response){serve(request,response,true);});
    server.Get("/v1/search",[](const auto&,auto& response){response.set_content(Json{{"results",Json::array({{{"name","验证城市"},{"country","Fixture"},{"latitude",30.0},{"longitude",120.0}}})}}.dump(),"application/json");});
    server.Get("/v1/forecast",[](const auto&,auto& response){response.set_content(Json{{"current",{{"temperature_2m",38.0},{"weather_code",63},{"relative_humidity_2m",85},{"precipitation",2.0},{"wind_speed_10m",12.0},{"is_day",1},{"time","2026-10-25T08:00"}}}}.dump(),"application/json");});
    std::atomic<int> readingCalls=0;
    const auto readingFixture=[&](const httplib::Request& request,httplib::Response& response,const char* filename){
        ++readingCalls;{std::lock_guard lock(mutex);std::ofstream(root/"reading-last-request.json")<<Json{{"target",request.target},{"user_agent",request.get_header_value("User-Agent")},{"authorization",request.has_header("Authorization")}}.dump();}
        const auto status=read(root/"reading-status.txt");if(!status.empty()){response.status=std::atoi(status.c_str());if(response.status!=200){response.set_content("fixture_reading_failure","text/plain");return;}}
        const auto content=read(root/filename);response.set_content(content.empty()?"{}":content,"application/json");
    };
    server.Get("/w/rest.php/v1/search/page",[&](const auto& request,auto& response){readingFixture(request,response,"reading-search.json");});
    server.Get(R"(/api/rest_v1/page/summary/.*)",[&](const auto& request,auto& response){readingFixture(request,response,"reading-summary.json");});
    server.Get(R"(/w/rest.php/v1/page/.*)",[&](const auto& request,auto& response){readingFixture(request,response,"reading-source.json");});
    server.Get("/fixture/reading-health",[&](const auto&,auto& response){response.set_content(Json{{"requests",readingCalls.load()}}.dump(),"application/json");});
    server.Post("/v1/audio/speech",[&](const auto&,auto& response){++audioCalls;response.set_content(tone(),"audio/wav");});
    server.Post("/fixture/slow-audio",[&](const auto&,auto& response){++audioCalls;std::this_thread::sleep_for(std::chrono::seconds(3));response.set_content(tone(),"audio/wav");});
    server.Get("/clip.mp4",[&](const auto&,auto& response){const auto bytes=read(root/"clip.mp4");if(bytes.empty()){response.status=404;return;}response.set_content(bytes,"video/mp4");});
    std::atomic<int> balanceCalls=0;
    server.Get("/user/balance",[&](const auto& request,auto& response){
        ++balanceCalls;const auto auth=request.get_header_value("Authorization");const bool reviewer=auth=="Bearer balance-reviewer-fixture";
        if(auth!="Bearer balance-actor-fixture"&&!reviewer){response.status=401;return;}
        const auto delay=read(root/"balance-delay.txt");if(!delay.empty())std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(std::atoi(delay.c_str()),0,3000)));
        const auto status=read(root/"balance-status.txt");if(!status.empty())response.status=std::atoi(status.c_str());
        const auto payload=read(root/(reviewer?"reviewer-balance.json":"balance.json"));response.set_content(payload.empty()?"{}":payload,"application/json");
    });
    server.Get("/fixture/balance-health",[&](const auto&,auto& response){response.set_content(Json{{"checks",balanceCalls.load()},{"model_calls",calls.load()}}.dump(),"application/json");});
    server.Get("/fixture/health",[&](const auto&,auto& response){response.set_content(Json{{"calls",calls.load()},{"audio_calls",audioCalls.load()}}.dump(),"application/json");});
    server.Get("/zh-cn/quick_start/pricing/",[&](const auto&,auto& response){response.set_content(read(root/"pricing.html"),"text/html; charset=utf-8");});
    server.Get("/fixture/page",[](const auto&,auto& response){response.set_content(R"(<html><meta charset="utf-8"><title>网页桥接验证</title><body style="background:#dfead8"><h1>实际的本地测试页面</h1><p id="visits"></p><a href="/fixture/video/watch">测试影片链接</a><input type="password" value="never-expose-password-value"><video controls muted src="/fixture/tone.wav" style="width:280px;height:100px"></video><script>let n=Number(localStorage.fixtureVisits||0)+1;localStorage.fixtureVisits=n;document.querySelector('#visits').innerText='页面访问次数：'+n;</script></body></html>)","text/html; charset=utf-8");});
    server.Get("/fixture/video/watch",[](const auto&,auto& response){response.set_content("<html><meta charset='utf-8'><title>选择后的影片页面</title><body style='background:#dfead8'>这是实际选择的影片页面。</body></html>","text/html; charset=utf-8");});
    server.Get("/fixture/tv-chrome",[](const auto&,auto& response){response.set_content("<html><meta charset='utf-8'><title>电视边框验证</title><body style='margin:0;background:#dfead8;color:#305543'><p>这是嵌在电视内部的页面。</p><video controls muted loop src='/fixture/tone.wav' style='width:100%;height:120px'></video></body></html>","text/html; charset=utf-8");});
    server.Get("/fixture/tone.wav",[](const auto&,auto& response){response.set_content(tone(),"audio/wav");});
    const auto port=server.bind_to_any_port("127.0.0.1");if(port<=0)return 3;std::ofstream(root/"port")<<port;
    std::jthread host([&]{server.listen_after_bind();});std::this_thread::sleep_for(std::chrono::seconds(lifetime));server.stop();host.join();return 0;
}
