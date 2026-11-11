#include "../PluginCommon.h"
#include <Hibernation/MentorAdvice.h>
#include <Hibernation/MentorExecutable.h>
#include <AYPlatform/ManagedProcess.h>
#include <objbase.h>
#include <AYIO/File.h>
#include <winhttp.h>
#include <fstream>
#include <set>
#include <thread>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
std::string proxyOrigin(const PluginJson& cfg){
    const auto chosen=cfg.value("proxyOrigin",std::string{});if(!chosen.empty())return chosen;if(!cfg.value("useSystemProxy",false))return {};
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG info{};if(!WinHttpGetIEProxyConfigForCurrentUser(&info))return {};
    struct Free {WINHTTP_CURRENT_USER_IE_PROXY_CONFIG& value;~Free(){if(value.lpszProxy)GlobalFree(value.lpszProxy);if(value.lpszProxyBypass)GlobalFree(value.lpszProxyBypass);if(value.lpszAutoConfigUrl)GlobalFree(value.lpszAutoConfigUrl);}}cleanup{info};
    if(!info.lpszProxy)return {};const std::wstring native=info.lpszProxy;const int bytes=WideCharToMultiByte(CP_UTF8,0,native.data(),static_cast<int>(native.size()),nullptr,0,nullptr,nullptr);std::string text(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,native.data(),static_cast<int>(native.size()),text.data(),bytes,nullptr,nullptr);
    if(text.find('=')!=text.npos){std::string selected;std::size_t start=0;while(start<text.size()){const auto end=text.find(';',start);const auto part=text.substr(start,end==text.npos?text.npos:end-start);if(part.starts_with("https=")){selected=part.substr(6);break;}if(part.starts_with("http="))selected=part.substr(5);if(end==text.npos)break;start=end+1;}text=selected;}
    if(text.empty())return {};if(!text.starts_with("http://")&&!text.starts_with("https://"))text="http://"+text;
    return ayt::net::http::validOrigin(text)?text:std::string{};
}
std::filesystem::path executable(const PluginJson& cfg){
    const auto configured=cfg.value("codexExecutable",std::string{});
    std::wstring local(32768,L'\0');const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),static_cast<DWORD>(local.size()));
    std::filesystem::path root;if(n&&n<local.size()){local.resize(n);root=std::filesystem::path(local)/L"OpenAI"/L"Codex"/L"bin";}
    const auto recovered=resolveMentorExecutable(std::filesystem::u8path(configured),root);if(!recovered.empty()||!configured.empty())return recovered;
    std::wstring path(32768,L'\0');const auto length=SearchPathW(nullptr,L"codex.exe",nullptr,static_cast<DWORD>(path.size()),path.data(),nullptr);if(!length||length>=path.size())return {};path.resize(length);return path;
}
void moduleAnchor(){}
std::filesystem::path workerPath(){
    HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&moduleAnchor),&module))return {};
    std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));if(!n||n>=path.size())return {};path.resize(n);return std::filesystem::path(path).parent_path()/"session_worker.py";
}
std::string nonce(){GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("random");const auto* p=reinterpret_cast<const unsigned char*>(&id);std::string out;constexpr char hex[]="0123456789abcdef";for(unsigned i=0;i<sizeof(id);++i){out+=hex[p[i]>>4];out+=hex[p[i]&15];}return out;}
std::filesystem::path pythonPath(const PluginJson& cfg,const PluginHostContext& host){
    const auto chosen=cfg.value("pythonExecutable",std::string{});if(!chosen.empty()){const auto p=std::filesystem::u8path(chosen);return p.is_absolute()&&std::filesystem::is_regular_file(p)?p:std::filesystem::path{};}
    const auto local=host.projectRoot/"out"/"deps"/"style-lora-venv"/"Scripts"/"python.exe";if(std::filesystem::is_regular_file(local))return local;
    std::wstring path(32768,L'\0');const auto n=SearchPathW(nullptr,L"python.exe",nullptr,static_cast<DWORD>(path.size()),path.data(),nullptr);if(!n||n>=path.size())return {};path.resize(n);if(path.find(L"WindowsApps")!=path.npos)return {};return path;
}
class Mentor final:public BasePlugin {
    mutable std::mutex mutex_;std::mutex startMutex_,requestMutex_;PluginJson cfg_,context_=PluginJson::object(),session_=PluginJson::object(),web_=PluginJson::object();PluginHostContext host_;
    std::atomic<bool> stopped_=false,paused_=false,busy_=false;std::atomic<unsigned> cancellation_{0};
    ayt::platform::ManagedProcess process_;std::string origin_,auth_,instance_,chatUrl_,activeWebRequest_;
    std::int64_t window_=0,last_=0;int used_=0,limit_=12;std::string lastTurn_,lastIssue_;
    void stopService(){std::lock_guard start(startMutex_);stopped_=true;process_.stop();std::lock_guard lock(mutex_);origin_.clear();auth_.clear();session_.erase("provider_thread_id");}
    std::string ensure(){
        std::lock_guard start(startMutex_);if(process_.snapshot().state==ayt::platform::ManagedProcessState::Stopping)process_.stop();PluginJson cfg;PluginHostContext host;{std::lock_guard lock(mutex_);cfg=cfg_;host=host_;if(process_.snapshot().active&&!origin_.empty())return {};}
        process_.stop();const auto python=pythonPath(cfg,host);if(python.empty())return "mentor_python_required";const auto worker=workerPath();if(!std::filesystem::is_regular_file(worker))return "mentor_worker_missing";
        const auto runtime=host.dataRoot/"runtime"/"mentor";std::filesystem::create_directories(runtime);const auto ready=runtime/"service-ready.json";const auto instance=nonce(),auth=nonce()+nonce();
        {std::lock_guard lock(mutex_);instance_=instance;auth_=auth;origin_.clear();}stopped_=false;
        ayt::platform::ProcessOptions options;options.executable=python;options.workingDirectory=worker.parent_path();options.arguments={"-B",pathUtf8(worker),"--root",pathUtf8(host.dataRoot/"mentors"/"default"),"--ready",pathUtf8(ready),"--instance",instance};options.timeout=std::chrono::milliseconds(0);options.outputLimit=4096;options.cancellationRequested=[this]{return stopped_.load();};
        options.environmentOverrides={{"HIBERNATION_MENTOR_AUTH",auth},{"PYTHONIOENCODING","utf-8"},{"PYTHONDONTWRITEBYTECODE","1"},{"OPENAI_API_KEY",std::nullopt},{"CODEX_API_KEY",std::nullopt}};
        for(const auto* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})options.environmentOverrides[key]=std::nullopt;
        const auto proxy=proxyOrigin(cfg);if(!proxy.empty()){options.environmentOverrides["HTTP_PROXY"]=proxy;options.environmentOverrides["HTTPS_PROXY"]=proxy;options.environmentOverrides["ALL_PROXY"]=proxy;}
        if(!process_.start(std::move(options)))return "mentor_service_unavailable";
        const auto state=process_.waitReady([this,ready,instance]{try{if(!std::filesystem::is_regular_file(ready)||std::filesystem::file_size(ready)>4096)return false;std::ifstream in(ready,std::ios::binary);const auto j=PluginJson::parse(in);const int port=j.at("port");if(j.at("instance")==instance&&port>0&&port<=65535){std::lock_guard lock(mutex_);origin_="http://127.0.0.1:"+std::to_string(port);return true;}}catch(...){}return false;},std::chrono::seconds(5),[this]{return stopped_.load();});
        return state.status==ayt::platform::ProcessReadiness::Ready?std::string{}:"mentor_service_not_ready";
    }
    PluginJson call(const std::string& path,const PluginJson& body,int seconds,const std::function<bool()>& cancel={}){
        std::string origin,auth;{std::lock_guard lock(mutex_);origin=origin_;auth=auth_;}if(origin.empty())return toolFailure("mentor_service_unavailable");
        ayt::net::http::Request request;request.origin=origin;request.path=path;request.method="POST";request.useSystemProxy=false;request.headers={{"Authorization","Bearer "+auth},{"Content-Type","application/json"}};request.body=body.dump();request.maxResponseBytes=128*1024;request.timeout=std::chrono::seconds(seconds);request.cancelled=cancel;
        const auto response=ayt::net::http::perform(request);if(!response||response.httpStatus!=200)return toolFailure(cancel&&cancel()?"cancelled":"mentor_service_request_failed");
        try{auto value=PluginJson::parse(response.body);if(!value.is_object()||!value.contains("ok")||!value.at("ok").is_boolean())return toolFailure("mentor_service_response");if(value.value("ok",false)&&value.contains("result")&&(path=="/status"||path=="/rebuild")){std::lock_guard lock(mutex_);session_=value.at("result");}return value;}catch(...){return toolFailure("mentor_service_response");}
    }
    PluginJson webFallback(const PluginJson& body,const PluginJson& cfg,const PluginJson& original,const std::function<bool()>& cancel){
        if(cfg.value("quotaFallback",std::string("chat_web"))!="chat_web")return original;
        const auto partial=original.value("result",PluginJson::object());if(partial.value("operations_dispatched",false)||partial.value("operations_may_have_executed",false))return original;
        PluginHostContext host;std::string url;{std::lock_guard lock(mutex_);host=host_;url=chatUrl_;}if(!host.desktopEnabled||!host.browser)return toolFailure("mentor_chat_desktop_required");
        auto opened=host.browser("mentor_chat.open",{{"url",url.empty()?"https://chatgpt.com/":url},{"ensure_new",url.empty()},{"visible",false}},cancel);if(!opened.value("ok",false))return opened;
        PluginJson page;const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
        do{if(cancel())return toolFailure("cancelled");page=host.browser("mentor_chat.status",PluginJson::object(),cancel);const auto state=page.value("result",PluginJson::object());if(page.value("ok",false)&&state.value("state",std::string{})!="loading")break;if(!page.value("ok",false)&&page.value("error",PluginJson::object()).value("code",std::string{})!="mentor_chat_not_ready")break;std::this_thread::sleep_for(std::chrono::milliseconds(150));}while(std::chrono::steady_clock::now()<readyDeadline);
        if(!page.value("ok",false))return page;{std::lock_guard lock(mutex_);web_=page.at("result");}
        if(page.at("result").value("login_required",false))return toolFailure("mentor_chat_login_required");
        if(!page.at("result").value("mode_verified",false)||page.at("result").value("work_mode_detected",false))return toolFailure("mentor_chat_mode_unverified");
        if(!page.at("result").value("ready",false))return toolFailure("mentor_chat_not_ready");
        auto prepared=call("/chat/prepare",body,5,cancel);if(!prepared.value("ok",false))return prepared;const auto value=prepared.at("result");
        if(value.contains("duplicate_result"))return toolSuccess(value.at("duplicate_result"));
        const auto ticket=value.at("ticket").get<std::string>(),request=body.at("request_id").get<std::string>();{std::lock_guard lock(mutex_);activeWebRequest_=request;}
        bool released=false;const auto abort=[&]{if(released)return;released=true;host.browser("mentor_chat.cancel",{{"request_id",request}},{});(void)call("/chat/abort",{{"ticket",ticket},{"request_id",request}},3);std::lock_guard lock(mutex_);activeWebRequest_.clear();};
        struct WebLease {std::function<void()> finish;~WebLease(){try{finish();}catch(...){}}} lease{abort};
        const auto sent=host.browser("mentor_chat.send",{{"request_id",request},{"prompt",value.at("prompt")}},cancel);if(!sent.value("ok",false)){abort();return sent;}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(cfg.value("timeoutSeconds",120));
        while(std::chrono::steady_clock::now()<deadline){
            if(cancel()){abort();return toolFailure("cancelled");}
            const auto polled=host.browser("mentor_chat.poll",{{"request_id",request}},cancel);if(!polled.value("ok",false)){abort();return polled;}const auto& answer=polled.at("result");
            if(answer.value("state",std::string{})=="complete"){
                const auto finished=call("/chat/complete",{{"ticket",ticket},{"request_id",request},{"text",answer.at("text")},{"conversation_url",answer.value("url",std::string{})}},5,cancel);
                if(!finished.value("ok",false)){abort();return finished;}{std::lock_guard lock(mutex_);chatUrl_=answer.value("url",std::string{});activeWebRequest_.clear();web_["last_issue"]="";web_["ready"]=true;web_["state"]="complete";}released=true;return finished;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        abort();return toolFailure("mentor_chat_timeout");
    }
public:
    ~Mentor()override{shutdown();}
    const char* getName()const override{return "mentor";}
    const char* getVersion()const override{return "0.6.0";}
    const char* getDescription()const override{return "A continuing ChatGPT mentor with private memory and its own workspace";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        try{configurationTypes(cfg,{"displayName","backend","codexExecutable","pythonExecutable","model","proxyOrigin","reasoningEffort","routingMode","casualModel","normalModel","complexModel","casualEffort","normalEffort","complexEffort","quotaFallback"},{"useSystemProxy","allowExecution"});if(cfg.value("displayName",std::string{}).size()>120)throw std::runtime_error("display_name");const auto backend=cfg.value("backend",std::string("chatgpt"));if(backend!="chatgpt"&&backend!="none")throw std::runtime_error("backend");
            for(const auto* key:{"model","casualModel","normalModel","complexModel"}){if(!cfg.contains(key)||cfg.at(key).get_ref<const std::string&>().empty())cfg[key]="gpt-6.1-sol";if(cfg.at(key).get_ref<const std::string&>().size()>120)throw std::runtime_error("model");}
            for(const auto& pair:std::initializer_list<std::pair<const char*,const char*>>{{"reasoningEffort","high"},{"casualEffort","low"},{"normalEffort","medium"},{"complexEffort","high"}}){if(!cfg.contains(pair.first))cfg[pair.first]=pair.second;const auto value=cfg.at(pair.first).get<std::string>();if(value!="low"&&value!="medium"&&value!="high"&&value!="xhigh")throw std::runtime_error("effort");}
            if(!cfg.contains("routingMode"))cfg["routingMode"]="fixed";if(cfg.at("routingMode")!="fixed"&&cfg.at("routingMode")!="character")throw std::runtime_error("routing");
            if(!cfg.contains("quotaFallback"))cfg["quotaFallback"]="chat_web";if(cfg.at("quotaFallback")!="chat_web"&&cfg.at("quotaFallback")!="pause")throw std::runtime_error("fallback");
            if(!cfg.contains("timeoutSeconds"))cfg["timeoutSeconds"]=120;
            const auto limit=number(cfg,"maxRequestsPerHour",12,1,120),seconds=number(cfg,"timeoutSeconds",120,5,300),cooldown=number(cfg,"cooldownSeconds",60,0,3600);if(std::floor(limit)!=limit||std::floor(seconds)!=seconds||std::floor(cooldown)!=cooldown)throw std::runtime_error("integer");
            for(const auto* key:{"codexExecutable","pythonExecutable"}){const auto path=cfg.value(key,std::string{});if(!path.empty()&&(!std::filesystem::u8path(path).is_absolute()||std::filesystem::u8path(path).extension()!=L".exe"))throw std::runtime_error("executable");}
            bool restart;{std::lock_guard lock(mutex_);restart=cfg_!=cfg||host_.dataRoot!=host.dataRoot;}if(restart)stopService();{std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);limit_=static_cast<int>(limit);context_=PluginJson::object();}stopped_=false;issue.clear();return true;
        }catch(...){issue="mentor_configuration";return false;}
    }
    PluginJson tools()const override{
        auto properties=PluginJson{{"goal",{{"type","string"},{"minLength",1},{"maxLength",600},{"description","想聊的话题、问题或委托目标"}}},{"context",{{"type","string"},{"maxLength",1600}}},{"obstacle",{{"type","string"},{"maxLength",800}}},{"attempts",{{"type","string"},{"maxLength",1000}}},{"tool_names",{{"type","array"},{"maxItems",4},{"items",{{"type","string"},{"minLength",1},{"maxLength",96}}}}}};
        properties["level"]={{"type","string"},{"enum",PluginJson::array({"casual","normal","complex"})},{"description","启用自主分级时：casual闲聊低、normal普通问题中、complex复杂问题高；省略按复杂问题高。固定模式使用配置的默认模型和强度，此参数不改变它。"}};
        auto result=PluginJson::array({tool("mentor.status","查看导师持续会话、独立记忆和请求额度；不调用模型。",object()),tool("mentor.ask","与同一位导师继续交流、闲聊或提问。导师有自己的长期记忆和工作区，无需每次重述前情，也不要求有困难或先尝试。仅提供本次需要的少量背景，不传完整人设和档案；导师回复由你自主理解。启用自主分级时可用level选择casual闲聊、normal普通问题、complex复杂问题；只选择满足问题需要的档位，不另外调用模型做分级。省略时使用高档。同轮至多咨询或委托一次。",object(properties,PluginJson::array({"goal"}))),tool("mentor.session.cancel","管理员：取消当前导师请求，保留记忆；不触发模型。",object()),tool("mentor.session.rebuild","管理员：重建原生上下文，保留导师记忆和逻辑会话；不触发模型。",object()),tool("mentor.session.chat_open","管理员：打开专用普通Chat窗口以手动登录；不自动发送问题。",object())});
        result.push_back(tool("mentor.history","检索导师以前给自己的实际回复，例如名字、相识经过和旧建议；当前窗口没显示不代表从未交流。不调用模型，不读取导师私下的笔记、电脑操作日志或其他人的档案。query为词语，可省略读取最近几条。",object({{"query",{{"type","string"},{"maxLength",200}}},{"limit",{{"type","integer"},{"minimum",1},{"maximum",8}}}})));
        auto relayProperties=properties;relayProperties["context"]["maxLength"]=6000;
        result.push_back(tool("mentor.forward","宿主内部：把已绑定来源的开发需求转入同一个导师会话，只交流和整理，不执行代码。",object(relayProperties,PluginJson::array({"goal"}))));
        result.push_back(tool("mentor.forward_status","宿主内部：查询导师需求的实际送达与完成回执，不调用模型。",object({{"request_id",{{"type","string"},{"minLength",1},{"maxLength",128}}}},PluginJson::array({"request_id"}))));
        std::lock_guard lock(mutex_);if(cfg_.value("allowExecution",true))result.push_back(tool("mentor.execute","将明确的任务交给导师处理。导师使用自身Codex权限和工作区，不受小爱工具白名单支配，实际能力受导师自身配置与审批约束；可省略tool_names。执行事实看原生操作回执，不能把文字承诺当成成功。无需再次通过宿主重放操作；不能递归委托。普通交流使用mentor.ask。",object(properties,PluginJson::array({"goal"}))));return result;
    }
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& external)override{
        if(name=="mentor.session.chat_open"){PluginHostContext host;std::string url;{std::lock_guard lock(mutex_);host=host_;url=chatUrl_;}if(!host.browser)return toolFailure("mentor_chat_desktop_required");return host.browser("mentor_chat.open",{{"url",url.empty()?"https://chatgpt.com/":url}},external);}
        if(name=="mentor.session.cancel"){++cancellation_;PluginHostContext host;std::string request;{std::lock_guard lock(mutex_);host=host_;request=activeWebRequest_;}if(!request.empty()&&host.browser)host.browser("mentor_chat.cancel",{{"request_id",request}},{});if(!process_.snapshot().active)return toolSuccess(snapshot());return call("/cancel",PluginJson::object(),3);}
        if(name=="mentor.session.rebuild"&&busy_)return toolFailure("mentor_busy");
        if(name=="mentor.status"||name=="mentor.session.rebuild"){const auto issue=ensure();if(!issue.empty())return toolFailure(issue.c_str());auto response=call(name=="mentor.status"?"/status":"/rebuild",PluginJson::object(),3);if(name=="mentor.status"&&response.value("ok",false)){PluginHostContext host;{std::lock_guard lock(mutex_);host=host_;}if(host.browser&&host.desktopEnabled){const auto page=host.browser("mentor_chat.status",PluginJson::object(),external);if(page.value("ok",false)){std::lock_guard lock(mutex_);web_=page.at("result");}}return toolSuccess(snapshot());}return response;}
        if(name=="mentor.history"){const auto issue=ensure();if(!issue.empty())return toolFailure(issue.c_str());return call("/history",args,5,external);}
        if(name=="mentor.forward_status"){const auto issue=ensure();if(!issue.empty())return toolFailure(issue.c_str());return call("/forward/status",args,3,external);}
        const bool forward=name=="mentor.forward",execute=name=="mentor.execute";if(name!="mentor.ask"&&!execute&&!forward)return toolFailure("tool_not_available");std::unique_lock serial(requestMutex_,std::try_to_lock);if(!serial.owns_lock())return toolFailure("mentor_busy");
        const auto generation=cancellation_.load();const auto cancel=[&]{return stopped_.load()||paused_.load()||cancellation_.load()!=generation||(external&&external());};if(cancel())return toolFailure("cancelled");
        PluginJson cfg,context;{std::lock_guard lock(mutex_);cfg=cfg_;context=std::move(context_);context_=PluginJson::object();}
        if(cfg.value("backend",std::string("chatgpt"))!="chatgpt")return toolFailure("mentor_not_configured");if(execute&&!cfg.value("allowExecution",true))return toolFailure("mentor_execution_disabled");
        if(context.value("turn_id",std::string{}).empty())return toolFailure("mentor_context_unavailable");if(execute&&!context.value("execution_authorized",false))return toolFailure("mentor_execution_host_required");
        const auto exe=executable(cfg);if(exe.empty())return toolFailure("mentor_codex_not_found");const auto issue=ensure();if(!issue.empty())return toolFailure(issue.c_str());
        if(forward){auto prior=call("/forward/status",{{"request_id",context.at("turn_id")}},3,cancel);if(prior.value("ok",false))return prior;auto status=call("/status",PluginJson::object(),3,cancel);if(status.value("ok",false)&&status.at("result").value("running",false))return toolFailure("mentor_busy",true);}
        {std::lock_guard lock(mutex_);const auto now=nowMs();if(!window_||now<window_||now-window_>=3600000){window_=now;used_=0;}if(context.at("turn_id")==lastTurn_)return toolFailure("mentor_once_per_turn");if(used_>=limit_)return toolFailure("mentor_hourly_budget");if(!forward&&last_&&now>=last_&&now-last_<cfg.value("cooldownSeconds",60)*1000LL)return toolFailure("mentor_cooldown");++used_;last_=now;lastTurn_=context.at("turn_id");lastIssue_.clear();}
        busy_=true;struct Busy{std::atomic<bool>& flag;~Busy(){flag=false;}}clear{busy_};cfg["codexExecutable"]=pathUtf8(exe);
        PluginJson available=PluginJson::array();for(const auto& chosen:args.value("tool_names",PluginJson::array()))for(const auto& item:context.value("tools",PluginJson::array()))if(item.value("name",std::string{})==chosen.get<std::string>()){available.push_back(item);break;}
        const PluginJson body={{"settings",cfg},{"args",args},{"execute",execute},{"available_tools",available},{"request_id",context.at("turn_id")}};
        auto result=call(forward?"/forward":"/ask",body,forward?5:cfg.value("timeoutSeconds",120)+12,cancel);
        if(forward){(void)call("/status",PluginJson::object(),3);return result;}
        if(!cancel()&&!result.value("ok",false)&&result.value("error",PluginJson::object()).value("code",std::string{})=="mentor_work_quota_exhausted")result=webFallback(body,cfg,result,cancel);
        if(cancel()||!result.value("ok",false)){call("/cancel",PluginJson::object(),3);std::lock_guard lock(mutex_);lastIssue_=result.value("error",PluginJson::object()).value("code",std::string("cancelled"));}
        (void)call("/status",PluginJson::object(),3);return result;
    }
    void event(const PluginJson& value)override{const auto kind=value.value("kind",std::string{});if(kind=="runtime.paused"){paused_=value.value("paused",false);if(paused_){++cancellation_;process_.requestStop();}}if(kind=="mentor.context"){std::lock_guard lock(mutex_);context_=value;}}
    PluginJson snapshot()const override{
        const auto active=process_.snapshot().active;std::lock_guard lock(mutex_);const auto now=nowMs();auto out=session_;out["display_name"]=cfg_.value("displayName",std::string("导师"));out["history_tool"]="mentor.history";out["routing_mode"]=cfg_.value("routingMode",std::string("fixed"));out["default_model"]=cfg_.value("model",std::string("gpt-6.1-sol"));out["default_effort"]=cfg_.value("reasoningEffort",std::string("high"));out["web_fallback"]=web_;out["web_fallback"]["enabled"]=cfg_.value("quotaFallback",std::string("chat_web"))=="chat_web";out["chat_conversation_url"]=chatUrl_;out["backend"]=cfg_.value("backend",std::string("chatgpt"));out["executable_available"]=!executable(cfg_).empty();out["authorization"]="codex_chatgpt_login";out["conversation_allowed"]=true;out["advisory_only"]=!cfg_.value("allowExecution",true);out["execution_enabled"]=cfg_.value("allowExecution",true);out["execution_scope"]="mentor_own_permissions";out["permission_policy"]="codex_user_configuration";out["automatic_consultation"]=false;out["service_running"]=active;out["busy"]=busy_.load()||out.value("running",false);out["request_window_ms"]=window_;out["requests_used"]=window_&&now>=window_&&now-window_<3600000?used_:0;out["request_limit"]=limit_;out["last_request_ms"]=last_;out["last_turn"]=lastTurn_;out["last_issue"]=lastIssue_.empty()?out.value("last_issue",std::string{}):lastIssue_;if(!active)out.erase("provider_thread_id");return out;
    }
    bool restore(const PluginJson& value)override{if(!value.is_object())return false;std::lock_guard lock(mutex_);const auto url=value.value("chat_conversation_url",std::string{});if(url.size()<=4096&&url.starts_with("https://chatgpt.com/c/")&&url.find_first_of("\r\n")==url.npos)chatUrl_=url;const auto now=nowMs(),window=value.value("request_window_ms",std::int64_t(0));if(window>0&&now>=window&&now-window<3600000){window_=window;used_=std::clamp(value.value("requests_used",0),0,120);}const auto last=value.value("last_request_ms",std::int64_t(0));if(last>0&&last<=now){last_=last;lastTurn_=value.value("last_turn",std::string{});}return true;}
    void shutdown()override{stopService();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Mentor;}
