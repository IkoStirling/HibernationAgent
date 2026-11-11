#include <Hibernation/Vault.h>
#include <Hibernation/NetworkRouting.h>
#include <AYCrypto.h>
#include <array>
#include <Hibernation/Application.h>
#include <Hibernation/DesktopHost.h>
#include <Hibernation/StartupSplash.h>
#include <AYNetwork/HttpClient.h>
#include <AYIO/File.h>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <algorithm>
#include <vector>
#include <string>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#endif
using namespace hibernation;
using namespace ayt::app::services;
namespace {
std::atomic<bool> interrupted=false;
void signalHandler(int){interrupted=true;}
struct Command {LocalRequest request;std::promise<LocalReply> answer;std::atomic<bool> cancelled=false;};
struct InstanceLock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit InstanceLock(const std::filesystem::path& path){handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);}
    ~InstanceLock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    bool valid()const{return handle!=INVALID_HANDLE_VALUE;}
#else
    int handle=-1;
    explicit InstanceLock(const std::filesystem::path& path){handle=open(path.c_str(),O_CREAT|O_RDWR,0600);if(handle>=0&&flock(handle,LOCK_EX|LOCK_NB)!=0){close(handle);handle=-1;}}
    ~InstanceLock(){if(handle>=0)close(handle);}
    bool valid()const{return handle>=0;}
