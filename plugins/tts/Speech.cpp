#include "../PluginCommon.h"
#include <sapi.h>
#include <sphelper.h>
#include <mmsystem.h>
#include <deque>
#include <future>
#include <condition_variable>
#include <thread>
#include <cstring>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
std::wstring xmlEscape(const std::wstring& text){std::wstring out;for(auto c:text){if(c==L'&')out+=L"&amp;";else if(c==L'<')out+=L"&lt;";else if(c==L'>')out+=L"&gt;";else out+=c;}return out;}
struct Wav {WAVEFORMATEX format{};std::vector<char> pcm;double seconds=0;};
void gain(Wav& audio,int volume){
    if(volume==100)return;for(size_t at=0;at<audio.pcm.size();at+=audio.format.wBitsPerSample/8){
        if(audio.format.wBitsPerSample==16){int16_t value;std::memcpy(&value,audio.pcm.data()+at,2);value=static_cast<int16_t>(static_cast<int32_t>(value)*volume/100);std::memcpy(audio.pcm.data()+at,&value,2);}
        else if(audio.format.wFormatTag==WAVE_FORMAT_PCM){int32_t value;std::memcpy(&value,audio.pcm.data()+at,4);value=static_cast<int32_t>(static_cast<int64_t>(value)*volume/100);std::memcpy(audio.pcm.data()+at,&value,4);}
        else{float value;std::memcpy(&value,audio.pcm.data()+at,4);value=std::isfinite(value)?std::clamp(value*volume/100.0f,-1.0f,1.0f):0;std::memcpy(audio.pcm.data()+at,&value,4);}
    }
}
bool wav(const std::string& bytes,Wav& out){if(bytes.size()<12||bytes.compare(0,4,"RIFF")||bytes.compare(8,4,"WAVE"))return false;auto u32=[&](size_t p){uint32_t value=0;std::memcpy(&value,bytes.data()+p,4);return value;};bool fmt=false,data=false;for(size_t at=12;at+8<=bytes.size();){const auto size=u32(at+4);if(size>bytes.size()-at-8)return false;if(bytes.compare(at,4,"fmt ")==0){if(size<16)return false;std::memcpy(&out.format,bytes.data()+at+8,16);out.format.cbSize=0;fmt=true;}if(bytes.compare(at,4,"data")==0){out.pcm.assign(bytes.begin()+at+8,bytes.begin()+at+8+size);data=true;}at+=8+size+(size&1);}
    if(!fmt||!data||out.format.nChannels<1||out.format.nChannels>2||out.format.nSamplesPerSec<8000||out.format.nSamplesPerSec>96000||(out.format.wBitsPerSample!=16&&out.format.wBitsPerSample!=32)||(out.format.wFormatTag!=WAVE_FORMAT_PCM&&out.format.wFormatTag!=WAVE_FORMAT_IEEE_FLOAT)||(out.format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT&&out.format.wBitsPerSample!=32)||out.format.nBlockAlign!=out.format.nChannels*out.format.wBitsPerSample/8||out.pcm.size()%out.format.nBlockAlign)return false;out.format.nAvgBytesPerSec=out.format.nSamplesPerSec*out.format.nBlockAlign;out.seconds=static_cast<double>(out.pcm.size())/out.format.nAvgBytesPerSec;return out.seconds<=300;
}
class Speech final:public BasePlugin {
    struct Command {std::string text,id;double maxSeconds=18;unsigned generation=0;std::shared_ptr<std::promise<PluginJson>> result;};
    PluginHostContext host_;PluginJson cfg_;mutable std::mutex mutex_;std::condition_variable cv_;std::deque<Command> commands_;std::thread thread_;std::atomic<bool> stop_=false,active_=false;std::atomic<unsigned> generation_=0;std::string voiceName_;double lastDuration_=0;
    std::string nextId(){static std::atomic<unsigned> next=0;return "speech-"+std::to_string(nowMs())+"-"+std::to_string(next++);}
    void queue(Command cmd){std::lock_guard lock(mutex_);if(commands_.size()>=8){if(cmd.result)cmd.result->set_value(toolFailure("speech_queue_full"));return;}commands_.push_back(std::move(cmd));cv_.notify_one();}
    void work(){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ISpVoice* voice=nullptr;CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,reinterpret_cast<void**>(&voice));
        if(voice){ISpObjectToken* token=nullptr;if(SUCCEEDED(SpFindBestToken(SPCAT_VOICES,L"Language=804",nullptr,&token))&&token){voice->SetVoice(token);token->Release();}ISpObjectToken* current=nullptr;if(SUCCEEDED(voice->GetVoice(&current))&&current){wchar_t* name=nullptr;if(SUCCEEDED(SpGetDescription(current,&name))&&name){const auto n=WideCharToMultiByte(CP_UTF8,0,name,-1,nullptr,0,nullptr,nullptr);std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,name,-1,text.data(),n,nullptr,nullptr);if(!text.empty())text.pop_back();{std::lock_guard lock(mutex_);voiceName_=std::move(text);}CoTaskMemFree(name);}current->Release();}}
        while(!stop_){Command cmd;PluginJson cfg;std::function<void(PluginJson)> emit;{std::unique_lock lock(mutex_);cv_.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop_||!commands_.empty();});if(stop_)break;if(commands_.empty())continue;cmd=std::move(commands_.front());commands_.pop_front();cfg=cfg_;emit=host_.emit;}
            if(cmd.generation!=generation_){if(cmd.result)cmd.result->set_value(toolFailure("speech_superseded"));continue;}const auto start=std::chrono::steady_clock::now();bool started=false;double duration=0;HWAVEOUT output=nullptr;WAVEHDR header{};Wav audio;
            auto cancelled=[&]{return stop_.load()||cmd.generation!=generation_.load();};
            if(cfg.value("backend",std::string("system"))=="http"){
                ayt::net::http::Request request;request.useSystemProxy=cfg.value("useSystemProxy",true);request.proxyOrigin=cfg.value("proxyOrigin",std::string{});request.origin=cfg.value("origin",std::string("http://127.0.0.1:5000"));request.path=cfg.value("path",std::string("/v1/audio/speech"));request.timeout=std::chrono::seconds(15);request.maxResponseBytes=8*1024*1024;request.cancelled=cancelled;request.headers={{"Content-Type","application/json"}};const auto key=ayt::net::http::environmentCredential(cfg.value("credentialEnvironment",std::string{}));if(!key.empty())request.headers.emplace_back("Authorization","Bearer "+key);
                request.body=PluginJson{{"model",cfg.value("model",std::string("tts"))},{"input",cmd.text},{"voice",cfg.value("voice",std::string("default"))},{"response_format","wav"},{"speed",std::clamp(1.0+cfg.value("rate",5)*0.1,0.25,4.0)}}.dump();const auto result=ayt::net::http::perform(request);
                if(result&&result.httpStatus==200&&wav(result.body,audio)&&!cancelled()){
                    const auto maximum=static_cast<size_t>(cmd.maxSeconds*audio.format.nSamplesPerSec)*audio.format.nBlockAlign;if(audio.pcm.size()>maximum)audio.pcm.resize(maximum);duration=static_cast<double>(audio.pcm.size())/audio.format.nAvgBytesPerSec;gain(audio,cfg.value("volume",75));
                    if(waveOutOpen(&output,WAVE_MAPPER,&audio.format,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR){header.lpData=audio.pcm.data();header.dwBufferLength=static_cast<DWORD>(audio.pcm.size());if(waveOutPrepareHeader(output,&header,sizeof(header))==MMSYSERR_NOERROR&&waveOutWrite(output,&header,sizeof(header))==MMSYSERR_NOERROR)started=true;}
                }
            }else if(voice&&!cancelled()){
                voice->SetRate(cfg.value("rate",5));voice->SetVolume(static_cast<USHORT>(cfg.value("volume",75)));const auto text=L"<pitch middle=\"+6\">"+xmlEscape(wide(cmd.text))+L"</pitch>";started=SUCCEEDED(voice->Speak(text.c_str(),SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_XML,nullptr));duration=std::min(cmd.maxSeconds,std::max(0.8,wide(cmd.text).size()/7.0));
            }
            if(cmd.result)cmd.result->set_value(started?toolSuccess({{"speech_id",cmd.id},{"started",true},{"estimated_seconds",duration},{"maximum_seconds",cmd.maxSeconds}}):toolFailure(cancelled()?"cancelled":"speech_unavailable",true));
            if(!started){if(output){waveOutReset(output);if(header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(output,&header,sizeof(header));waveOutClose(output);}emit({{"kind","speech.failed"},{"plugin","tts"},{"speech_id",cmd.id}});continue;}
            active_=true;const auto speakingStart=std::chrono::steady_clock::now();emit({{"kind","speech.started"},{"plugin","tts"},{"speech_id",cmd.id},{"text",cmd.text},{"maximum_seconds",cmd.maxSeconds},{"estimated_seconds",duration}});
            while(!cancelled()&&std::chrono::steady_clock::now()-speakingStart<std::chrono::duration<double>(cmd.maxSeconds)){if(output){if(header.dwFlags&WHDR_DONE)break;std::this_thread::sleep_for(std::chrono::milliseconds(15));}else if(voice->WaitUntilDone(20)==S_OK)break;}
            if(output){waveOutReset(output);waveOutUnprepareHeader(output,&header,sizeof(header));waveOutClose(output);}else if(voice)voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);active_=false;
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-speakingStart).count();{std::lock_guard lock(mutex_);lastDuration_=elapsed;}emit({{"kind","speech.ended"},{"plugin","tts"},{"speech_id",cmd.id},{"duration_seconds",elapsed}});
        }
        if(voice){voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);voice->Release();}if(SUCCEEDED(com))CoUninitialize();
        std::lock_guard lock(mutex_);for(auto& cmd:commands_)if(cmd.result)cmd.result->set_value(toolFailure("speech_stopped"));commands_.clear();
    }
