#include <Hibernation/DesktopIntegration.h>
#include <AYPlatform/ManagedProcess.h>
#include <Hibernation/NapcatProcess.h>
#include <AYNetwork/HttpClient.h>
#include <AYCrypto.h>
#include <Windows.h>
#include <shellapi.h>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

#include <set>
#include <algorithm>

namespace hibernation {
using Json=nlohmann::json;
namespace {
std::string utf8(const std::filesystem::path& p){auto v=p.u8string();return {reinterpret_cast<const char*>(v.data()),v.size()};}
std::wstring wide(const std::string& s){auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n&&!s.empty())throw std::runtime_error("integration_utf8");std::wstring v(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),v.data(),n);return v;}
std::string hash(const std::string& v){auto h=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);return h->hashString(reinterpret_cast<const std::uint8_t*>(v.data()),v.size());}
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool origin(const std::string& v){
 if(!ayt::net::http::validOrigin(v)||!v.starts_with("http://"))return false;
 auto h=v.substr(7);for(const auto* name:{"127.0.0.1","localhost","[::1]"}){std::string n=name;if(h==n)return true;if(h.starts_with(n+":")){const auto port=h.substr(n.size()+1);if(!port.empty()&&port.size()<=5&&std::all_of(port.begin(),port.end(),[](unsigned char c){return c>='0'&&c<='9';})){auto p=std::stoi(port);return p>0&&p<=65535;}}}return false;
}
bool qqLink(const std::string& v){
 if(v.size()>4096||v.find_first_of("\r\n")!=v.npos||v.find('\0')!=v.npos)return false;
 const auto start=v.starts_with("https://")?8:v.starts_with("http://")?7:0;if(!start)return false;
 auto host=v.substr(start,v.find_first_of("/?#",start)-start);
 std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});
 return host=="qq.com"||host.ends_with(".qq.com");
}
Json readJson(const std::filesystem::path& p){
 std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f||f.tellg()<0||f.tellg()>128*1024)return Json::object();
 auto size=static_cast<std::size_t>(f.tellg());std::string s(size,0);f.seekg(0);f.read(s.data(),size);auto j=Json::parse(s,nullptr,false);return j.is_object()?j:Json::object();
}
std::filesystem::path executablePath(){std::wstring p(32768,0);auto n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));require(n&&n<p.size(),"integration_executable");p.resize(n);return p;}
Json result(bool ok,const char* code=""){return ok?Json{{"ok",true}}:Json{{"ok",false},{"error",code}};}
}
Json DesktopIntegration::settings(const Json& input){
 require(input.is_object(),"desktop_integration_config");for(const auto& [k,v]:input.items())require(k=="napcat","desktop_integration_field");
 auto n=input.value("napcat",Json::object());require(n.is_object(),"napcat_launch_config");
 for(const auto& [k,v]:n.items())require(k=="directory"||k=="qqExecutable"||k=="autoStart"||k=="webuiOrigin","napcat_launch_field");
 for(const auto* k:{"directory","qqExecutable","webuiOrigin"}){if(!n.contains(k))n[k]="";require(n[k].is_string(),"napcat_launch_path");const auto s=n[k].get<std::string>();require(s.size()<=2048&&s.find_first_of("\r\n") == s.npos&&s.find('\0')==s.npos&&s.find('"')==s.npos,"napcat_launch_path");}
 if(!n.contains("autoStart"))n["autoStart"]=false;require(n["autoStart"].is_boolean(),"napcat_launch_auto");
 require(n["webuiOrigin"]==""||origin(n["webuiOrigin"]),"napcat_webui_loopback_required");return {{"napcat",n}};
}
struct DesktopIntegration::Impl {
 std::filesystem::path config,exe;std::wstring registry,key,command;mutable std::mutex mutex;std::condition_variable wake;
 Json configuration=DesktopIntegration::settings(Json::object()),snapshot={{"running",false},{"managed",false},{"phase","stopped"},{"issue",""}};
 std::string qr,account,credential,credentialOrigin;bool configured=false,autoWas=false;std::atomic<bool> shutting=false,wanted=false,launchRequested=false,stopRequested=false,refreshRequested=false;
 NapcatProcess process;std::thread thread;std::atomic<bool> preserveOnExit=false;std::shared_ptr<ComponentInstaller> installer;
 Impl(std::filesystem::path c,std::wstring r):config(std::filesystem::absolute(c)),exe(executablePath()),registry(std::move(r)),process(config){
  key=L"Aliyat.XiaoAi."+wide(hash(utf8(config)).substr(0,16));
  command=L"\""+exe.wstring()+L"\" --config \""+config.wstring()+L"\" --quick-start";
  thread=std::thread([this]{run();});
 }
 bool startup()const{HKEY h=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,registry.c_str(),0,KEY_QUERY_VALUE,&h)!=ERROR_SUCCESS)return false;DWORD bytes=0,type=0;auto rc=RegQueryValueExW(h,key.c_str(),nullptr,&type,nullptr,&bytes);std::wstring value(bytes/sizeof(wchar_t),0);if(rc==ERROR_SUCCESS&&type==REG_SZ&&bytes<=65536)rc=RegQueryValueExW(h,key.c_str(),nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&bytes);RegCloseKey(h);if(rc!=ERROR_SUCCESS||type!=REG_SZ)return false;while(!value.empty()&&value.back()==0)value.pop_back();return value==command;}
 bool setStartup(bool enabled){HKEY h=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,registry.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&h,nullptr)!=ERROR_SUCCESS)return false;const auto rc=enabled?RegSetValueExW(h,key.c_str(),0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))):RegDeleteValueW(h,key.c_str());RegCloseKey(h);return rc==ERROR_SUCCESS||(!enabled&&rc==ERROR_FILE_NOT_FOUND);}
 Json post(const std::string& o,const std::string& path,const Json& body,bool auth=true){
  ayt::net::http::Request req;req.origin=o;req.path="/api/"+path;req.method="POST";req.body=body.dump();req.timeout=std::chrono::milliseconds(1800);req.maxResponseBytes=128*1024;req.cancelled=[this]{return shutting.load();};req.headers={{"Content-Type","application/json"}};if(auth)req.headers.emplace_back("Authorization","Bearer "+credential);
  auto r=ayt::net::http::perform(req);if(!r||r.httpStatus!=200)return Json();auto j=Json::parse(r.body,nullptr,false);return j.is_object()?j:Json();
 }
 std::filesystem::path directory(const Json& n){auto d=std::filesystem::u8path(n.at("directory").get<std::string>());return d.empty()?d:std::filesystem::absolute(d.is_absolute()?d:config.parent_path()/d);}
 std::string webOrigin(const Json& n,const std::filesystem::path& d){
  auto o=n.at("webuiOrigin").get<std::string>();if(o.empty()){auto c=readJson(d/"config"/"webui.json");auto p=c.value("port",6099);if(p<1||p>65535)p=6099;o="http://127.0.0.1:"+std::to_string(p);}return o;
 }
 bool validDirectory(const std::filesystem::path& d){if(d.empty())return false;for(const auto* f:{"NapCatWinBootMain.exe","NapCatWinBootHook.dll","napcat.mjs"})if(!std::filesystem::is_regular_file(d/f))return false;return true;}
 std::filesystem::path qqExecutable(const Json& n,const std::filesystem::path& d){
  auto q=std::filesystem::u8path(n.at("qqExecutable").get<std::string>());
  if(!q.empty()){if(!q.is_absolute())q=config.parent_path()/q;return q;}
  for(auto p=d;!p.empty()&&p!=p.parent_path();p=p.parent_path())if(std::filesystem::is_regular_file(p/"QQ.exe"))return p/"QQ.exe";
  return {};
 }
 void publish(bool running,bool managed,const std::string& phase,const std::string& issue,const std::string& o){std::lock_guard lock(mutex);snapshot={{"running",running},{"managed",managed},{"phase",phase},{"issue",issue},{"webui_url",o}};if(!running)qr.clear();}
 void run(){
  while(!shutting){
   const bool refreshNow=refreshRequested.exchange(false);auto pollDelay=std::chrono::seconds(3);
   try{
    Json n;std::string quick;{std::lock_guard lock(mutex);n=configuration.at("napcat");quick=account;}
    auto d=directory(n);const auto o=webOrigin(n,d);
    if(stopRequested.exchange(false)){process.stop();credential.clear();wanted=false;publish(false,false,"stopped","",o);}
    if(launchRequested.exchange(false)&&wanted&&!process.snapshot().value("active",false)){
     bool already=false;
     if(validDirectory(d)){auto wc=readJson(d/"config"/"webui.json");auto token=wc.value("token",std::string{});if(!token.empty()){auto a=post(o,"auth/login",{{"hash",hash(token+".napcat")}},false);if(a.is_object()&&a.value("code",-1)==0&&a.contains("data")&&a["data"].is_object()){already=true;credential=a["data"].value("Credential",std::string{});credentialOrigin=o;}}}
     if(!already){
      auto qq=qqExecutable(n,d);
      if(!validDirectory(d)||qq.empty()||qq.filename()!=L"QQ.exe"||!std::filesystem::is_regular_file(qq)){wanted=false;publish(false,false,"stopped","napcat_installation_missing",o);}
      else{
       if(process.start()){credential.clear();publish(true,true,"starting","",o);}else{wanted=false;publish(false,false,"stopped","napcat_host_start_failed",o);}
      }
     }
    }
    const bool owned=process.snapshot().value("active",false);if(!validDirectory(d)){publish(owned,owned,owned?"starting":"stopped","napcat_installation_missing",o);}
    else{
     if(credentialOrigin!=o){credential.clear();credentialOrigin=o;}
     if(credential.empty()){
      auto configValue=readJson(d/"config"/"webui.json");auto token=configValue.value("token",std::string{});
      if(token.empty())publish(owned,owned,owned?"starting":"stopped","napcat_webui_token_missing",o);
      else{auto a=post(o,"auth/login",{{"hash",hash(token+".napcat")}},false);if(a.is_object()&&a.value("code",-1)==0&&a.contains("data")&&a["data"].is_object()){auto data=a["data"];if(data.value("require2FA",false)){pollDelay=std::chrono::seconds(30);publish(true,owned,"login_required","napcat_webui_2fa_required",o);}else credential=data.value("Credential",std::string{});}else publish(owned,owned,owned?"starting":"stopped","napcat_webui_unreachable_or_auth_failed",o);}
     }
     if(!credential.empty()){
      if(refreshNow)post(o,"QQLogin/RefreshQRcode",Json::object());
      auto value=post(o,"QQLogin/CheckLoginStatus",Json::object());
      if(value.is_object()&&value.value("code",-1)==0&&value.contains("data")&&value["data"].is_object()){
       const auto& data=value["data"];const bool logged=data.value("isLogin",false);auto url=data.value("qrcodeurl",std::string{});if(!qqLink(url))url.clear();
       if(!logged&&url.empty()){auto code=post(o,"QQLogin/GetQQLoginQrcode",Json::object());if(code.is_object()&&code.value("code",-1)==0&&code.contains("data")&&code["data"].is_object()){url=code["data"].value("qrcode",std::string{});if(!qqLink(url))url.clear();}}
       publish(true,owned,logged?"online":url.empty()?"preparing_login":"login_required","",o);
       {std::lock_guard lock(mutex);qr=logged?"":url;}
      }else{credential.clear();publish(owned,owned,owned?"starting":"stopped","napcat_webui_unreachable_or_auth_failed",o);}
     }
    }
   }catch(...){const bool owned=process.snapshot().value("active",false);publish(owned,owned,"error","napcat_management_failed","");}
   std::unique_lock lock(mutex);wake.wait_for(lock,pollDelay,[this]{return shutting||launchRequested||stopRequested||refreshRequested;});
  }
  if(!preserveOnExit)process.stop();
 }
};
DesktopIntegration::DesktopIntegration(std::filesystem::path p,std::wstring r,std::shared_ptr<ComponentInstaller> installer):impl_(std::make_unique<Impl>(std::move(p),std::move(r))){impl_->installer=std::move(installer);}
DesktopIntegration::~DesktopIntegration(){stop();}
bool DesktopIntegration::canConfigure(const Json& v)const{auto n=settings(v);std::lock_guard lock(impl_->mutex);return !impl_->process.snapshot().value("active",false)||(n["napcat"]["directory"]==impl_->configuration["napcat"]["directory"]&&n["napcat"]["qqExecutable"]==impl_->configuration["napcat"]["qqExecutable"]&&n["napcat"]["webuiOrigin"]==impl_->configuration["napcat"]["webuiOrigin"]);}
void DesktopIntegration::configure(const Json& v,const std::string& quick){
 auto n=settings(v);require(quick.empty()||(quick.size()<=20&&std::all_of(quick.begin(),quick.end(),[](unsigned char c){return c>='0'&&c<='9';})),"napcat_quick_account");
 std::lock_guard lock(impl_->mutex);auto autoStart=n["napcat"].value("autoStart",false);
 impl_->configuration=std::move(n);impl_->account=quick;
 if(autoStart&&(!impl_->configured||!impl_->autoWas)){impl_->wanted=true;impl_->launchRequested=true;}
 impl_->autoWas=autoStart;impl_->configured=true;impl_->wake.notify_all();
}
Json DesktopIntegration::status()const{std::lock_guard lock(impl_->mutex);auto v=impl_->snapshot;v["configuration"]=impl_->configuration;v["startup_enabled"]=impl_->startup();v["startup_minimized"]=true;v["operator_only"]=true;v["restart_preserves_napcat"]=true;v["preserve_on_exit"]=impl_->preserveOnExit.load();v["process_host"]=impl_->process.snapshot();v["installation"]=componentInstallationStatus(impl_->installer);return v;}
Json DesktopIntegration::login()const{std::lock_guard lock(impl_->mutex);auto v=impl_->snapshot;v["qr_url"]=impl_->qr;return v;}
Json DesktopIntegration::action(const Json& v){
 const auto action=v.value("action",std::string{});
 if(action=="component_install"){
  if(!impl_->installer)return result(false,"engine_installation_not_connected");
  const auto id=v.value("component_id",std::string{}),version=v.value("version",std::string{}),operation=v.value("operation",std::string("install"));
  auto valid=[](const std::string& s){return s.size()<=100&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_';});};
  if(id.empty()||!std::isalnum(static_cast<unsigned char>(id.front()))||!valid(id)||!valid(version)||!std::set<std::string>{"install","update","repair","remove"}.contains(operation))return result(false,"component_request_invalid");
  for(const auto& [key,value]:v.items())if(key!="action"&&key!="component_id"&&key!="version"&&key!="operation")return result(false,"component_request_invalid");
  ComponentInstallRequest request;request.componentId=id;request.version=version;request.operation=operation=="update"?ComponentInstallRequest::Operation::Update:operation=="repair"?ComponentInstallRequest::Operation::Repair:operation=="remove"?ComponentInstallRequest::Operation::Remove:ComponentInstallRequest::Operation::Install;return impl_->installer->submit(request);
 }
 if(action=="startup"){if(!v.contains("enabled")||!v["enabled"].is_boolean())return result(false,"startup_boolean_required");return result(impl_->setStartup(v["enabled"]),"startup_registration_failed");}
 if(action=="start"){impl_->wanted=true;impl_->launchRequested=true;impl_->wake.notify_all();return result(true);}
 if(action=="prepare_restart"||action=="cancel_restart"){impl_->preserveOnExit=action=="prepare_restart";return result(true);}
 if(action=="stop"){if(!impl_->process.snapshot().value("active",false)&&!impl_->launchRequested.load())return result(false,"napcat_instance_not_owned");impl_->wanted=false;impl_->launchRequested=false;impl_->stopRequested=true;impl_->wake.notify_all();return result(true);}
 if(action=="refresh_qr"){impl_->refreshRequested=true;impl_->wake.notify_all();return result(true);}
 if(action=="open_admin"||action=="open_login_link"){
  auto s=login();const auto url=action=="open_admin"?s.value("webui_url",std::string{}):s.value("qr_url",std::string{});
  if(action=="open_admin"?!origin(url):!qqLink(url))return result(false,"napcat_link_not_ready");
  return result(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32,"napcat_open_link_failed");
 }
 return result(false,"integration_action_unknown");
}
void DesktopIntegration::stop(){if(!impl_)return;impl_->shutting=true;impl_->wake.notify_all();if(impl_->thread.joinable())impl_->thread.join();}
}
