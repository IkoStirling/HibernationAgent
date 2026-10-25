#include "../PluginCommon.h"
#include <sapi.h>
#include <sphelper.h>
#include <mmsystem.h>
#include <deque>
#include <future>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <optional>
#include <memory>
#include "MemorySpeech.h"
#include "KanaSpeech.h"
#include <Hibernation/TextPlayback.h>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
std::wstring xmlEscape(const std::wstring& text){std::wstring out;for(auto c:text){if(c==L'&')out+=L"&amp;";else if(c==L'<')out+=L"&lt;";else if(c==L'>')out+=L"&gt;";else out+=c;}return out;}
struct Wav {WAVEFORMATEX format{};std::vector<char> pcm;double seconds=0;std::vector<std::pair<double,std::size_t>> cues;};
void gain(Wav& audio,int volume){
    if(volume==100)return;for(size_t at=0;at<audio.pcm.size();at+=audio.format.wBitsPerSample/8){
        if(audio.format.wBitsPerSample==16){int16_t value;std::memcpy(&value,audio.pcm.data()+at,2);value=static_cast<int16_t>(static_cast<int32_t>(value)*volume/100);std::memcpy(audio.pcm.data()+at,&value,2);}
        else if(audio.format.wFormatTag==WAVE_FORMAT_PCM){int32_t value;std::memcpy(&value,audio.pcm.data()+at,4);value=static_cast<int32_t>(static_cast<int64_t>(value)*volume/100);std::memcpy(audio.pcm.data()+at,&value,4);}
        else{float value;std::memcpy(&value,audio.pcm.data()+at,4);value=std::isfinite(value)?std::clamp(value*volume/100.0f,-1.0f,1.0f):0;std::memcpy(audio.pcm.data()+at,&value,4);}
    }
}
bool wav(const std::string& bytes,Wav& out){if(bytes.size()<12||bytes.compare(0,4,"RIFF")||bytes.compare(8,4,"WAVE"))return false;auto u32=[&](size_t p){uint32_t value=0;std::memcpy(&value,bytes.data()+p,4);return value;};bool fmt=false,data=false;for(size_t at=12;at+8<=bytes.size();){const auto size=u32(at+4);if(size>bytes.size()-at-8)return false;if(bytes.compare(at,4,"fmt ")==0){if(size<16)return false;std::memcpy(&out.format,bytes.data()+at+8,16);out.format.cbSize=0;fmt=true;}if(bytes.compare(at,4,"aytm")==0){try{auto timing=PluginJson::parse(bytes.substr(at+8,size));if(timing.at("version")!=1||!timing.at("cues").is_array()||timing.at("cues").size()>6500)return false;double lastTime=-1;std::size_t lastCharacter=0;for(const auto& cue:timing.at("cues")){const double t=cue.at(0);const auto n=cue.at(1).get<std::size_t>();if(!std::isfinite(t)||t<lastTime||n<lastCharacter||n>6000)return false;out.cues.emplace_back(t,n);lastTime=t;lastCharacter=n;}}catch(...){return false;}}if(bytes.compare(at,4,"data")==0){out.pcm.assign(bytes.begin()+at+8,bytes.begin()+at+8+size);data=true;}at+=8+size+(size&1);}
    if(!fmt||!data||out.format.nChannels<1||out.format.nChannels>2||out.format.nSamplesPerSec<8000||out.format.nSamplesPerSec>96000||(out.format.wBitsPerSample!=16&&out.format.wBitsPerSample!=32)||(out.format.wFormatTag!=WAVE_FORMAT_PCM&&out.format.wFormatTag!=WAVE_FORMAT_IEEE_FLOAT)||(out.format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT&&out.format.wBitsPerSample!=32)||out.format.nBlockAlign!=out.format.nChannels*out.format.wBitsPerSample/8||out.pcm.size()%out.format.nBlockAlign)return false;out.format.nAvgBytesPerSec=out.format.nSamplesPerSec*out.format.nBlockAlign;out.seconds=static_cast<double>(out.pcm.size())/out.format.nAvgBytesPerSec;return out.seconds<=300;
}
#include "QueuedSpeech.h"
class Speech final:public BasePlugin {
    struct Command {std::string text,id;double maxSeconds=60;unsigned generation=0;std::shared_ptr<std::promise<PluginJson>> result;std::string replyId;};
    PluginHostContext host_;PluginJson cfg_;mutable std::mutex mutex_;std::condition_variable cv_;std::deque<Command> commands_;std::thread thread_;KanaSynthesizer kana_;std::atomic<bool> synthesizing_=false,paused_=false;std::atomic<bool> stop_=false,active_=false;std::atomic<unsigned> generation_=0;std::string voiceName_,lastError_;double lastDuration_=0,lastSourcePeak_=0,lastSynthesisMs_=0;unsigned startedCount_=0,completedCount_=0,lastSegments_=0,lastBuffered_=0,lastUnderruns_=0;bool synthesisComplete_=true;
    std::string nextId(){static std::atomic<unsigned> next=0;return "speech-"+std::to_string(nowMs())+"-"+std::to_string(next++);}
    void queue(Command cmd){std::lock_guard lock(mutex_);if(commands_.size()>=8){if(cmd.result)cmd.result->set_value(toolFailure("speech_queue_full"));host_.emit({{"kind","speech.failed"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId}});return;}commands_.push_back(std::move(cmd));cv_.notify_one();}
    void speakKana(const Command& cmd,const PluginJson& cfg,const std::filesystem::path& root,const std::function<void(PluginJson)>& emit,const std::function<bool()>& cancelled){
        synthesizing_=true;{std::lock_guard lock(mutex_);lastSegments_=lastBuffered_=lastUnderruns_=0;synthesisComplete_=false;}
        const auto result=playQueuedSpeech(cmd.text,cmd.maxSeconds,cfg.value("volume",75),cancelled,
            [&](const std::string& part,double maximum,const std::function<bool()>& stopped)->std::optional<Wav>{
                bool silent=false;const auto bytes=kana_.render({{"version",1},{"text",part},{"speed",cfg.value("speed",1.0)},{"max_seconds",maximum}},root,stopped,&silent);if(silent)return Wav{};Wav audio;
                if(!wav(bytes,audio)||audio.format.nChannels!=1||audio.format.wBitsPerSample!=16||audio.format.nSamplesPerSec!=24000)return std::nullopt;return audio;
            },
            [&](double duration,double milliseconds,double peak){
                synthesizing_=false;active_=true;{std::lock_guard lock(mutex_);++startedCount_;lastError_.clear();lastSynthesisMs_=milliseconds;lastSourcePeak_=peak;}
                emit({{"kind","speech.started"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"text",cmd.text},{"maximum_seconds",cmd.maxSeconds},{"estimated_seconds",duration},{"duration_complete",false},{"streaming",true}});
                if(cmd.result)cmd.result->set_value(toolSuccess({{"speech_id",cmd.id},{"started",true},{"estimated_seconds",duration},{"duration_complete",false},{"maximum_seconds",cmd.maxSeconds},{"synthesis_ms",milliseconds},{"streaming",true}}));
            },
            [&](double seconds,double duration,std::size_t visible,std::size_t total,unsigned segments,unsigned buffered,bool complete){{std::lock_guard lock(mutex_);lastSegments_=segments;lastBuffered_=buffered;synthesisComplete_=complete;}emit({{"kind","speech.progress"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"position_seconds",seconds},{"duration_seconds",duration},{"visible_characters",visible},{"total_characters",total}});});
        synthesizing_=false;active_=false;if(paused_||stop_)kana_.close();
        {std::lock_guard lock(mutex_);lastSegments_=result.segments;lastBuffered_=result.maximumBuffered;lastUnderruns_=result.underruns;lastSourcePeak_=result.peak;if(result.started){lastDuration_=result.elapsed;++completedCount_;}if(!result.started||result.failed)lastError_="speech_unavailable";}
        if(!result.started&&cmd.result)cmd.result->set_value(toolFailure(cancelled()?"cancelled":"speech_unavailable",true));
        if(!result.started||result.failed)emit({{"kind","speech.failed"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"interrupted",cancelled()}});
        else emit({{"kind","speech.ended"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"duration_seconds",result.elapsed},{"interrupted",cancelled()},{"time_limit",result.limited}});
    }
    void work(){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ISpVoice* voice=nullptr;CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,reinterpret_cast<void**>(&voice));
        if(voice){ISpObjectToken* token=nullptr;if(SUCCEEDED(SpFindBestToken(SPCAT_VOICES,L"Language=804",nullptr,&token))&&token){voice->SetVoice(token);token->Release();}ISpObjectToken* current=nullptr;if(SUCCEEDED(voice->GetVoice(&current))&&current){wchar_t* name=nullptr;if(SUCCEEDED(SpGetDescription(current,&name))&&name){const auto n=WideCharToMultiByte(CP_UTF8,0,name,-1,nullptr,0,nullptr,nullptr);std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,name,-1,text.data(),n,nullptr,nullptr);if(!text.empty())text.pop_back();{std::lock_guard lock(mutex_);voiceName_=std::move(text);}CoTaskMemFree(name);}current->Release();}}
        {PluginJson cfg;std::filesystem::path root;{std::lock_guard lock(mutex_);cfg=cfg_;root=host_.dataRoot;}if(cfg.value("backend",std::string("system"))=="kana")kana_.warm(root,[&]{return stop_.load();});}
        while(!stop_){Command cmd;PluginJson cfg;std::filesystem::path dataRoot;std::function<void(PluginJson)> emit;{std::unique_lock lock(mutex_);cv_.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop_||!commands_.empty();});if(stop_)break;if(commands_.empty())continue;cmd=std::move(commands_.front());commands_.pop_front();cfg=cfg_;emit=host_.emit;dataRoot=host_.dataRoot;}
            if(cmd.generation!=generation_){if(cmd.result)cmd.result->set_value(toolFailure("speech_superseded"));emit({{"kind","speech.failed"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"interrupted",true}});continue;}bool started=false;double duration=0;HWAVEOUT output=nullptr;WAVEHDR header{};Wav audio;
            auto cancelled=[&]{return stop_.load()||cmd.generation!=generation_.load();};
            if(cfg.value("backend",std::string("system"))=="kana"){speakKana(cmd,cfg,dataRoot,emit,cancelled);continue;}
            synthesizing_=true;const auto synthesisStart=std::chrono::steady_clock::now();
            if(cfg.value("backend",std::string("system"))=="http"){
                ayt::net::http::Request request;request.useSystemProxy=cfg.value("useSystemProxy",true);request.proxyOrigin=cfg.value("proxyOrigin",std::string{});request.origin=cfg.value("origin",std::string("http://127.0.0.1:5000"));request.path=cfg.value("path",std::string("/v1/audio/speech"));request.timeout=std::chrono::seconds(15);request.maxResponseBytes=8*1024*1024;request.cancelled=cancelled;request.headers={{"Content-Type","application/json"}};const auto key=ayt::net::http::environmentCredential(cfg.value("credentialEnvironment",std::string{}));if(!key.empty())request.headers.emplace_back("Authorization","Bearer "+key);
                request.body=PluginJson{{"model",cfg.value("model",std::string("tts"))},{"input",cmd.text},{"voice",cfg.value("voice",std::string("default"))},{"response_format","wav"},{"speed",std::clamp(1.0+cfg.value("rate",5)*0.1,0.25,4.0)}}.dump();const auto result=ayt::net::http::perform(request);
                if(!(result&&result.httpStatus==200&&wav(result.body,audio)&&!cancelled()))audio.pcm.clear();
            }else if(voice&&!cancelled()){
                const auto text=L"<pitch middle=\"+6\">"+xmlEscape(wide(cmd.text))+L"</pitch>";if(!renderSpeech(voice,text,cfg.value("rate",5),cancelled,audio.format,audio.pcm))audio.pcm.clear();
            }
            const auto synthesisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-synthesisStart).count();synthesizing_=false;{std::lock_guard lock(mutex_);lastSynthesisMs_=synthesisMs;}
            if(!audio.pcm.empty()&&!cancelled()){
                double peak=0;for(std::size_t at=0;at<audio.pcm.size();at+=audio.format.wBitsPerSample/8){double sample=0;if(audio.format.wBitsPerSample==16){int16_t x;std::memcpy(&x,audio.pcm.data()+at,2);sample=x/32768.0;}else if(audio.format.wFormatTag==WAVE_FORMAT_PCM){int32_t x;std::memcpy(&x,audio.pcm.data()+at,4);sample=x/2147483648.0;}else{float x;std::memcpy(&x,audio.pcm.data()+at,4);sample=std::isfinite(x)?x:0;}peak=std::max(peak,std::abs(sample));}{std::lock_guard lock(mutex_);lastSourcePeak_=peak;}
                const auto maximum=static_cast<size_t>(cmd.maxSeconds*audio.format.nSamplesPerSec)*audio.format.nBlockAlign;if(audio.pcm.size()>maximum)audio.pcm.resize(maximum);duration=static_cast<double>(audio.pcm.size())/audio.format.nAvgBytesPerSec;gain(audio,cfg.value("volume",75));
                if(waveOutOpen(&output,WAVE_MAPPER,&audio.format,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR){header.lpData=audio.pcm.data();header.dwBufferLength=static_cast<DWORD>(audio.pcm.size());if(waveOutPrepareHeader(output,&header,sizeof(header))==MMSYSERR_NOERROR&&waveOutWrite(output,&header,sizeof(header))==MMSYSERR_NOERROR)started=true;}
            }
            {std::lock_guard lock(mutex_);if(started){active_=true;++startedCount_;lastError_.clear();}else lastError_="speech_unavailable";}
            if(cmd.result)cmd.result->set_value(started?toolSuccess({{"speech_id",cmd.id},{"started",true},{"estimated_seconds",duration},{"maximum_seconds",cmd.maxSeconds},{"synthesis_ms",synthesisMs}}):toolFailure(cancelled()?"cancelled":"speech_unavailable",true));
            if(!started){if(output){waveOutReset(output);if(header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(output,&header,sizeof(header));waveOutClose(output);}emit({{"kind","speech.failed"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId}});continue;}
            active_=true;const auto speakingStart=std::chrono::steady_clock::now();emit({{"kind","speech.started"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"text",cmd.text},{"maximum_seconds",cmd.maxSeconds},{"estimated_seconds",duration}});
                        const auto totalCharacters=textOffsets(cmd.text).size()-1;double lastPosition=0;auto nextProgress=speakingStart;
            auto progress=[&](bool finished){MMTIME position{};position.wType=TIME_SAMPLES;double seconds=lastPosition;if(finished)seconds=duration;else if(waveOutGetPosition(output,&position,sizeof(position))==MMSYSERR_NOERROR){if(position.wType==TIME_SAMPLES)seconds=static_cast<double>(position.u.sample)/audio.format.nSamplesPerSec;else if(position.wType==TIME_BYTES)seconds=static_cast<double>(position.u.cb)/audio.format.nAvgBytesPerSec;else if(position.wType==TIME_MS)seconds=position.u.ms/1000.0;}seconds=std::clamp(seconds,lastPosition,duration);lastPosition=seconds;std::size_t visible=0;
                if(audio.cues.empty())visible=duration>0?static_cast<std::size_t>(totalCharacters*seconds/duration):totalCharacters;
                else{double previous=0;std::size_t before=0;for(const auto& [at,characters]:audio.cues){if(seconds>=at){visible=characters;previous=at;before=characters;continue;}if(at>previous)visible=before+static_cast<std::size_t>((characters-before)*(seconds-previous)/(at-previous));break;}}
                emit({{"kind","speech.progress"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"position_seconds",seconds},{"duration_seconds",duration},{"visible_characters",std::min(totalCharacters,visible)},{"total_characters",totalCharacters}});
            };
            while(!cancelled()&&std::chrono::steady_clock::now()-speakingStart<std::chrono::duration<double>(cmd.maxSeconds)){const bool done=(header.dwFlags&WHDR_DONE)!=0;const auto now=std::chrono::steady_clock::now();if(done||now>=nextProgress){progress(done);nextProgress=now+std::chrono::milliseconds(40);}if(done)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            if(output){waveOutReset(output);waveOutUnprepareHeader(output,&header,sizeof(header));waveOutClose(output);}else if(voice)voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);active_=false;
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-speakingStart).count();{std::lock_guard lock(mutex_);lastDuration_=elapsed;++completedCount_;}emit({{"kind","speech.ended"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"duration_seconds",elapsed},{"interrupted",cancelled()},{"time_limit",elapsed>=cmd.maxSeconds-0.025}});
        }
        kana_.close();if(voice){voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);voice->Release();}if(SUCCEEDED(com))CoUninitialize();
        std::lock_guard lock(mutex_);for(auto& cmd:commands_){if(cmd.result)cmd.result->set_value(toolFailure("speech_stopped"));host_.emit({{"kind","speech.failed"},{"plugin","tts"},{"speech_id",cmd.id},{"reply_id",cmd.replyId},{"interrupted",true}});}commands_.clear();
    }