public:
    const char* getName()const override{return "tts";}const char* getDescription()const override{return "Bounded local SAPI or HTTP WAV speech";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{try{configurationTypes(cfg,{"backend","origin","path","model","voice","credentialEnvironment","proxyOrigin"},{"autoSpeak","useSystemProxy"});const auto backend=cfg.value("backend",std::string("system"));if(backend!="system"&&backend!="http")throw std::runtime_error("backend");number(cfg,"rate",5,-10,10);number(cfg,"volume",75,0,100);number(cfg,"maxSeconds",18,1,120);
        if(backend=="http"){const auto origin=cfg.value("origin",std::string("http://127.0.0.1:5000"));if(!ayt::net::http::validOrigin(origin)||(!origin.starts_with("https://")&&!origin.starts_with("http://127.0.0.1:")&&!origin.starts_with("http://localhost:")))throw std::runtime_error("origin");}
        {std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);}if(!thread_.joinable()){stop_=false;thread_=std::thread([this]{work();});}issue.clear();return true;}catch(...){issue="speech_configuration";return false;}}
    PluginJson tools()const override{return PluginJson::array({tool("speech.speak","朗读文字；实际启动后返回结果，并限制最大朗读时长。",object({{"text",{{"type","string"},{"minLength",1},{"maxLength",6000}}},{"max_seconds",{{"type","number"},{"minimum",1},{"maximum",120}}}},PluginJson::array({"text"}))),tool("speech.stop","中断当前朗读。",object()),tool("speech.status","读取语音播放状态。",object())});}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{if(name=="speech.stop"){++generation_;return toolSuccess({{"stopping",true}});}if(name=="speech.status"){std::lock_guard lock(mutex_);return toolSuccess({{"active",active_.load()},{"voice",voiceName_},{"last_duration_seconds",lastDuration_}});}
        auto promise=std::make_shared<std::promise<PluginJson>>();auto future=promise->get_future();double maximum;{std::lock_guard lock(mutex_);maximum=std::min(cfg_.value("maxSeconds",18.0),args.value("max_seconds",120.0));}const auto generation=++generation_;queue({args.at("text"),nextId(),maximum,generation,promise});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(18);while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready){if((cancelled&&cancelled())||std::chrono::steady_clock::now()>deadline){++generation_;return toolFailure("speech_cancelled_or_timeout",true);}}return future.get();
    }
    void event(const PluginJson& event)override{if(event.value("kind","")=="runtime.paused"&&event.value("paused",false)){++generation_;return;}if(event.value("kind","")!="assistant.reply")return;PluginJson cfg;{std::lock_guard lock(mutex_);cfg=cfg_;}if(!cfg.value("autoSpeak",true)||!event.value("presentation",PluginJson::object()).value("speech",true))return;const auto generation=++generation_;queue({event.at("text"),nextId(),cfg.value("maxSeconds",18.0),generation,{}});}
    PluginJson snapshot()const override{return PluginJson::object();}
    void shutdown()override{stop_=true;++generation_;cv_.notify_all();if(thread_.joinable())thread_.join();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Speech;}
