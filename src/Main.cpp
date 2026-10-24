#include <Hibernation/Application.h>
#include <AYIO/File.h>
#include <atomic>
#include <condition_variable>
#include <csignal>
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
int main(int argc,char** argv) {
    std::filesystem::path configFile,readyFile;
    for(int i=1;i<argc;++i){const std::string option=argv[i];if(option=="--config"&&i+1<argc)configFile=std::filesystem::u8path(argv[++i]);else if(option=="--ready-file"&&i+1<argc)readyFile=std::filesystem::u8path(argv[++i]);else {std::cerr<<"usage: hibernation-agent --config <path> --ready-file <path>\n";return 2;}}
    if(configFile.empty()||readyFile.empty())return 2;
    try {
        std::signal(SIGINT,signalHandler);std::signal(SIGTERM,signalHandler);
        auto cfg=loadConfig(configFile);std::filesystem::create_directories(cfg.dataRoot/"runtime");
        InstanceLock instance(cfg.dataRoot/"runtime"/"service.lock");if(!instance.valid()){std::cerr<<"archive_already_in_use\n";return 7;}
        Application app(std::move(cfg));if(!app.initialize()){std::cerr<<"application_initialize_failed\n";return 3;}
        std::mutex mutex;std::deque<std::shared_ptr<Command>> mailbox;std::atomic<bool> accepting=true;
        LocalService service;LocalServiceOptions options;options.credentialEnvironment=app.config().credentialEnvironment;options.port=app.config().port;
        options.maxMessageBytes=2*1024*1024;options.allowSameOriginBrowser=true;
        for(const auto& item:std::vector<std::tuple<std::string,std::string,std::string>>{{"/","text/html; charset=utf-8","index.html"},{"/ui/app.js","application/javascript; charset=utf-8","app.js"},{"/ui/style.css","text/css; charset=utf-8","style.css"},{"/ui/icon.svg","image/svg+xml","icon.svg"}})
            options.publicAssets.push_back({std::get<0>(item),std::get<1>(item),readFile(app.config().uiRoot/std::get<2>(item),512*1024)});
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
        const auto ready=Json{{"version",1},{"port",service.port()},{"pid",pid},{"application","HibernationAgent"}}.dump();
        std::filesystem::create_directories(readyFile.parent_path());auto name=readyFile.u8string();
        if(!ayt::io::File::atomicWriteUtf8({reinterpret_cast<const char*>(name.data()),name.size()},ready.data(),ready.size()))return 5;
        std::cout<<"HibernationAgent ready on loopback port "<<service.port()<<"\n"<<std::flush;
        while(!interrupted&&!app.stopping()) {
            std::deque<std::shared_ptr<Command>> batch;{std::lock_guard guard(mutex);batch.swap(mailbox);}
            for(const auto& command:batch){if(command->cancelled)continue;command->answer.set_value(app.request(command->request));}
            app.tick();std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        accepting=false;
        {std::lock_guard guard(mutex);for(auto& command:mailbox)command->answer.set_value({503,"{\"error\":\"stopping\"}"});mailbox.clear();}
        service.stop();app.stop();return 0;
    }catch(const std::exception&){std::cerr<<"application_configuration_or_storage_error\n";return 6;}
}
