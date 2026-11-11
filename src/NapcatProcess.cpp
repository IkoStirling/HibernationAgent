#include <Hibernation/NapcatProcess.h>
#include <AYPlatform/ManagedProcess.h>
#include <AYPlatform/Desktop.h>
#include <AYCrypto.h>
#include <Windows.h>
#include <sddl.h>
#include <fstream>
#include <algorithm>
#include <vector>
#include <thread>
#include <cwctype>

namespace hibernation {
namespace {
using Json=nlohmann::json;
std::string utf8(const std::filesystem::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string digest(const std::string& s){return ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256)->hashString(reinterpret_cast<const std::uint8_t*>(s.data()),s.size());}
void require(bool ok){if(!ok)throw std::runtime_error("napcat_host_unavailable");}
Json read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()>=0&&f.tellg()<=1024*1024);std::string s(static_cast<size_t>(f.tellg()),0);f.seekg(0);f.read(s.data(),s.size());require(bool(f)||s.empty());return Json::parse(s);}
void noLinks(std::filesystem::path p){for(;!p.empty();p=p.parent_path()){auto a=GetFileAttributesW(p.c_str());require(a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_REPARSE_POINT));if(p==p.parent_path())break;}}
std::filesystem::path executable(){std::wstring s(32768,0);auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));require(n&&n<s.size());s.resize(n);return s;}
struct Handle {HANDLE v=nullptr;~Handle(){if(v&&v!=INVALID_HANDLE_VALUE)CloseHandle(v);}Handle()=default;Handle(const Handle&)=delete;};
struct Security {
    PSECURITY_DESCRIPTOR descriptor=nullptr;SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    Security(){Handle token;require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.v));DWORD n=0;GetTokenInformation(token.v,TokenUser,nullptr,0,&n);std::vector<unsigned char> data(n);require(GetTokenInformation(token.v,TokenUser,data.data(),n,&n));LPWSTR sid=nullptr;require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid));std::wstring policy=L"D:P(A;;GA;;;SY)(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);require(ConvertStringSecurityDescriptorToSecurityDescriptorW(policy.c_str(),SDDL_REVISION_1,&descriptor,nullptr));attributes.lpSecurityDescriptor=descriptor;}
    ~Security(){if(descriptor)LocalFree(descriptor);}
};
struct Location {
    std::filesystem::path config,runtime;std::wstring mutex,stop;
    explicit Location(std::filesystem::path p):config(std::filesystem::absolute(p).lexically_normal()){
        auto c=read(config);auto data=std::filesystem::u8path(c.at("dataRoot").get<std::string>());runtime=std::filesystem::absolute(data.is_absolute()?data:config.parent_path()/data)/"runtime";
        auto key=config.wstring();std::transform(key.begin(),key.end(),key.begin(),::towlower);auto id=digest(utf8(std::filesystem::path(key)));
        mutex=L"Local\\Aliyat.NapcatHost."+std::wstring(id.begin(),id.end());stop=mutex+L".stop";noLinks(runtime);
    }
};
bool active(const Location& l){Handle h;h.v=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,l.mutex.c_str());if(!h.v)return false;auto rc=WaitForSingleObject(h.v,0);if(rc==WAIT_OBJECT_0||rc==WAIT_ABANDONED){ReleaseMutex(h.v);return false;}return rc==WAIT_TIMEOUT;}
std::string fileHash(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()>=0&&f.tellg()<=64*1024*1024);std::string s(static_cast<size_t>(f.tellg()),0);f.seekg(0);f.read(s.data(),s.size());require(bool(f)||s.empty());return digest(s);}
std::filesystem::path stagedHost(const Location& l){
    auto source=executable().parent_path()/"components"/"napcat-host";noLinks(source);require(std::filesystem::is_directory(source));
    std::vector<std::filesystem::path> files;for(const auto& f:std::filesystem::directory_iterator(source)){require(f.is_regular_file());noLinks(f.path());auto ext=f.path().extension();require(ext==L".exe"||ext==L".dll");files.push_back(f.path());}require(!files.empty()&&files.size()<=32);std::sort(files.begin(),files.end());
    std::string content;std::vector<std::string> hashes;for(const auto& f:files){hashes.push_back(fileHash(f));content+=utf8(f.filename())+":"+hashes.back()+"\n";}
    auto destination=l.runtime/"component-host"/digest(content);noLinks(destination);std::filesystem::create_directories(destination);
    for(size_t i=0;i<files.size();++i){auto target=destination/files[i].filename();noLinks(target);if(!std::filesystem::exists(target))std::filesystem::copy_file(files[i],target);require(fileHash(target)==hashes[i]);}
    auto host=destination/"hibernation-napcat-host.exe";require(std::filesystem::is_regular_file(host));return host;
}
}
NapcatProcess::NapcatProcess(std::filesystem::path config):config_(std::move(config)){}
Json NapcatProcess::snapshot()const{
    try{Location l(config_);if(!active(l))return {{"active",false}};auto j=read(l.runtime/"napcat-host.identity.json");return {{"active",true},{"host_pid",j.at("host_pid")},{"host_started_ms",j.at("host_started_ms")}};}catch(...){return {{"active",false}};}
}
bool NapcatProcess::start(){
    try{Location l(config_);if(active(l))return true;auto host=stagedHost(l);auto launched=ayt::platform::launchDesktopApplication(host,{"--config",utf8(l.config)});if(!launched.launched)return false;for(int i=0;i<100;++i){if(snapshot().value("active",false))return true;std::this_thread::sleep_for(std::chrono::milliseconds(30));}return false;}catch(...){return false;}
}
bool NapcatProcess::stop(){
    try{Location l(config_);if(!active(l))return true;Handle stop;stop.v=OpenEventW(EVENT_MODIFY_STATE,FALSE,l.stop.c_str());if(!stop.v||!SetEvent(stop.v))return false;for(int i=0;i<300;++i){if(!active(l))return true;std::this_thread::sleep_for(std::chrono::milliseconds(30));}return false;}catch(...){return false;}
}
int runNapcatHost(const std::filesystem::path& path){
    try{
        Location l(path);Security security;Handle mutex;mutex.v=CreateMutexW(&security.attributes,FALSE,l.mutex.c_str());require(bool(mutex.v));auto acquired=WaitForSingleObject(mutex.v,0);if(acquired==WAIT_TIMEOUT)return 0;require(acquired==WAIT_OBJECT_0||acquired==WAIT_ABANDONED);
        Handle stop;stop.v=CreateEventW(&security.attributes,TRUE,FALSE,l.stop.c_str());require(bool(stop.v));ResetEvent(stop.v);
        auto c=read(l.config);auto n=c.at("desktopIntegration").at("napcat");auto dir=std::filesystem::u8path(n.at("directory").get<std::string>());require(!dir.empty());dir=std::filesystem::absolute(dir.is_absolute()?dir:l.config.parent_path()/dir);noLinks(dir);
        auto qq=std::filesystem::u8path(n.value("qqExecutable",std::string{}));if(!qq.empty())qq=std::filesystem::absolute(qq.is_absolute()?qq:l.config.parent_path()/qq);else for(auto p=dir;!p.empty()&&p!=p.parent_path();p=p.parent_path())if(std::filesystem::is_regular_file(p/"QQ.exe")){qq=p/"QQ.exe";break;}
        require(qq.filename()==L"QQ.exe"&&std::filesystem::is_regular_file(qq));noLinks(qq);for(const auto* name:{"NapCatWinBootMain.exe","NapCatWinBootHook.dll","napcat.mjs"})require(std::filesystem::is_regular_file(dir/name));
        ayt::platform::ProcessOptions options;options.executable=dir/"NapCatWinBootMain.exe";options.arguments={utf8(qq),utf8(dir/"NapCatWinBootHook.dll")};auto account=c.value("plugins",Json::object()).value("napcat",Json::object()).value("selfUserId",std::string{});require(account.size()<=20&&std::all_of(account.begin(),account.end(),[](char ch){return ch>='0'&&ch<='9';}));if(!account.empty())options.arguments.insert(options.arguments.end(),{"-q",account});options.workingDirectory=dir;options.captureOutput=true;options.outputLimit=16384;
        for(const auto& [key,file]:std::vector<std::pair<std::string,std::string>>{{"NAPCAT_PATCH_PACKAGE","qqnt.json"},{"NAPCAT_LOAD_PATH","loadNapCat.js"},{"NAPCAT_INJECT_PATH","NapCatWinBootHook.dll"},{"NAPCAT_LAUNCHER_PATH","NapCatWinBootMain.exe"},{"NAPCAT_MAIN_PATH","napcat.mjs"}})options.environmentOverrides[key]=utf8(dir/file);
        ayt::platform::ManagedProcess process;require(process.start(std::move(options)));std::filesystem::create_directories(l.runtime);auto identity=l.runtime/"napcat-host.identity.json";noLinks(identity);auto temporary=identity;temporary+=L".new";noLinks(temporary);{std::ofstream f(temporary);f<<Json{{"version",1},{"host_pid",GetCurrentProcessId()},{"host_started_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}}.dump();require(bool(f));}require(MoveFileExW(temporary.c_str(),identity.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));
        while(WaitForSingleObject(stop.v,100)==WAIT_TIMEOUT&&process.snapshot().active){}
        process.stop();ReleaseMutex(mutex.v);return 0;
    }catch(...){return 3;}
}
}
