#include "../PluginCommon.h"
#include <AYPlatform/ChildProcess.h>
#include <fstream>
#include <atomic>
#include <set>
#include <thread>
#include "WatchSession.h"
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
int moduleAnchor;
std::filesystem::path pluginDirectory(){HMODULE m{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&moduleAnchor),&m))throw std::runtime_error("plugin_path");wchar_t p[32768]{};if(!GetModuleFileNameW(m,p,32768))throw std::runtime_error("plugin_path");return std::filesystem::path(p).parent_path();}
class VideoAnalysis final:public BasePlugin {
    PluginHostContext host_;PluginJson cfg_;std::filesystem::path directory_;std::atomic<bool> stopped_{false};std::mutex running_;std::atomic<unsigned> serial_{0};
    mutable std::mutex stateMutex_;video::WatchSession watch_;
    void emit(PluginJson event){event["plugin"]="video-analysis";if(host_.emit)host_.emit(std::move(event));}
    std::string identifier(const char* prefix){return std::string(prefix)+"-"+std::to_string(nowMs())+"-"+std::to_string(serial_++);}
    // Business permission boundary: only the two app-provided material mounts.
    std::filesystem::path source(const std::string& mount,const std::string& relative,bool subtitle=false)const{
        auto root=mount=="materials"?host_.materialsRoot:mount=="drafts"?host_.draftsRoot:std::filesystem::path{};
        if(root.empty())root=mount=="materials"?host_.dataRoot/"materials":mount=="drafts"?host_.dataRoot/"drafts":std::filesystem::path{};
        auto rel=std::filesystem::u8path(relative);
        if(root.empty()||relative.empty()||relative.size()>1024||relative.find(':')!=relative.npos||relative.find('\\')!=relative.npos||relative.find('\0')!=relative.npos||rel.is_absolute())throw std::runtime_error("video_source_denied");
        for(const auto& part:rel)if(part==L".."||part==L".")throw std::runtime_error("video_source_denied");
        auto base=std::filesystem::canonical(root),file=base/rel;
        // No reparse points may redirect a media reference to private archives.
        for(auto p=file;!p.empty();p=p.parent_path()){auto a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("video_source_denied");if(p==base)break;}
        if(!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>(subtitle?2ull*1024*1024:20ull*1024*1024*1024))throw std::runtime_error("video_source_denied");
        auto ext=file.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
        const std::set<std::wstring> videos={L".mp4",L".mkv",L".webm",L".mov",L".m4v",L".avi"},subs={L".srt",L".json"};
        if(!(subtitle?subs:videos).contains(ext))throw std::runtime_error("video_source_denied");if(!subtitle){std::ifstream f(file,std::ios::binary);char b[12]{};f.read(b,12);if(f.gcount()!=12||!(std::string(b+4,4)=="ftyp"||std::string(b,4)==std::string("\x1a\x45\xdf\xa3",4)||(std::string(b,4)=="RIFF"&&std::string(b+8,4)=="AVI ")))throw std::runtime_error("video_source_denied");}return file;
    }
    PluginJson webObserve(const PluginJson& args,const std::function<bool()>& cancelled){
        if(!host_.browser)return toolFailure("webview_unavailable");
        const int count=static_cast<int>(number(args,"frames",2,1,3)),spacing=static_cast<int>(number(args,"spacing_ms",800,250,1200));
        PluginJson frames=PluginJson::array(),first,lastVideo;std::string url,mediaId;double previous=0;auto capturedAt=std::chrono::steady_clock::now();
        for(int i=0;i<count;++i){
            if(i){const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(spacing);while(std::chrono::steady_clock::now()<until){if(stopped_||(cancelled&&cancelled()))return toolFailure("cancelled");std::this_thread::sleep_for(std::chrono::milliseconds(20));}}
            if(stopped_||(cancelled&&cancelled()))return toolFailure("cancelled");
            const auto observed=host_.browser("video_frame",PluginJson::object(),cancelled);if(!observed.value("ok",false))return observed;
            auto value=observed.at("result");const auto metadata=value.value("video",PluginJson::object());const auto now=std::chrono::steady_clock::now();const double position=value.at("frame_time_seconds");
            if(!i){first=value;url=value.at("source_url");mediaId=metadata.value("media_id",std::string{});}else{
                const auto elapsed=std::chrono::duration<double>(now-capturedAt).count();
                if(value.value("source_url",std::string{})!=url||metadata.value("media_id",std::string{})!=mediaId||position<previous-0.06||position-previous>elapsed*1.5+0.75)return toolFailure("web_video_changed_during_observation",true);
            }
            lastVideo=metadata;frames.push_back({{"index",i},{"frame_time_seconds",position},{"sampled_at_ms",value.value("sampled_at_ms",nowMs())},{"width",value.at("width")},{"height",value.at("height")},{"image",value.at("image")}});previous=position;capturedAt=now;
            if(metadata.value("paused",false))break;
        }
        return toolSuccess({{"scope","current_web_video_frames"},{"source_url",url},{"title",first.value("title",std::string{})},{"video",lastVideo},{"frames",frames},{"sampled_frames",frames.size()},{"observation_kind","video_element_sequence"},{"screenshot_used",false},{"audio_understood",false},{"audio_delivered",false},{"full_video_watched",false},{"perception_note","当前视频元素的抽样图像，各帧带实际时间码。视觉与音频输入范围见结构字段。"}});
    }
public:
    const char* getDescription()const override{return "Bounded local and web continuous video observations";}
    const char* getName()const override{return "video-analysis";}
    void shutdown()override{stopped_=true;}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& error)override{
        try{configurationTypes(cfg,{"nodeExecutable","ffmpegExecutable","ffprobeExecutable","asrEndpoint","asrModel","asrKeyEnv","asrLanguage"},{"asrEnabled"});number(cfg,"watchWindowSeconds",30,10,120);host_=std::move(host);cfg_=std::move(cfg);directory_=pluginDirectory();return true;}catch(...){error="video_configuration_invalid";return false;}
    }
    PluginJson tools()const override{
        auto properties=PluginJson{{"mount",{{"type","string"},{"enum",{"materials","drafts"}}}},{"path",{{"type","string"},{"minLength",1},{"maxLength",1024}}},{"from",{{"type","number"},{"minimum",0},{"maximum",86400}}},{"to",{{"type","number"},{"minimum",0},{"maximum",86400}}},{"transcript_path",{{"type","string"},{"maxLength",1024}}}};
        auto list=PluginJson::array();for(const auto* name:{"video.probe","video.storyboard","video.zoom","video.locate"}){auto p=properties;auto required=PluginJson::array({"mount","path"});if(std::string(name)=="video.locate"){p["query"]={{"type","string"},{"minLength",1},{"maxLength",200}};required.push_back("query");}list.push_back(tool(name,std::string(name)=="video.probe"?"探测工作区本地视频信息，不生成图片。":"分析本地视频指定时间段（默认30秒，最多120秒），返回有时间码的抽样分镜/字幕。zoom看细节；locate仅检索字幕原文。先probe再选时间窗；这不是连续看完。陪看期间只可分析已实际播放的范围，不可抢看后续。网页导航使用television.page，当前网页视频用video.web_observe；不能传网址或浏览器账号给本地分析工具。",object(p,required)));}
        list.push_back(tool("video.web_observe","看一眼小电视网站当前视频，采样1–3帧；这次调用结束就停止观察。想继续看、陪看或稍后跟进，应另用video.watch(action=start,source=webpage)建立持续陪看；程序会按播放进度唤醒。网站浏览先television.page读取HTML，再open_page打开已返回链接。跨域或受保护帧不可取时返回原因。",object({{"frames",{{"type","integer"},{"minimum",1},{"maximum",3}}},{"spacing_ms",{{"type","integer"},{"minimum",250},{"maximum",1200}}}})));
        list.push_back(tool("video.watch","建立持续陪看任务，不要只口头答应后就结束。网站已在小电视播放时用action=start,source=webpage（不填mount/path）；本地片用source=local并给mount/path。程序按实际播放进度定期唤醒，届时watch_next获取新画面并可note留笔记，可以安静观看不必每段发言。pause/resume/stop/status管理陪看，电视播放用television.control；暂停、换片、关闭或取帧失败会暂停推进，重启需要恢复。网站是间隔抽样，不代表完整看过或听懂音轨。",object({{"action",{{"enum",{"start","pause","resume","stop","status"}}}},{"source",{{"enum",{"local","webpage"}}}},{"mount",properties.at("mount")},{"path",properties.at("path")},{"transcript_path",properties.at("transcript_path")}},PluginJson::array({"action"}))));
        list.push_back(tool("video.watch_next","持续陪看被唤醒后取下一次观察：本地返回已播放时间段分镜/可用字幕；网页返回此刻新采的视频帧和真实时间码回执。不是重新搜索或重新打开影片。未到时间返回not_ready，不循环轮询、不跳播；可以安静看，取得watch_receipt后用video.note留下有来源笔记。",object()));
        list.push_back(tool("video.note","为实际取得的陪看分镜写有来源的笔记，角色/事件、疑问和感想分别分类。receipt_id取工具实际回执；不把猜想写成事件。笔记跨会话保存。",object({{"receipt_id",{{"type","string"},{"maxLength",100}}},{"category",{{"enum",{"character","event","question","impression"}}}},{"text",{{"type","string"},{"minLength",1},{"maxLength",400}}}},PluginJson::array({"receipt_id","category","text"}))));
        list.push_back(tool("video.notes","读取已存的观看笔记及其片源、时间码；可选mount/path过滤。笔记不是整片理解，也不改变播放状态。",object({{"mount",properties.at("mount")},{"path",properties.at("path")}})));
        return list;
    }
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(stopped_||(cancelled&&cancelled()))return toolFailure("cancelled");
        std::filesystem::path scratch;
        bool nextWindow=false,watchBound=false;double observedFrom=0;std::uint64_t watchEpoch=0;PluginJson window;
        const auto failProcessing=[&](const char* code,bool retryable=false){if(nextWindow){std::lock_guard guard(stateMutex_);if(watch_.epoch==watchEpoch){watch_.failed();emit({{"kind","video.watch_changed"},{"session",watch_.state}});}}return toolFailure(code,retryable);};
        try{
            if(name=="video.watch"||name=="video.note"||name=="video.notes"){
                std::lock_guard guard(stateMutex_);
                if(name=="video.notes"){
                    auto rows=PluginJson::array();for(const auto& n:watch_.notes)if((!args.contains("mount")||n.at("source").at("mount")==args.at("mount"))&&(!args.contains("path")||n.at("source").at("path")==args.at("path")))rows.push_back(n);
                    return toolSuccess({{"notes",rows},{"retained_limit",64},{"originals_preserved",true}});
                }
                if(name=="video.note"){
                    const auto note=watch_.addNote(args.at("receipt_id"),args.at("category"),args.at("text"),identifier("note"));if(note.is_null())return toolFailure("video_note_source_invalid");
                    emit({{"kind","video.note_saved"},{"note",note}});return toolSuccess({{"note",note},{"saved",true}});
                }
                const auto action=args.at("action").get<std::string>();
                if(action=="start"&&(args.value("source",std::string{})=="webpage"||(!args.contains("mount")&&watch_.playback.value("mode",std::string{})=="webpage"))){
                    if(!watch_.startWeb(identifier("watch"),number(cfg_,"watchWindowSeconds",30,10,120),nowMs()))return toolFailure("video_watch_requires_ready_web_playback",true);
                }else if(action=="start"){
                    const auto mount=args.at("mount").get<std::string>(),path=args.at("path").get<std::string>();const auto input=source(mount,path);
                    if(args.contains("transcript_path"))source(mount,args.at("transcript_path"),true);
                    if(!watch_.start(mount,path,pathUtf8(input),identifier("watch"),number(cfg_,"watchWindowSeconds",30,10,120),nowMs()))return toolFailure("video_watch_requires_matching_local_playback",true);
                    if(args.contains("transcript_path"))watch_.state["transcript_path"]=args.at("transcript_path");
                }else if(action=="resume"){
                    if(!watch_.exists())return toolFailure("video_watch_missing");
                    watch_.boundSource=watch_.web()?watch_.state.at("path").get<std::string>():pathUtf8(source(watch_.state.at("mount"),watch_.state.at("path")));
                    if(!watch_.resume(nowMs()))return toolFailure(watch_.web()?"video_watch_requires_matching_web_playback":"video_watch_requires_matching_local_playback",true);
                }else if(action=="pause"||action=="stop")watch_.control(action);
                if(action!="status")emit({{"kind","video.watch_changed"},{"session",watch_.state}});
                return toolSuccess(watch_.snapshot());
            }
            std::unique_lock mediaLock(running_,std::try_to_lock);if(!mediaLock.owns_lock())return toolFailure("video_busy",true);
            if(name=="video.web_observe")return webObserve(args,cancelled);
            auto inputArgs=args;
            if(name=="video.watch_next"){
                bool web=false;
                {std::lock_guard guard(stateMutex_);window=watch_.next(nowMs());if(window.is_null())return toolFailure("video_watch_not_ready");inputArgs=window;web=watch_.web();watchEpoch=watch_.epoch;
                if(watch_.state.contains("transcript_path"))inputArgs["transcript_path"]=watch_.state.at("transcript_path");nextWindow=true;}
                if(web){
                    auto result=webObserve({{"frames",2},{"spacing_ms",800}},[&]{if(cancelled&&cancelled())return true;std::lock_guard guard(stateMutex_);return watch_.epoch!=watchEpoch;});
                    if(!result.value("ok",false))return failProcessing(result.value("error",PluginJson::object()).value("code",std::string("video_web_observation_failed")).c_str(),true);
                    std::lock_guard guard(stateMutex_);if(watch_.epoch!=watchEpoch||!watch_.active())return toolFailure("video_watch_interrupted");
                    const auto& viewed=result.at("result");const auto& frames=viewed.at("frames");const auto& v=viewed.at("video");
                    watch_.observe({{"sampled_at_ms",frames.back().at("sampled_at_ms")},{"mode","webpage"},{"source",viewed.at("source_url")},{"media_id",v.at("media_id")},{"state",v.value("ended",false)?"ended":v.value("paused",false)?"paused":"playing"},{"position_seconds",frames.back().at("frame_time_seconds")},{"playback_rate",v.value("playback_rate",1.0)},{"seeking",v.value("seeking",false)}},nowMs());
                    // A web receipt covers the frames actually sampled now, never the unsampled interval.
                    const PluginJson sampled={{"mount","web"},{"path",viewed.at("source_url")},{"from",frames.front().at("frame_time_seconds")},{"to",frames.back().at("frame_time_seconds")}};
                    const auto receipt=watch_.commit(sampled,identifier("view"),watchEpoch,nowMs(),true);if(receipt.is_null())return toolFailure("video_watch_interrupted");
                    result["result"]["watch_receipt"]=receipt;emit({{"kind","video.window_observed"},{"receipt",receipt}});return result;
                }
            }
            if(name!="video.probe"&&name!="video.storyboard"&&name!="video.zoom"&&name!="video.locate"&&name!="video.watch_next")return toolFailure("tool_not_available");
            const auto mount=inputArgs.at("mount").get<std::string>(),relative=inputArgs.at("path").get<std::string>();auto input=source(mount,relative);
            auto node=std::filesystem::u8path(cfg_.value("nodeExecutable",std::string{})),ffmpeg=std::filesystem::u8path(cfg_.value("ffmpegExecutable",std::string{})),ffprobe=std::filesystem::u8path(cfg_.value("ffprobeExecutable",std::string{}));
            if(node.empty()||ffmpeg.empty()||ffprobe.empty()||!node.is_absolute()||!ffmpeg.is_absolute()||!ffprobe.is_absolute()||!std::filesystem::is_regular_file(node)||!std::filesystem::is_regular_file(ffmpeg)||!std::filesystem::is_regular_file(ffprobe))return toolFailure("video_dependencies_missing");
            const auto from=number(inputArgs,"from",0,0,86400),to=number(inputArgs,"to",from+30,0,86520);if(to<=from||to-from>120)return toolFailure("video_window_limit");
            {
                std::lock_guard guard(stateMutex_);watchBound=watch_.guarded();watchEpoch=watch_.epoch;
                if(watchBound&&!watch_.permits(mount,relative,from,to,nowMs()))return toolFailure("video_watch_outside_observed_progress");
                if(watchBound)observedFrom=watch_.state.at("coverage").at("from");
                window={{"mount",mount},{"path",relative},{"from",from},{"to",to}};
            }
            scratch=host_.dataRoot/"runtime"/"video-analysis"/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(nowMs())+"-"+std::to_string(serial_++));
            PluginJson request={{"tool",nextWindow?"video.storyboard":name},{"input",pathUtf8(input)},{"mount",mount},{"source_path",relative},{"from",from},{"to",to},{"scratch",pathUtf8(scratch)},{"ffmpeg",pathUtf8(ffmpeg)},{"ffprobe",pathUtf8(ffprobe)}};
            if(inputArgs.contains("transcript_path"))request["transcript"]=pathUtf8(source(mount,inputArgs.at("transcript_path"),true));
            request["strict_window"]=watchBound;request["observed_from"]=observedFrom;
            request["asr"]={{"enabled",cfg_.value("asrEnabled",false)},{"endpoint",cfg_.value("asrEndpoint",std::string{})},{"model",cfg_.value("asrModel",std::string{})},{"keyEnv",cfg_.value("asrKeyEnv",std::string{})},{"language",cfg_.value("asrLanguage",std::string("zh"))}};
            if(name=="video.locate"){auto q=args.at("query").get<std::string>();if(q.empty()||q.size()>800)return toolFailure("video_query_invalid");request["query"]=q;}
            ayt::platform::ProcessOptions options;options.executable=node;options.arguments={pathUtf8(directory_/"worker.mjs"),request.dump()};options.workingDirectory=directory_;options.timeout=std::chrono::seconds(60);options.outputLimit=4*1024*1024;options.cancellationRequested=[&]{if(stopped_||(cancelled&&cancelled()))return true;std::lock_guard guard(stateMutex_);return watchBound&&watch_.epoch!=watchEpoch;};
            // Engine process ownership also kills ffmpeg descendants on cancellation/timeout.
            auto run=ayt::platform::runProcess(options);std::error_code ec;std::filesystem::remove_all(scratch,ec);
            if(run.cancelled)return toolFailure(watchBound?"video_watch_interrupted":"cancelled");if(run.timedOut)return failProcessing("video_timeout",true);if(!run.launched||run.outputTruncated)return failProcessing("video_worker_failed");
            auto result=PluginJson::parse(run.output,nullptr,false);if(!result.is_object()||!result.contains("ok"))return failProcessing("video_output_invalid");
            if(watchBound){
                std::lock_guard guard(stateMutex_);
                if(!result.value("ok",false)){if(nextWindow){watch_.failed();emit({{"kind","video.watch_changed"},{"session",watch_.state}});}return result;}
                if(name=="video.storyboard"||name=="video.zoom"||nextWindow){
                    const auto receipt=watch_.commit(window,identifier("view"),watchEpoch,nowMs(),nextWindow);
                    if(receipt.is_null())return toolFailure("video_watch_interrupted");result["result"]["watch_receipt"]=receipt;
                    emit({{"kind","video.window_observed"},{"receipt",receipt}});
                }else if(watchEpoch!=watch_.epoch)return toolFailure("video_watch_interrupted");
            }
            return result;
        }catch(const std::exception& e){if(!scratch.empty()){std::error_code ec;std::filesystem::remove_all(scratch,ec);}const std::string code=e.what();return toolFailure(code=="video_source_denied"?"video_source_denied":"video_arguments_invalid");}
    }
    void event(const PluginJson& value)override{
        const auto kind=value.value("kind",std::string{});std::lock_guard guard(stateMutex_);
        if(kind=="runtime.television.playback"){
            const auto previousStatus=watch_.state.value("status",std::string{});watch_.observe(value.at("playback"),nowMs());
            if(previousStatus!=watch_.state.value("status",std::string{}))emit({{"kind","video.watch_changed"},{"session",watch_.state}});
        }else if(kind=="runtime.attention")watch_.attention(value.value("input_active",false),watch_.paused,value.value("busy",false));
        else if(kind=="runtime.paused")watch_.attention(watch_.input,value.value("paused",false),watch_.occupied);
    }
    void tick(double)override{
        std::lock_guard guard(stateMutex_);if(watch_.active()&&watch_.sampleAt&&nowMs()-watch_.sampleAt>3000){watch_.state["status"]="suspended";watch_.state["reason"]="telemetry_unavailable";watch_.invalidate();emit({{"kind","video.watch_changed"},{"session",watch_.state}});}
        const auto event=watch_.notice(nowMs());if(!event.is_null())emit(event);
    }
    PluginJson snapshot()const override{
        std::lock_guard guard(stateMutex_);return {{"scope","local_and_web_video_samples"},{"max_window_seconds",120},{"default_window_seconds",30},{"max_images",2},{"network_download",false},{"audio_recognition",cfg_.value("asrEnabled",false)},{"continuous_watching",true},{"web_continuous_watching",true},{"web_watch_audio",false},{"watch",watch_.snapshot()}};
    }
    bool restore(const PluginJson& value)override{std::lock_guard guard(stateMutex_);return value.is_object()&&(!value.contains("watch")||watch_.restore(value.at("watch")));}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new VideoAnalysis();}
extern "C" __declspec(dllexport) void DestroyPlugin(ayt::plugin::IPlugin* p){delete p;}
