#include <Hibernation/DesktopIntegration.h>
#include <AYPlatform/ManagedProcess.h>
#include <AYCrypto.h>
#include <httplib.h>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <functional>
using Json=nlohmann::json;using namespace hibernation;
namespace {
int checks=0;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);++checks;std::cout<<"PASS "<<why<<'\n';}
void await(const std::function<bool()>& probe,const char* why){for(int i=0;i<220;++i){if(probe())return;std::this_thread::sleep_for(std::chrono::milliseconds(80));}throw std::runtime_error(why);}
void write(const std::filesystem::path& p,const std::string& v){std::ofstream f(p);f<<v;}
std::string text(const std::filesystem::path& p){auto u=p.u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
struct InstallerFixture:ComponentInstaller {
 int submitted=0;
 Json status()const override{return {{"available",true},{"provider","synthetic"}};}
 Json submit(const ComponentInstallRequest& r)override{++submitted;return {{"ok",true},{"accepted",true},{"completed",false},{"job_id","fixture-job"},{"component_id",r.componentId}};}
};
}
int main(int argc,char** argv){
 try{
  if(argc<3)return 2;const auto fixture=std::filesystem::absolute(argv[1]);
  const auto root=std::filesystem::absolute(argv[2])/("run-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  const auto install=root/"napcat";std::filesystem::create_directories(install/"config");
  int port=0;{httplib::Server probe;port=probe.bind_to_any_port("127.0.0.1");std::thread listening([&]{probe.listen_after_bind();});probe.wait_until_ready();probe.stop();listening.join();}check(port>0,"fixture chooses its own loopback port");
  const std::string token="SYNTHETIC_WEBUI_SECRET_NOT_A_REAL_ACCOUNT";
  auto h=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);const auto password=token+".napcat";const auto digest=h->hashString(reinterpret_cast<const std::uint8_t*>(password.data()),password.size());
  write(install/"config"/"webui.json",Json{{"token",token},{"port",port},{"expectedHash",digest}}.dump());
  for(const auto* name:{"QQ.exe","NapCatWinBootHook.dll","napcat.mjs","qqnt.json","loadNapCat.js"})write(install/name,"synthetic fixture");
  std::filesystem::copy_file(fixture,install/"NapCatWinBootMain.exe");
  for(const auto& file:std::filesystem::directory_iterator(fixture.parent_path()))if(file.is_regular_file()&&file.path().extension()==L".dll")std::filesystem::copy_file(file.path(),install/file.path().filename(),std::filesystem::copy_options::overwrite_existing);
  const auto config=root/"app.json";
  const auto registry=L"Software\\Aliyat\\IntegrationTests\\"+std::to_wstring(GetCurrentProcessId());
  auto settings=DesktopIntegration::settings({{"napcat",{{"directory",text(install)},{"autoStart",true}}}});
  write(config,Json{{"dataRoot",text(root/"data")},{"desktopIntegration",settings}}.dump());
  {
   DesktopIntegration manager(config,registry);
   check(!manager.status()["startup_enabled"].get<bool>(),"startup is initially disabled in the isolated test registry");
   check(manager.action({{"action","startup"},{"enabled",true}})["ok"],"explicit action registers quick startup");
   check(manager.status()["startup_enabled"],"startup status reflects registry rather than an unsaved checkbox");
   check(manager.action({{"action","startup"},{"enabled",false}})["ok"],"explicit action removes only this installation entry");
   check(!manager.status()["startup_enabled"].get<bool>(),"startup is disabled after removal");
   check(!manager.status()["installation"]["available"].get<bool>(),"missing engine installation adapter is explicitly unavailable");
   check(manager.action({{"action","component_install"},{"component_id","napcat"}}).value("error",std::string{})=="engine_installation_not_connected","no adapter cannot pretend to install or download");
   bool bad=false;try{DesktopIntegration::settings({{"napcat",{{"webuiOrigin","https://example.invalid"}}}});}catch(...){bad=true;}check(bad,"remote WebUI origins cannot receive local administrative credentials");
   manager.configure(settings,"");try{await([&]{return manager.login().value("phase",std::string{})=="login_required";},"owned fixture did not reach QR login");}catch(...){std::cerr<<manager.status().dump()<<" root="<<text(root)<<"\n";throw;}
   auto live=manager.status();check(live["running"]&&live["managed"],"background instance is owned and its local WebUI is ready");
   const auto originalQr=manager.login().value("qr_url",std::string{});
   check(originalQr.starts_with("https://ssl.ptlogin2.qq.com/"),"QR data comes from authenticated local status");
   check(live.dump().find(token)==std::string::npos&&live.dump().find("CHILD_OUTPUT_SECRET")==std::string::npos,"status excludes WebUI secrets and captured child logs");
   check(!live.contains("qr_url"),"normal dashboard status does not include the login QR token");
   auto changed=settings;changed["napcat"]["directory"]=text(root/"other");check(!manager.canConfigure(changed),"live instance must stop before its launch directory is changed");
   std::string pid;{std::ifstream f(install/"fixture.pid");f>>pid;}
   manager.action({{"action","start"}});std::this_thread::sleep_for(std::chrono::milliseconds(250));std::string nextPid;{std::ifstream f(install/"fixture.pid");f>>nextPid;}check(pid==nextPid,"repeated start does not launch a duplicate process");
   manager.action({{"action","refresh_qr"}});await([&]{return manager.login().value("qr_url",std::string{})!=originalQr;},"QR refresh did not update");
   check(true,"QR refresh returns a new local login URL");
   write(install/"fixture-invalid-qr","1");await([&]{return manager.login().value("qr_url",std::string{}).empty();},"foreign QR URL was not filtered");
   check(!manager.action({{"action","open_login_link"}})["ok"].get<bool>(),"an untrusted QR navigation cannot open");
   std::filesystem::remove(install/"fixture-invalid-qr");
   write(install/"fixture-online","1");await([&]{return manager.login().value("phase",std::string{})=="online";},"online phase did not arrive");
   check(manager.login().value("qr_url",std::string{}).empty(),"logged-in status removes stale QR material");
   check(manager.action({{"action","stop"}})["ok"],"stop requests cancellation of the owned process tree");
   await([&]{return !manager.status().value("running",true);},"owned instance did not stop");
   manager.configure(settings,"");std::this_thread::sleep_for(std::chrono::milliseconds(350));check(!manager.status().value("running",true),"unrelated configuration save does not undo a manual stop");
   manager.stop();
  }
  {
   std::string pid;Json identity;
   {
    DesktopIntegration manager(config,registry);manager.configure(settings,"");
    await([&]{return manager.status().value("phase",std::string{})=="online"&&manager.status().value("managed",false);},"restart fixture not owned");
    {std::ifstream f(install/"fixture.pid");f>>pid;}identity=manager.status()["process_host"];
    check(manager.action({{"action","prepare_restart"}})["ok"],"operator can arm a backend-only restart");
    manager.stop();
   }
   httplib::Client client("127.0.0.1",port);check(bool(client.Get("/fixture/health")),"NapCat remains reachable while backend owner is absent");
   {
    DesktopIntegration manager(config,registry);auto manual=settings;manual["napcat"]["autoStart"]=false;manager.configure(manual,"");
    await([&]{return manager.status().value("running",false)&&manager.status().value("managed",false);},"replacement backend did not attach");
    auto after=manager.status()["process_host"];std::string next;{std::ifstream f(install/"fixture.pid");f>>next;}
    check(pid==next&&identity==after,"restart retains both exact host generation and exact NapCat PID without autoStart");
    check(!manager.status()["preserve_on_exit"].get<bool>(),"restart preservation is one exit only");manager.stop();
   }
   await([&]{return !client.Get("/fixture/health");},"normal exit did not close resumed owned NapCat");
  }
  {
   ayt::platform::ManagedProcess external;ayt::platform::ProcessOptions options;options.executable=install/"NapCatWinBootMain.exe";options.arguments={text(install/"QQ.exe"),text(install/"NapCatWinBootHook.dll")};options.workingDirectory=install;external.start(options);
   httplib::Client client("127.0.0.1",port);await([&]{auto r=client.Get("/fixture/health");return r&&r->status==200;},"external fixture did not start");
   DesktopIntegration manager(config,registry);auto manual=settings;manual["napcat"]["autoStart"]=false;manager.configure(manual,"");
   // Start before the first cached status: an already-running service must be
   // detected by its authenticated endpoint instead of spawning a second copy.
   manager.action({{"action","start"}});
   await([&]{return manager.status().value("running",false);},"external service was not observed");
   check(!manager.status().value("managed",true),"existing external instance is observed without adopting ownership");
   check(!manager.action({{"action","stop"}})["ok"].get<bool>(),"external instance cannot be killed by the owned-instance toggle");
   manager.stop();check(external.snapshot().active,"closing manager leaves an externally owned instance running");external.stop();
  }
  RegDeleteTreeW(HKEY_CURRENT_USER,registry.c_str());
  {
   auto provider=std::make_shared<InstallerFixture>();DesktopIntegration manager(config,registry,provider);
   check(manager.status()["installation"]["available"],"engine adapter can be supplied independently of launcher UI");
   auto accepted=manager.action({{"action","component_install"},{"component_id","napcat"},{"version","1.0"},{"operation","install"}});
   check(accepted["accepted"]&&!accepted["completed"].get<bool>()&&provider->submitted==1,"install boundary returns a job receipt rather than a fabricated completion");
   check(!manager.action({{"action","component_install"},{"component_id","../napcat"}})["ok"].get<bool>(),"component IDs cannot be paths");
   check(!manager.action({{"action","component_install"},{"component_id","napcat"},{"url","https://example.invalid"}})["ok"].get<bool>()&&provider->submitted==1,"raw sources are rejected before provider submission");
   manager.stop();
  }
  write(root/"acceptance.json",Json{{"passed",true},{"checks",checks},{"real_accounts_used",false},{"real_autorun_modified",false},{"paid_model_calls",0}}.dump(2));
  std::cout<<"Desktop integration passed "<<checks<<" checks. Evidence: "<<text(root)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