public:
    const char* getName()const override{return "tts";}const char* getDescription()const override{return "Bounded local SAPI or HTTP WAV speech";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{try{configurationTypes(cfg,{"backend","origin","path","model","voice","credentialEnvironment","proxyOrigin"},{"autoSpeak","useSystemProxy","respectModelSpeech"});const auto backend=cfg.value("backend",std::string("system"));if(backend!="system"&&backend!="http"&&backend!="kana")throw std::runtime_error("backend");number(cfg,"speed",1.0,0.5,2.0);if(backend=="kana"){(void)kanaRuntime();}number(cfg,"rate",5,-10,10);number(cfg,"volume",75,0,100);number(cfg,"maxSeconds",60,1,120);
        if(backend=="http"){const auto origin=cfg.value("origin",std::string("http://127.0.0.1:5000"));if(!ayt::net::http::validOrigin(origin)||(!origin.starts_with("https://")&&!origin.starts_with("http://127.0.0.1:")&&!origin.starts_with("http://localhost:")))throw std::runtime_error("origin");}
        {std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);}if(!thread_.joinable()){stop_=false;thread_=std::thread([this]{work();});}issue.clear();return true;}catch(...){issue="speech_configuration";return false;}}
    PluginJson tools()const override{return PluginJson::array({tool("speech.speak","朗读文字；实际启动后返回结果，并限制最大朗读时长。",object({{"text",{{"type","string"},{"minLength",1},{"maxLength",6000}}},{"max_seconds",{{"type","number"},{"minimum",1},{"maximum",120}}}},PluginJson::array({"text"}))),tool("speech.stop","中断当前朗读。",object()),tool("speech.status","读取语音播放状态。",object())});}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{if(name=="speech.stop"){++generation_;return toolSuccess({{"stopping",true}});}if(name=="speech.status"){std::lock_guard lock(mutex_);return toolSuccess({{"active",active_.load()},{"voice",cfg_.value("backend",std::string("system"))=="kana"?"Open JTalk / Mei · 中文假名":cfg_.value("backend",std::string("system"))=="http"?cfg_.value("voice",std::string("default")):voiceName_},{"backend",cfg_.value("backend",std::string("system"))},{"speed",cfg_.value("speed",1.0)},{"synthesis_complete",synthesisComplete_},{"last_stream_segments",lastSegments_},{"maximum_buffered_segments",lastBuffered_},{"last_underruns",lastUnderruns_},{"last_synthesis_ms",lastSynthesisMs_},{"synthesizing",synthesizing_.load()},{"kana_worker_starts",kana_.starts()},{"kana_worker_running",kana_.running()},{"last_duration_seconds",lastDuration_},{"started_count",startedCount_},{"completed_count",completedCount_},{"queued",commands_.size()},{"last_error",lastError_},{"volume",cfg_.value("volume",75)},{"source_peak",lastSourcePeak_},{"playback_path","wave-mapper"}});}
        auto promise=std::make_shared<std::promise<PluginJson>>();auto future=promise->get_future();double maximum;{std::lock_guard lock(mutex_);maximum=std::min(cfg_.value("maxSeconds",60.0),args.value("max_seconds",120.0));}const auto generation=++generation_;queue({args.at("text"),nextId(),maximum,generation,promise});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(18);while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready){if((cancelled&&cancelled())||std::chrono::steady_clock::now()>deadline){++generation_;return toolFailure("speech_cancelled_or_timeout",true);}}return future.get();
    }
    void event(const PluginJson& event)override{if(event.value("kind","")=="runtime.paused"){paused_=event.value("paused",false);if(paused_)++generation_;return;}if(event.value("kind","")!="assistant.reply")return;PluginJson cfg;{std::lock_guard lock(mutex_);cfg=cfg_;}if(!cfg.value("autoSpeak",true)||(cfg.value("respectModelSpeech",false)&&!event.value("presentation",PluginJson::object()).value("speech",true)))return;const auto generation=generation_.load();queue({event.at("text"),nextId(),cfg.value("maxSeconds",60.0),generation,{},event.value("reply_id",std::string{})});}
    PluginJson snapshot()const override{return PluginJson::object();}
    void shutdown()override{stop_=true;++generation_;cv_.notify_all();if(thread_.joinable())thread_.join();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Speech;}
