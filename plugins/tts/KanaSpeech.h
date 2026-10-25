#pragma once
#include <AYPlatform/ChildProcess.h>
#include <fstream>
#include <future>
#include <thread>
#include <wincrypt.h>
namespace hibernation::plugins {
inline std::filesystem::path speechDirectory(){
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&speechDirectory),&module);
    std::wstring name(32768,L'\0');const auto n=GetModuleFileNameW(module,name.data(),static_cast<DWORD>(name.size()));if(!n||n>=name.size())throw std::runtime_error("speech_directory");name.resize(n);return std::filesystem::path(name).parent_path();
}
inline PluginJson kanaRuntime(){
    const auto dir=speechDirectory();std::ifstream in(dir/"runtime.json",std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||in.tellg()>8192)throw std::runtime_error("speech_runtime_missing");in.seekg(0);auto cfg=PluginJson::parse(in);
    for(const auto* key:{"pythonExecutable","modules"}){const auto value=std::filesystem::u8path(cfg.at(key).get<std::string>());if(!value.is_absolute()||!std::filesystem::exists(value))throw std::runtime_error("speech_runtime_missing");}
    if(!std::filesystem::is_regular_file(dir/"kana_worker.py")||!std::filesystem::is_regular_file(dir/"kana_reading.py"))throw std::runtime_error("speech_runtime_missing");return cfg;
}
inline std::string speechNonce(){GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("speech_nonce");wchar_t name[40]{};StringFromGUID2(id,name,40);return pathUtf8(std::filesystem::path(name));}
inline void sealSpeechFile(const std::filesystem::path& file,const PluginJson& value){
    const auto text=value.dump();DATA_BLOB source{static_cast<DWORD>(text.size()),reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))},sealed{};
    if(!CryptProtectData(&source,L"Transient speech bootstrap",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&sealed))throw std::runtime_error("speech_bootstrap");
    struct Owner{BYTE* data;~Owner(){LocalFree(data);}} owner{sealed.pbData};std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(sealed.pbData),sealed.cbData);if(!out)throw std::runtime_error("speech_bootstrap");
}
inline PluginJson openSpeechFile(const std::filesystem::path& file){
    std::ifstream in(file,std::ios::binary|std::ios::ate);if(!in||in.tellg()<=0||in.tellg()>8192)throw std::runtime_error("speech_ready");const auto size=static_cast<std::size_t>(in.tellg());std::string text(size,'\0');in.seekg(0);in.read(text.data(),size);if(!in)throw std::runtime_error("speech_ready");
    DATA_BLOB source{static_cast<DWORD>(text.size()),reinterpret_cast<BYTE*>(text.data())},plain{};if(!CryptUnprotectData(&source,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain))throw std::runtime_error("speech_ready");
    struct Owner{BYTE* data;~Owner(){LocalFree(data);}} owner{plain.pbData};return PluginJson::parse(plain.pbData,plain.pbData+plain.cbData);
}
// Voice-specific protocol; the engine owns the child job and HTTP cancellation.
// Dialogue and PCM remain in memory. Only encrypted, short-lived bootstrap files
// touch disk; the loopback service requires a fresh per-worker bearer token.
class KanaSynthesizer {
    std::future<ayt::platform::ProcessResult> process_;
    std::atomic<bool> stopping_=false,running_=false;
    std::atomic<unsigned> starts_=0;
    std::string origin_,token_;
    std::filesystem::path boot_,ready_;
    void clean(){std::error_code ec;for(const auto& file:{boot_,ready_})if(!file.empty())std::filesystem::remove(file,ec);if(!ready_.empty()){auto pending=ready_;pending.replace_extension(".pending.dpapi");std::filesystem::remove(pending,ec);}boot_.clear();ready_.clear();}
    void stop(){stopping_=true;if(process_.valid()){try{process_.get();}catch(...){}}running_=false;origin_.clear();token_.clear();clean();}
public:
    ~KanaSynthesizer(){stop();}
    void close(){stop();}
    unsigned starts()const{return starts_.load();}
    bool running()const{return running_.load();}
    bool warm(const std::filesystem::path& dataRoot,const std::function<bool()>& cancelled){
        if(!origin_.empty()&&running_)return true;
        stop();
        try{
            const auto runtime=kanaRuntime();const auto root=dataRoot/"runtime"/"speech";std::filesystem::create_directories(root);const auto id=speechNonce();boot_=root/(id+".boot.dpapi");ready_=root/(id+".ready.dpapi");token_=speechNonce()+speechNonce();
            sealSpeechFile(boot_,{{"version",1},{"token",token_},{"ready",pathUtf8(ready_)}});
            ayt::platform::ProcessOptions options;options.executable=std::filesystem::u8path(runtime.at("pythonExecutable").get<std::string>());options.arguments={"-B",pathUtf8(speechDirectory()/"kana_worker.py"),"--serve",pathUtf8(boot_)};options.outputLimit=4096;options.cancellationRequested=[this]{return stopping_.load();};
            stopping_=false;running_=true;++starts_;process_=std::async(std::launch::async,[this,options]{try{auto result=ayt::platform::runProcess(options);running_=false;return result;}catch(...){running_=false;throw;}});
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(running_&&!cancelled()&&std::chrono::steady_clock::now()<deadline){
                if(std::filesystem::exists(ready_)){const auto response=openSpeechFile(ready_);const int port=response.at("port");if(response.at("version")!=1||port<1||port>65535)break;origin_="http://127.0.0.1:"+std::to_string(port);clean();return true;}
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }catch(...){}
        stop();return false;
    }
    std::string render(const PluginJson& request,const std::filesystem::path& dataRoot,const std::function<bool()>& cancelled,bool* silent=nullptr){
        if(cancelled()||!warm(dataRoot,cancelled))return {};
        ayt::net::http::Request call;call.origin=origin_;call.path="/synthesize";call.useSystemProxy=false;call.timeout=std::chrono::seconds(15);call.maxResponseBytes=8*1024*1024;call.cancelled=cancelled;call.headers={{"Content-Type","application/json"},{"Authorization","Bearer "+token_}};call.body=request.dump();
        const auto response=ayt::net::http::perform(call);if(response&&response.httpStatus==200&&!cancelled())return response.body;if(response&&response.httpStatus==204&&!cancelled()&&silent){*silent=true;return {};}
        stop();return {};
    }
};
}