#endif
};
}
int launch(int argc,char** argv) {
    std::filesystem::path configFile,readyFile;bool desktop=false,checkConfig=false,quickStart=false;
    for(int i=1;i<argc;++i){const std::string option=argv[i];if(option=="--config"&&i+1<argc)configFile=std::filesystem::u8path(argv[++i]);else if(option=="--ready-file"&&i+1<argc)readyFile=std::filesystem::u8path(argv[++i]);else if(option=="--desktop")desktop=true;else if(option=="--quick-start"){quickStart=true;desktop=true;}else if(option=="--check-config")checkConfig=true;else {std::cerr<<"usage: hibernation-agent --config <path> [--check-config | --ready-file <path> [--desktop]]\n";return 2;}}
    if(configFile.empty()||(!checkConfig&&!quickStart&&readyFile.empty()))return 2;
    // A deployment preflight must not open archives, create the workspace, load
    // plugins, read stored keys, or connect a second mailbox to an external chat.
    if(checkConfig){try{(void)loadConfig(configFile);std::cout<<"Configuration valid (syntax and policy only).\n";return 0;}catch(...){std::cerr<<"configuration_validation_failed\n";return 3;}}
#ifdef _WIN32
    // Workers may import adjacent Python modules from an immutable installed
    // version; their caches belong in data roots, never as undeclared files here.
    _putenv_s("PYTHONDONTWRITEBYTECODE","1");
    // Establish one physical coordinate space before plugins create layered
    // surfaces; DPI virtualization otherwise disagrees with color-key hit tests.
    if(desktop)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    try {
        std::signal(SIGINT,signalHandler);std::signal(SIGTERM,signalHandler);
        auto cfg=loadConfig(configFile);directNetworkEnvironment(cfg.plugins.value("television",nlohmann::json::object()).value("browserUseSystemProxy",false));cfg.desktopEnabled=desktop;std::filesystem::create_directories(cfg.dataRoot/"runtime");
        if(!cfg.pluginRootExplicit){
#ifdef _WIN32
            std::wstring executable(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));if(!length||length>=executable.size())throw std::runtime_error("executable_path");executable.resize(length);cfg.pluginRoot=std::filesystem::path(executable).parent_path()/"plugins";
#else
            cfg.pluginRoot=std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path()/"plugins";
#endif
        }
        InstanceLock instance(cfg.dataRoot/"runtime"/"service.lock");if(!instance.valid()){if(quickStart)return 0;std::cerr<<"archive_already_in_use\n";return 7;}
        if(quickStart){
            if(readyFile.empty())readyFile=cfg.dataRoot/"runtime"/"ready.json";
            const auto secretFile=cfg.dataRoot/"runtime"/".ipc-token.dpapi";
            auto token=loadUserSecret(secretFile);
            const bool valid=token.size()==64&&std::all_of(token.begin(),token.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});
            if(!valid){
                std::array<std::uint8_t,32> random{};if(!ayt::crypto::tryGenerateRandomBytes(random.data(),random.size()))throw std::runtime_error("startup_random");
                constexpr char digits[]="0123456789abcdef";token.clear();for(auto v:random){token.push_back(digits[v>>4]);token.push_back(digits[v&15]);}
                if(!saveUserSecret(secretFile,token))throw std::runtime_error("startup_credential");
            }
            if(_putenv_s(cfg.credentialEnvironment.c_str(),token.c_str()))throw std::runtime_error("startup_credential");
        }
        StartupSplash splash(desktop&&!quickStart&&cfg.frontend.value("startupAnimation",true),cfg.frontend.value("reducedMotion",false));
        Application app(std::move(cfg));if(!app.initialize()){std::cerr<<"application_initialize_failed\n";return 3;}
        std::mutex mutex;std::deque<std::shared_ptr<Command>> mailbox;std::atomic<bool> accepting=true;
        LocalService service;LocalServiceOptions options;options.credentialEnvironment=app.config().credentialEnvironment;options.port=app.config().port;
        options.maxMessageBytes=12*1024*1024;options.allowSameOriginBrowser=true;
        for(const auto& item:std::vector<std::tuple<std::string,std::string,std::string>>{{"/ui/desktop-integration.js","application/javascript; charset=utf-8","desktop-integration.js"},{"/ui/desktop-integration.css","text/css; charset=utf-8","desktop-integration.css"},{"/ui/napcat-login.html","text/html; charset=utf-8","napcat-login.html"},{"/ui/napcat-login.js","application/javascript; charset=utf-8","napcat-login.js"},{"/ui/qrcodegen.js","application/javascript; charset=utf-8","qrcodegen.js"},{"/","text/html; charset=utf-8","index.html"},{"/ui/app.js","application/javascript; charset=utf-8","app.js"},{"/ui/desktop-supplies.js","application/javascript; charset=utf-8","desktop-supplies.js"},{"/ui/desktop-supplies.css","text/css; charset=utf-8","desktop-supplies.css"},{"/ui/continuous-tasks.js","application/javascript; charset=utf-8","continuous-tasks.js"},{"/ui/dashboard-order.js","application/javascript; charset=utf-8","dashboard-order.js"},{"/ui/dashboard-panels.js","application/javascript; charset=utf-8","dashboard-panels.js"},{"/ui/memory-workshop.js","application/javascript; charset=utf-8","memory-workshop.js"},{"/ui/style-lora.js","application/javascript; charset=utf-8","style-lora.js"},{"/ui/mentor-session.js","application/javascript; charset=utf-8","mentor-session.js"},{"/ui/pixel-canvas-examples.js","application/javascript; charset=utf-8","pixel-canvas-examples.js"},{"/ui/pixel-canvas.js","application/javascript; charset=utf-8","pixel-canvas.js"},{"/ui/pixel-canvas.css","text/css; charset=utf-8","pixel-canvas.css"},{"/ui/runtime-control.js","application/javascript; charset=utf-8","runtime-control.js"},{"/ui/speech-style.js","application/javascript; charset=utf-8","speech-style.js"},{"/ui/runtime-control.css","text/css; charset=utf-8","runtime-control.css"},{"/ui/pricing-settings.js","application/javascript; charset=utf-8","pricing-settings.js"},{"/ui/plugin-forms.js","application/javascript; charset=utf-8","plugin-forms.js"},{ "/ui/skills-settings.js","application/javascript; charset=utf-8","skills-settings.js"},{"/ui/style.css","text/css; charset=utf-8","style.css"},{"/ui/icon.svg","image/svg+xml","icon.svg"},{"/ui/chat.html","text/html; charset=utf-8","chat.html"},{"/ui/chat.js","application/javascript; charset=utf-8","chat.js"},{"/ui/chat.css","text/css; charset=utf-8","chat.css"}})
            options.publicAssets.push_back({std::get<0>(item),std::get<1>(item),readFile(app.config().uiRoot/std::get<2>(item),512*1024),std::get<0>(item)=="/ui/chat.html"});
        if(!service.start(options,[&](const LocalRequest& request)->LocalReply {
            if(!accepting)return {503,"{\"error\":\"stopping\"}"};
            auto command=std::make_shared<Command>();command->request=request;auto future=command->answer.get_future();
            {std::lock_guard guard(mutex);if(mailbox.size()>=32)return {429,"{\"error\":\"mailbox_full\"}"};mailbox.push_back(command);}
            if(future.wait_for(std::chrono::seconds(8))!=std::future_status::ready){command->cancelled=true;return {504,"{\"error\":\"owner_timeout\"}"};}return future.get();
        })){std::cerr<<"local_service_start_failed\n";return 4;}
