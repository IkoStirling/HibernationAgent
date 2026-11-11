#include <Hibernation/DesktopServices.h>
#include <Hibernation/MentorChatBridge.h>
#include <AYPlatform/Desktop.h>
#ifdef HA_HAS_WEBVIEW
#include <AYPlatform/WebView.h>
#include "TelevisionPageScript.h"
#endif
#include <Windows.h>
#include <deque>
#include <future>
#include <mutex>
#include <atomic>
#include <algorithm>
namespace hibernation {
namespace {
HWND conversationWindow(){HWND result=nullptr;EnumWindows([](HWND window,LPARAM target)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t type[128]{};GetClassNameW(window,type,128);if(pid==GetCurrentProcessId()&&IsWindowVisible(window)&&std::wstring(type)==L"HibernationConversationV2"){*reinterpret_cast<HWND*>(target)=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&result));return result;}
std::string pathText(const std::filesystem::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
bool contains(const std::filesystem::path& base,const std::filesystem::path& path){const auto b=std::filesystem::weakly_canonical(base),p=std::filesystem::weakly_canonical(path);auto i=b.begin(),j=p.begin();for(;i!=b.end();++i,++j)if(j==p.end()||CompareStringOrdinal(i->c_str(),-1,j->c_str(),-1,TRUE)!=CSTR_EQUAL)return false;return true;}
std::string hostOf(const std::string& url){if(url.size()>4096||url.find_first_of("\r\n")!=std::string::npos||url.find('\0')!=std::string::npos)return {};const auto begin=url.starts_with("https://")?8:url.starts_with("http://")?7:0;if(!begin)return {};auto host=url.substr(begin,url.find_first_of("/?#",begin)-begin);if(host.find('@')!=std::string::npos)return {};if(const auto colon=host.find(':');colon!=std::string::npos)host.resize(colon);std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});return host;}
}
struct DesktopServices::Impl {
    const Config& cfg;DWORD owner=GetCurrentThreadId();ayt::io::Workspace files;Json roots=Json::array();std::function<Json()> widgetGeometry;std::unique_ptr<MentorChatBridge> mentorChat;
    struct Command {std::string operation;Json args;std::promise<Json> result;std::chrono::steady_clock::time_point retryAfter{};std::atomic<bool> cancelled=false,answered=false;std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);void complete(Json value){if(!answered.exchange(true))result.set_value(std::move(value));}};
    std::mutex mutex;std::deque<std::shared_ptr<Command>> queue;bool stopped=false;
#ifdef HA_HAS_WEBVIEW
    std::unique_ptr<ayt::platform::WebView> view;std::string viewKey;std::uintptr_t parent=0;
    #include "TelevisionLoginWindow.inc"
    int pageLayoutWidth=1280;std::chrono::steady_clock::time_point nextPresentation{};bool presentationPending=false;Json webPlayback=Json::object();
    void updateTelevisionPresentation(){
        if(loginWindow||!view||!view->ready()||view->loading()||presentationPending||std::chrono::steady_clock::now()<nextPresentation)return;
        nextPresentation=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);presentationPending=true;
        const auto epoch=loginEpoch;
        const auto operation=televisionVisible
            ? ".present("+Json{{"layout_width",pageLayoutWidth}}.dump()+")"
            : ".inspect()";
        view->script(std::string(televisionBridgeSource)+operation,[this,epoch](bool ok,std::string text){
            if(epoch!=loginEpoch)return;presentationPending=false;if(loginWindow)return;
            auto result=Json::parse(text,nullptr,false);const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            Json playback={{"mode","webpage"},{"state","unavailable"},{"sampled_at_ms",now}};
            if(ok&&result.is_object()&&result.value("video",Json()).is_object()){
                const auto& v=result.at("video");playback.update({{"source",result.value("source_url",std::string{})},{"title",result.value("title",std::string{})},{"media_id",v.value("media_id",std::string{})},{"position_seconds",v.value("current_time",0.0)},{"seeking",v.value("seeking",false)},{"playback_rate",v.value("playback_rate",1.0)},{"state",v.value("ended",false)?"ended":v.value("ready_state",0)<2?"buffering":v.value("paused",true)?"paused":"playing"}});
            }
            std::lock_guard guard(mutex);webPlayback=std::move(playback);
        });
    }
