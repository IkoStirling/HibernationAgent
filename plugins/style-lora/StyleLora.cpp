#include "../PluginCommon.h"
#include "DatasetWorkshop.h"
#include <AYPlatform/ManagedProcess.h>
#include <fstream>
#include <tuple>
#include <objbase.h>
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
void moduleAnchor(){}
std::filesystem::path workerPath(){
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&moduleAnchor),&module))return {};
    std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));if(!length||length>=path.size())return {};path.resize(length);
    return std::filesystem::path(path).parent_path()/"worker.py";
}
std::string nonce(){GUID value{};if(FAILED(CoCreateGuid(&value)))throw std::runtime_error("random");const auto* bytes=reinterpret_cast<const unsigned char*>(&value);constexpr char hex[]="0123456789abcdef";std::string out;for(std::size_t i=0;i<sizeof(value);++i){out+=hex[bytes[i]>>4];out+=hex[bytes[i]&15];}return out;}
class StyleLora final:public BasePlugin {
    mutable std::mutex mutex_;std::mutex startMutex_;PluginHostContext host_;PluginJson cfg_=PluginJson::object();
    std::atomic<bool> stopped_=false,paused_=false;
    ayt::platform::ManagedProcess process_;std::string origin_,auth_,instance_,issue_;std::shared_ptr<expression::DatasetWorkshop> workshop_;std::atomic<bool> datasetBlocked_=false;
    void stopService(){std::lock_guard start(startMutex_);stopped_=true;process_.stop();std::lock_guard lock(mutex_);origin_.clear();auth_.clear();}
    std::string ensure(){
        std::lock_guard start(startMutex_);if(paused_)return "runtime_paused";if(process_.snapshot().state==ayt::platform::ManagedProcessState::Stopping)process_.stop();PluginJson cfg;PluginHostContext host;
        {std::lock_guard lock(mutex_);cfg=cfg_;host=host_;if(process_.snapshot().active&&!origin_.empty())return {};}
        std::filesystem::path ready;std::string instance;
        if(!process_.snapshot().active){
            process_.stop();const auto python=std::filesystem::u8path(cfg.value("pythonExecutable",std::string{}));
            if(python.empty()||!python.is_absolute()||python.extension()!=L".exe"||!std::filesystem::is_regular_file(python))return "style_python_required";
            const auto worker=workerPath();if(!std::filesystem::is_regular_file(worker))return "style_worker_missing";
            const auto root=host.dataRoot/"runtime"/"style-lora";std::filesystem::create_directories(root);
            instance=nonce();const auto auth=nonce()+nonce();ready=root/"service-ready.json";
            {std::lock_guard lock(mutex_);instance_=instance;auth_=auth;origin_.clear();issue_.clear();}
            stopped_=false;
            ayt::platform::ProcessOptions options;options.executable=python;options.workingDirectory=worker.parent_path();
            options.arguments={"-B",pathUtf8(worker),"--root",pathUtf8(root),"--ready",pathUtf8(ready),"--instance",instance};
            options.outputLimit=4096;options.timeout=std::chrono::milliseconds(0);options.cancellationRequested=[this]{return stopped_.load()||paused_.load();};
            options.environmentOverrides={{"HIBERNATION_STYLE_AUTH",auth},{"PYTHONIOENCODING","utf-8"},{"PYTHONDONTWRITEBYTECODE","1"},{"HF_HUB_OFFLINE","1"},{"TRANSFORMERS_OFFLINE","1"}};
            if(!process_.start(std::move(options)))return "style_service_unavailable";
        }else{std::lock_guard lock(mutex_);instance=instance_;ready=host_.dataRoot/"runtime"/"style-lora"/"service-ready.json";}
        const auto state=process_.waitReady([this,ready,instance]{
            try{if(!std::filesystem::is_regular_file(ready)||std::filesystem::file_size(ready)>4096)return false;std::ifstream stream(ready,std::ios::binary);const auto value=PluginJson::parse(stream);const int port=value.at("port");if(value.at("instance")==instance&&port>0&&port<=65535){std::lock_guard lock(mutex_);origin_="http://127.0.0.1:"+std::to_string(port);return true;}}catch(...){}return false;
        },std::chrono::seconds(4),[this]{return stopped_.load()||paused_.load();});
        if(state.status==ayt::platform::ProcessReadiness::Ready)return {};
        {std::lock_guard lock(mutex_);origin_.clear();issue_="style_service_not_ready";}
        return state.status==ayt::platform::ProcessReadiness::Cancelled?"cancelled":"style_service_not_ready";
    }
public:
    ~StyleLora()override{shutdown();}
    const char* getName()const override{return "style-lora";}
    const char* getVersion()const override{return "0.2.0";}
    const char* getDescription()const override{return "Operator-trained local LoRA for optional, semantically reviewed dialogue editing";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        std::string stage="types";
        try{
            configurationTypes(cfg,{"pythonExecutable","baseModelPath","adapterId","device"},{"applyEnabled"});
            stage="paths";
            for(const auto* key:{"pythonExecutable","baseModelPath"}){const auto text=cfg.value(key,std::string{});if(!text.empty()&&!std::filesystem::u8path(text).is_absolute())throw std::runtime_error("absolute_path");}
            stage="device";const auto device=cfg.value("device",std::string("auto"));if(device!="auto"&&device!="cpu"&&device!="cuda")throw std::runtime_error("device");
            for(const auto& entry:std::initializer_list<std::tuple<const char*,double,double,double>>{{"rank",8,1,32},{"maxSteps",100,1,5000},{"maxSequenceLength",512,64,2048},{"generationLimit",192,16,1024},{"timeoutMs",12000,1000,60000},{"trainingTimeoutSeconds",1800,10,43200}}){const auto [key,fallback,lo,hi]=entry;stage=key;const auto value=number(cfg,key,fallback,lo,hi);if(std::floor(value)!=value)throw std::runtime_error("integer");cfg[key]=static_cast<int>(value);}
            stage="learningRate";number(cfg,"learningRate",.0002,.000001,.05);stage="adapterScale";number(cfg,"adapterScale",1,0,2);stage="runtime";
            bool restart=false;{std::lock_guard lock(mutex_);restart=cfg_.value("pythonExecutable",std::string{})!=cfg.value("pythonExecutable",std::string{})||host_.dataRoot!=host.dataRoot;}
            if(restart)stopService();{std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);}stopped_=false;issue.clear();return true;
        }catch(...){issue="style_lora_configuration_"+stage;return false;}
    }
    PluginJson tools()const override{
        return PluginJson::array({
            tool("style_lora.dataset.status","管理员：查看候选语料批次和方向版本，不训练。",object()),
            tool("style_lora.dataset.import","管理员：导入明确选择的方向示范包，不代表训练同意。",{{"type","object"},{"additionalProperties",true}}),
            tool("style_lora.dataset.start","管理员：开始有预算的合成候选生成和独立审核，不执行角色动作。",{{"type","object"},{"additionalProperties",true}}),
            tool("style_lora.dataset.pause","管理员：当前调用完成后暂停本批。",object()),
            tool("style_lora.dataset.resume","管理员：继续当前暂停批次。",object()),
            tool("style_lora.dataset.cancel","管理员：取消本批并保留已有结果。",object()),
            tool("style_lora.dataset.get","管理员：读取合成候选与审核意见。",{{"type","object"},{"additionalProperties",true}}),
            tool("style_lora.dataset.decide","管理员：保存人工选择或修改建议，不自动批准训练。",{{"type","object"},{"additionalProperties",true}}),
            tool("style_lora.dataset.export","管理员：导出完整候选档案及调用记录，不训练。",{{"type","object"},{"additionalProperties",true}}),
            tool("style_lora.status","管理员：查看本地训练状态，不读取或返回语料正文。",object()),
            tool("style_lora.import","管理员明确认可后导入表达对照。不能由角色自我采纳历史对白。",object({{"consent",{{"type","boolean"}}},{"samples",{{"type","array"},{"minItems",1},{"maxItems",256},{"items",{{"type","object"},{"additionalProperties",true}}}}}},PluginJson::array({"consent","samples"}))),
            tool("style_lora.train","管理员：开始一批有界SFT LoRA训练，不自动启用结果。",object()),
            tool("style_lora.cancel","管理员：取消当前训练；未完成适配器不进入可用版本。",object()),
            tool("style_lora.preview","管理员或宿主内部：对一段对白提出编辑候选，不执行操作或修改记忆。",object({{"text",{{"type","string"},{"minLength",1},{"maxLength",4000}}},{"context",{{"type","array"},{"maxItems",12},{"items",{{"type","object"},{"additionalProperties",true}}}}},{"facts",{{"type","string"},{"maxLength",6000}}}},PluginJson::array({"text"})))
        });
    }
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(name.starts_with("style_lora.dataset.")){
            if(cancelled&&cancelled())return toolFailure("cancelled");
            const auto action=name.substr(std::string("style_lora.dataset.").size());
            if(datasetBlocked_&&(action=="start"||action=="resume"))return toolFailure("runtime_paused");
            std::shared_ptr<expression::DatasetWorkshop> workshop;
            {std::lock_guard lock(mutex_);if(!workshop_)workshop_=std::make_shared<expression::DatasetWorkshop>(host_.dataRoot/"runtime"/"style-lora"/"datasets");workshop=workshop_;}
            return workshop->invoke(action,args);
        }
        if(!name.starts_with("style_lora."))return toolFailure("tool_not_available");
        if(cancelled&&cancelled())return toolFailure("cancelled");const auto problem=ensure();if(!problem.empty())return toolFailure(problem.c_str());
        PluginJson cfg;std::string origin,auth;{std::lock_guard lock(mutex_);cfg=cfg_;origin=origin_;auth=auth_;}
        if(args.dump().size()>1024*1024)return toolFailure("style_request_limit");
        ayt::net::http::Request request;request.origin=origin;request.path="/"+name.substr(11);request.method="POST";request.useSystemProxy=false;
        request.timeout=std::chrono::milliseconds(name=="style_lora.preview"?cfg.value("timeoutMs",12000):5000);request.maxResponseBytes=1024*1024;
        request.headers={{"Authorization","Bearer "+auth},{"Content-Type","application/json"}};request.body=PluginJson{{"settings",cfg},{"args",args}}.dump();request.cancelled=[this,cancelled]{return stopped_.load()||paused_.load()||(cancelled&&cancelled());};
        const auto result=ayt::net::http::perform(request);if(!result||result.httpStatus!=200){stopService();return toolFailure("style_service_unavailable",true);}
        try{auto response=PluginJson::parse(result.body);if(!response.is_object()||!response.contains("ok")||!response.at("ok").is_boolean())return toolFailure("style_service_response");return response;}catch(...){return toolFailure("style_service_response");}
    }
    void event(const PluginJson& value)override{if(value.value("kind",std::string{})=="runtime.paused"){paused_=value.value("paused",false);datasetBlocked_=paused_||value.value("offline",false)||value.value("balance_offline",false);if(paused_)process_.requestStop();std::shared_ptr<expression::DatasetWorkshop> workshop;{std::lock_guard lock(mutex_);workshop=workshop_;}if(datasetBlocked_&&workshop)workshop->cancel();}}
    PluginJson snapshot()const override{const auto state=process_.snapshot();std::lock_guard lock(mutex_);const auto issue=!state.active&&state.hasResult?(state.result.cancelled?"style_service_stopped":"style_service_exited"):issue_;return {{"service_running",state.active},{"issue",issue},{"apply_enabled",cfg_.value("applyEnabled",false)},{"adapter_id",cfg_.value("adapterId",std::string{})},{"review_required",true},{"operator_only",true}};}
    void shutdown()override{std::shared_ptr<expression::DatasetWorkshop> workshop;{std::lock_guard lock(mutex_);workshop=workshop_;}if(workshop)workshop->stop();stopService();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new StyleLora;}