#ifdef _WIN32
        const auto pid=static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        const auto pid=static_cast<std::uint64_t>(getpid());
#endif
        std::unique_ptr<DesktopHost> desktopHost;
        if(desktop){const auto url="http://127.0.0.1:"+std::to_string(service.port())+"/#session="+ayt::net::http::environmentCredential(app.config().credentialEnvironment);desktopHost=std::make_unique<DesktopHost>(app,url);if(!desktopHost->create(quickStart)){std::cerr<<"desktop_initialize_failed\n";return 8;}}
        const auto ready=Json{{"version",1},{"port",service.port()},{"pid",pid},{"application","HibernationAgent"},{"desktop",desktop}}.dump();
        std::filesystem::create_directories(readyFile.parent_path());auto name=readyFile.u8string();
        if(!ayt::io::File::atomicWriteUtf8({reinterpret_cast<const char*>(name.data()),name.size()},ready.data(),ready.size()))return 5;
        splash.ready();std::cout<<"HibernationAgent ready on loopback port "<<service.port()<<"\n"<<std::flush;
        while(!interrupted&&!app.stopping()) {
            if(desktopHost&&!desktopHost->pump())break;
            std::deque<std::shared_ptr<Command>> batch;{std::lock_guard guard(mutex);batch.swap(mailbox);}
            for(const auto& command:batch){if(command->cancelled)continue;if(command->request.path=="/api/desktop/integration/show-login"){const bool shown=desktopHost&&desktopHost->showNapcatLogin();command->answer.set_value({shown?200:409,shown?"{\"shown\":true}":"{\"error\":\"login_window_unavailable\"}"});}else if(command->request.path=="/api/desktop/show"){if(desktopHost)desktopHost->show();command->answer.set_value({desktopHost?200:409,desktopHost?"{\"shown\":true}":"{\"error\":\"desktop_unavailable\"}"});}else command->answer.set_value(app.request(command->request));}
            app.tick();if(desktopHost)desktopHost->update();std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        accepting=false;
        {std::lock_guard guard(mutex);for(auto& command:mailbox)command->answer.set_value({503,"{\"error\":\"stopping\"}"});mailbox.clear();}
        service.stop();app.stop();return 0;
    }catch(const std::exception& failure){
        // Only our bounded symbolic error codes are loggable. Library messages
        // may quote input data and must never become plaintext memory logs.
        const std::string code=failure.what();const bool symbolic=code.size()<=96&&!code.empty()&&std::all_of(code.begin(),code.end(),[](unsigned char c){return (c>='a'&&c<='z')||c=='_'||c==':';});
        std::cerr<<(symbolic?code:"application_configuration_or_storage_error")<<'\n';return 6;
    }
}
#ifdef _WIN32
int wmain(int argc,wchar_t** arguments){
    try{std::vector<std::string> values;values.reserve(argc);for(int i=0;i<argc;++i){const auto length=static_cast<int>(wcslen(arguments[i]));const auto bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[i],length,nullptr,0,nullptr,nullptr);if(length&&!bytes)return 2;std::string value(bytes,'\0');if(bytes)WideCharToMultiByte(CP_UTF8,0,arguments[i],length,value.data(),bytes,nullptr,nullptr);values.push_back(std::move(value));}std::vector<char*> argv;for(auto& value:values)argv.push_back(value.data());argv.push_back(nullptr);return launch(argc,argv.data());}catch(...){std::cerr<<"command_line_encoding\n";return 2;}
}
#else
int main(int argc,char** argv){return launch(argc,argv);}
#endif