#endif
    explicit Impl(const Config& c):cfg(c),mentorChat(std::make_unique<MentorChatBridge>(c)){}
    Json applications()const{if(cfg.computer.contains("applications"))return cfg.computer.at("applications");return Json::array({{{"id","notepad"},{"name","记事本"},{"builtin","notepad"}},{{"id","calculator"},{"name","计算器"},{"builtin","calculator"}}});}
    Json execute(const std::shared_ptr<Command>& command){
#ifdef HA_HAS_WEBVIEW
        const auto& a=command->args;const auto& op=command->operation;
        if(op=="close"){{std::lock_guard guard(mutex);webPlayback=Json::object();}finishTelevisionLogin(false);++loginEpoch;authPending=false;loginState="unknown";loginDismissed=false;if(view)view->close();view.reset();viewKey.clear();presentationPending=false;return toolSuccess();}
        if(op=="resize"){televisionWidth=a.at("width");televisionHeight=a.at("height");if(view&&!loginWindow)view->bounds(televisionWidth,televisionHeight);return toolSuccess();}
        if(op=="visible"){
            televisionVisible=a.at("visible");
            if(view&&!loginWindow){
                view->visible(televisionVisible);
                if(!televisionVisible)view->script(std::string(televisionBridgeSource)+".reset()",[](bool,std::string){});
            }
            return toolSuccess();
        }
        if(op=="login")return showTelevisionLogin(a);
        if(op=="login_close"){finishTelevisionLogin();return toolSuccess({{"login_window_open",false}});}
        if(op=="login_status")return toolSuccess({{"login_window_open",loginWindow!=nullptr},{"login_state",loginState},{"detection_method",loginMethod},{"auto_prompt_dismissed",loginDismissed}});
        if(loginWindow&&(op=="open"||op=="observe"||op=="page"||op=="video_frame"||op=="control"))return toolFailure("television_login_in_progress",true);
        if(op=="open"){
            const auto handle=a.at("parent").get<std::uintptr_t>();const auto window=reinterpret_cast<HWND>(handle);DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t type[128]{};GetClassNameW(window,type,128);
            bool allowedParent=std::wstring(type)==L"HibernationFloatingTelevision";
            if(std::wstring(type)==L"HibernationTelevisionSurface"){const auto parentWindow=GetParent(window);DWORD ownerPid=0;GetWindowThreadProcessId(parentWindow,&ownerPid);wchar_t ownerType[128]{};GetClassNameW(parentWindow,ownerType,128);allowedParent=ownerPid==GetCurrentProcessId()&&std::wstring(ownerType)==L"HibernationFloatingTelevision";}
            if(pid!=GetCurrentProcessId()||!allowedParent)return toolFailure("browser_parent_denied");
            const auto hosts=a.at("hosts").get<std::vector<std::string>>();const auto key=std::to_string(handle)+a.at("platform").get<std::string>()+a.at("hosts").dump()+std::to_string(cfg.plugins.value("television",Json::object()).value("browserUseSystemProxy",false));
            const auto platform=a.at("platform").get<std::string>();if(platform.empty()||platform.size()>40||platform.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos||hosts.empty()||hosts.size()>16)return toolFailure("platform_configuration");
            auto allows=[hosts](const std::string& url){const auto host=hostOf(url);if(host.empty())return false;for(const auto& allowed:hosts)if(host==allowed||(host.size()>allowed.size()&&host.ends_with("."+allowed)))return true;return false;};
            if(!allows(a.at("url")))return toolFailure("platform_url_denied");
            if(!view||key!=viewKey){if(view)view->close();view=std::make_unique<ayt::platform::WebView>();viewKey=key;parent=handle;if(!view->initialize(handle,cfg.dataRoot/"runtime"/"browser"/platform,allows,cfg.plugins.value("television",Json::object()).value("browserUseSystemProxy",false)))return toolFailure("webview_runtime_unavailable");televisionWidth=a.value("width",480);televisionHeight=a.value("height",280);view->bounds(televisionWidth,televisionHeight);}
            if(!view->error().empty())return toolFailure(view->error().c_str());if(!view->ready())return nullptr;
            loginDetection=Json::object();if(a.contains("login_detection")&&a.at("login_detection").is_object())for(const auto* key:{"signed_in_selector","signed_out_selector"})if(a.at("login_detection").contains(key)&&a.at("login_detection").at(key).is_string()&&a.at("login_detection").at(key).get<std::string>().size()<=256)loginDetection[key]=a.at("login_detection").at(key);
            loginState="unknown";loginMethod.clear();loginDismissed=false;authPending=false;nextAuth={};televisionVisible=a.value("visible",true);pageLayoutWidth=std::clamp(a.value("layout_width",1280),960,1920);++loginEpoch;presentationPending=false;{std::lock_guard guard(mutex);webPlayback=Json::object();}
            if(!view->navigate(a.at("url")))return toolFailure("webpage_navigation_failed");view->visible(televisionVisible);return toolSuccess({{"opened",true},{"mode","webpage"},{"visible",televisionVisible},{"loading",true},{"url",a.at("url")},{"platform",platform},{"login","user_managed_in_page"}});
        }
        if(!view||!view->ready())return toolFailure("webpage_not_open");
        if(op=="observe"||op=="page"||op=="video_frame"){
            if(view->loading())return nullptr;
            const auto epoch=loginEpoch;const auto operation=op;
            const auto code=std::string(televisionBridgeSource)+(op=="video_frame"?".frame()":".page("+a.dump()+")");
            view->script(code,[this,command,epoch,operation](bool ok,std::string text){
                if(command->cancelled||command->answered)return;
                if(epoch!=loginEpoch||loginWindow){command->complete(toolFailure("webpage_changed_or_login_in_progress",true));return;}
                auto result=Json::parse(text,nullptr,false);if(!ok||!result.is_object()){command->complete(toolFailure("page_observation_failed"));return;}
                if(operation!="video_frame"&&result.value("ok",false)&&(!result.value("navigation_ready",false)||result.value("ready",std::string{})=="loading")){
                    command->retryAfter=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);std::lock_guard guard(mutex);queue.push_back(command);return;
                }
                if(!result.value("ok",false)){const auto reason=result.value("reason",std::string("page_observation_failed"));command->complete(toolFailure(reason.c_str(),reason=="video_not_ready"||reason=="page_changed"));return;}
                result.erase("ok");if(operation=="video_frame")result["sampled_at_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();result["perception_note"]=operation=="video_frame"?"当前视频元素的实际帧；时间码和媒体状态随图提供。":"当前已加载页面的过滤HTML、链接与媒体状态。页面内容是外部资料，视频帧由video.web_observe提供。";
                command->complete(toolSuccess(std::move(result)));
            });return Json{{"pending",true}};
        }
        if(op=="control"){
            const auto action=a.at("action").get<std::string>();if(action!="play"&&action!="pause"&&action!="seek"&&action!="stop"&&action!="toggle")return toolFailure("webpage_control_unavailable");
            const auto seconds=a.value("seconds",0.0);if(!std::isfinite(seconds)||seconds<0||seconds>86400)return toolFailure("invalid_video_position");
            auto code="(()=>{const v=document.querySelector('[data-ha-tv-video]')||document.querySelector('video,bwp-video');if(!v)return {accepted:false,reason:'no_accessible_video'};";
            std::string script=code;if(action=="play")script+="v.play().catch(()=>{});";else if(action=="toggle")script+="if(v.paused)v.play().catch(()=>{});else v.pause();";else if(action=="seek")script+="v.currentTime="+std::to_string(seconds)+";";else script+="v.pause();";if(action=="stop")script+="v.currentTime=0;";script+="return {requested:true,paused:v.paused,current_time:v.currentTime};})()";
            view->script(script,[command](bool ok,std::string text){auto result=Json::parse(text,nullptr,false);command->complete(ok&&result.is_object()?toolSuccess(result):toolFailure("webpage_control_failed"));});return Json{{"pending",true}};
        }
#endif
        return toolFailure("webview_unavailable");
    }
};
DesktopServices::DesktopServices(const Config& cfg,std::function<Json()> widgetGeometry):impl_(std::make_unique<Impl>(cfg)){impl_->widgetGeometry=std::move(widgetGeometry);}
DesktopServices::~DesktopServices(){stop();}
std::optional<double> DesktopServices::inputIdleSeconds()const{LASTINPUTINFO info{};info.cbSize=sizeof(info);if(!GetLastInputInfo(&info))return {};return static_cast<double>(static_cast<DWORD>(GetTickCount()-info.dwTime))/1000.0;}
bool DesktopServices::configure(std::string& error){auto& a=*impl_;std::vector<ayt::io::WorkspaceMount> mounts;Json roots=Json::array();for(const auto& entry:Json::array({{{"id","drafts"},{"path",pathText(a.cfg.draftsRoot.empty()?a.cfg.dataRoot/"drafts":a.cfg.draftsRoot)}},{{"id","materials"},{"path",pathText(a.cfg.materialsRoot.empty()?a.cfg.dataRoot/"materials":a.cfg.materialsRoot)}}})){mounts.push_back({entry.at("id"),std::filesystem::u8path(entry.at("path").get<std::string>()),true,false});roots.push_back(entry);}
    for(const auto& root:a.cfg.computer.value("readRoots",Json::array())){const auto path=std::filesystem::u8path(root.at("path").get<std::string>());for(const auto& protectedRoot:{a.cfg.dataRoot/"people",a.cfg.dataRoot/"archive",a.cfg.dataRoot/"runtime",a.cfg.characterRoot,a.cfg.uiRoot,a.cfg.projectRoot})if(contains(path,protectedRoot)||contains(protectedRoot,path)){error="computer_root_overlaps_private_data";return false;}mounts.push_back({root.at("id"),path,true,false});roots.push_back(root);}
    for(auto& mount:mounts)mount.maxReadBytes=32768;
    if(!a.files.configure(std::move(mounts),[shell=a.cfg.computer.value("limitedShellEnabled",false)](const auto& access){return access.principal=="character"&&(access.operation==ayt::io::WorkspaceOperation::List||shell&&access.operation==ayt::io::WorkspaceOperation::Read);},error))return false;a.roots=std::move(roots);return true;
}
Json DesktopServices::capabilities()const{const auto& a=*impl_;return {{"enabled",a.cfg.computer.value("enabled",true)},{"applications",a.applications()},{"read_roots",a.roots},{"screen_capture",a.cfg.computer.value("allowScreenCapture",false)},{"application_capture",a.cfg.desktopEnabled},{"shell_access",a.cfg.computer.value("limitedShellEnabled",false)},{"shell_kind","command_allowlist"},{"arbitrary_commands",false}};}
Json DesktopServices::invoke(const std::string& name,const Json& args,const std::function<bool()>& cancelled){auto& a=*impl_;if(!a.cfg.computer.value("enabled",true))return toolFailure("computer_tools_disabled");if(cancelled&&cancelled())return toolFailure("cancelled");
    if(name=="computer.list_directory"){std::vector<std::string> entries;std::string error;if(!a.files.list("character",args.at("root").get<std::string>(),args.value("path",std::string{}),entries,error,4096))return toolFailure("directory_not_authorized_or_too_large");const auto offset=args.value("offset",size_t(0)),limit=args.value("limit",size_t(128));Json items=Json::array();for(size_t i=offset;i<entries.size()&&items.size()<limit;++i)items.push_back(entries[i]);return toolSuccess({{"entries",items},{"total",entries.size()},{"next_offset",std::min(entries.size(),offset+items.size())},{"complete",offset+items.size()>=entries.size()}});}
    if(name=="computer.shell_read"){
        if(!a.cfg.computer.value("limitedShellEnabled",false))return toolFailure("limited_shell_disabled");std::string text,error;if(!a.files.read("character",args.at("root").get<std::string>(),args.at("path").get<std::string>(),text,error))return toolFailure("shell_read_denied_or_too_large");
        try{if(text.find('\0')!=text.npos)return toolFailure("shell_text_required");(void)Json(text).dump();}catch(...){return toolFailure("shell_text_required");}return toolSuccess({{"text",text},{"bytes",text.size()},{"maximum_bytes",32768}});
    }
    if(name=="computer.open_application"){if(!a.cfg.desktopEnabled)return toolFailure("desktop_required");const auto apps=a.applications();const auto it=std::find_if(apps.begin(),apps.end(),[&](const auto& app){return app.at("id")==args.at("id");});if(it==apps.end())return toolFailure("application_not_allowed");std::filesystem::path executable;
        if(it->contains("builtin")){wchar_t system[32768]{};GetSystemDirectoryW(system,32768);const auto builtin=it->at("builtin").get<std::string>();executable=std::filesystem::path(system)/(builtin=="notepad"?L"notepad.exe":L"calc.exe");}else executable=std::filesystem::u8path(it->at("path").get<std::string>());
        const auto result=ayt::platform::launchDesktopApplication(executable);if(!result.launched)return toolFailure(result.error.c_str());return toolSuccess({{"launched",true},{"id",args.at("id")},{"process_id",result.processId},{"note","仅确认启动请求成功，不等于已读到程序内容。"}});
    }
    if(name=="computer.screenshot"){if(!a.cfg.desktopEnabled)return toolFailure("desktop_required");const auto target=args.value("target",std::string("application"));HWND window=nullptr;Json geometry=Json::object(),requestedRegion=args.value("region",Json());
        if(target=="screen"||target=="widget"){
            if(!a.cfg.computer.value("allowScreenCapture",false))return toolFailure("screen_capture_not_enabled");
            if(target=="widget"){
                if(args.contains("region"))return toolFailure("widget_capture_region_is_program_managed");
                geometry=a.widgetGeometry?a.widgetGeometry():Json::object();if(!geometry.value("available",false)||!geometry.contains("capture_region"))return toolFailure("widget_not_visible_or_unavailable");
                requestedRegion=geometry.at("capture_region");
            }
        }else{if(target!="application")return toolFailure("invalid_capture_target");window=conversationWindow();if(!window)return toolFailure("application_window_not_visible");}
        ayt::platform::DesktopCaptureRegion region;
        if(!requestedRegion.is_null()){
            const auto& r=requestedRegion;if(!r.is_object()||r.size()!=4)return toolFailure("invalid_capture_region");
            for(const auto* key:{"x","y","width","height"})if(!r.contains(key)||!r.at(key).is_number())return toolFailure("invalid_capture_region");
            region={r.at("x").get<double>(),r.at("y").get<double>(),r.at("width").get<double>(),r.at("height").get<double>()};
        }
        const auto image=ayt::platform::captureDesktopRegion(reinterpret_cast<std::uintptr_t>(window),region,960,540);if(!image)return toolFailure(image.error.c_str());auto encoded=encodeJpegBase64(image.bgra,image.width,image.height);if(encoded.empty())return toolFailure("capture_encoding_failed");
        Json output={{"target",target},{"width",image.width},{"height",image.height},{"region",{{"x",region.x},{"y",region.y},{"width",region.width},{"height",region.height}}},{"coordinate_space","normalized_full_target_top_left"},{"region_reference",window?"application_client":"virtual_desktop"},{"image",{{"mime_type","image/jpeg"},{"base64",encoded}}},{"note","从本次实际目标区域直接截图后缩小，不是裁切旧预览。不提供键盘输入、任意点击或文件修改权限。"}};
        if(!window)output["screen_bounds"]={{"x",GetSystemMetrics(SM_XVIRTUALSCREEN)},{"y",GetSystemMetrics(SM_YVIRTUALSCREEN)},{"width",GetSystemMetrics(SM_CXVIRTUALSCREEN)},{"height",GetSystemMetrics(SM_CYVIRTUALSCREEN)}};
        if(target=="widget")output["widget_geometry"]=geometry;return toolSuccess(output);
    }return toolFailure("tool_not_available");
}
Json DesktopServices::browser(const std::string& operation,const Json& args,const std::function<bool()>& cancelled){auto& a=*impl_;if(!a.cfg.desktopEnabled)return toolFailure("desktop_required");if(operation.starts_with("mentor_chat."))return a.mentorChat->invoke(operation,args,cancelled);auto command=std::make_shared<Impl::Command>();command->operation=operation;command->args=args;
    if(GetCurrentThreadId()==a.owner)return a.execute(command);
    auto future=command->result.get_future();{std::lock_guard lock(a.mutex);if(a.stopped||a.queue.size()>=16)return toolFailure("browser_queue_unavailable");a.queue.push_back(command);}
    while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready){if((cancelled&&cancelled())||std::chrono::steady_clock::now()>command->deadline){command->cancelled=true;return toolFailure("webpage_wait_timeout",true);}}return future.get();
}
Json DesktopServices::webVideoPlayback()const{
#ifdef HA_HAS_WEBVIEW
    auto& a=*impl_;std::lock_guard guard(a.mutex);if(a.loginWindow||!a.view)return Json::object();return a.webPlayback;
#else
    return Json::object();
#endif
}
void DesktopServices::tick(){auto& a=*impl_;a.mentorChat->tick();std::deque<std::shared_ptr<Impl::Command>> queue;{std::lock_guard lock(a.mutex);queue.swap(a.queue);}for(auto& command:queue){if(command->cancelled||command->answered)continue;if(std::chrono::steady_clock::now()>command->deadline){command->complete(toolFailure("webpage_wait_timeout",true));continue;}if(std::chrono::steady_clock::now()<command->retryAfter){std::lock_guard lock(a.mutex);a.queue.push_back(command);continue;}auto result=a.execute(command);if(result.is_null()){std::lock_guard lock(a.mutex);a.queue.push_back(command);}else if(!result.value("pending",false))command->complete(std::move(result));}
#ifdef HA_HAS_WEBVIEW
 a.updateTelevisionLogin();a.updateTelevisionPresentation();
#endif
}
void DesktopServices::stop(){if(!impl_)return;auto& a=*impl_;a.mentorChat->stop();{std::lock_guard lock(a.mutex);a.stopped=true;for(auto& item:a.queue)item->complete(toolFailure("browser_closed"));a.queue.clear();}
#ifdef HA_HAS_WEBVIEW
    a.finishTelevisionLogin(false);if(a.view)a.view->close();a.view.reset();
#endif
}
}
