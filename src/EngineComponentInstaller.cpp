#include <Hibernation/EngineComponentInstaller.h>
#include <AYApplication/Installer.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <set>
#include <algorithm>
namespace hibernation {
namespace {
using J=nlohmann::json;
struct InvalidCatalog:ComponentInstaller {J status()const override{return {{"available",false},{"provider","AYApplicationInstaller"},{"reason","component_catalog_invalid"}};}J submit(const ComponentInstallRequest&)override{return {{"ok",false},{"error","component_catalog_invalid"}};}};
struct Adapter:ComponentInstaller {
 J catalog,state=J::object();mutable std::mutex mutex;std::thread worker;std::atomic<bool> cancel=false;bool busy=false;uint64_t sequence=0;
 explicit Adapter(J value):catalog(std::move(value)){}
 ~Adapter(){cancel=true;if(worker.joinable())worker.join();}
 J status()const override{std::lock_guard lock(mutex);J entries=J::array();for(const auto& e:catalog.at("components"))entries.push_back({{"id",e.at("id")},{"name",e.value("name",e.at("id").get<std::string>())}});return {{"available",true},{"provider","AYApplicationInstaller"},{"busy",busy},{"components",entries},{"job",state},{"local_import_available",true}};}
 J submit(const ComponentInstallRequest& request) override{
  std::unique_lock lock(mutex);if(busy)return {{"ok",false},{"error","component_installation_busy"}};
  if(request.operation==ComponentInstallRequest::Operation::Remove)return {{"ok",false},{"error","component_remove_not_supported"}};
  J selected;for(const auto& e:catalog.at("components"))if(e.at("id")==request.componentId)selected=e;
  if(selected.is_null()||(!request.version.empty()&&selected.value("version",std::string{})!=request.version))return {{"ok",false},{"error","component_release_not_authorized"}};
  if(worker.joinable())worker.join();busy=true;cancel=false;auto id="component-"+std::to_string(++sequence);state={{"id",id},{"phase","starting"},{"completed",false}};
  worker=std::thread([this,selected,id]{
   J result;try{ayt::app::installer::Policy p;p.trustedManifestSha256=selected.at("manifestSha256");p.allowedOrigins=selected.at("allowedOrigins").get<std::vector<std::string>>();p.allowLoopbackHttp=selected.value("allowLoopbackHttp",false);p.maxPackageBytes=selected.value("maxPackageBytes",512ull*1024*1024);auto root=std::filesystem::u8path(selected.at("programRoot").get<std::string>());if(!root.is_absolute())throw std::runtime_error("root");std::filesystem::create_directories(root);ayt::app::installer::Service service(root);auto manifest=service.fetchManifest({selected.at("origin"),selected.at("path")},p);auto r=service.install(manifest,p,[this](const auto& progress){std::lock_guard guard(mutex);state["phase"]=progress.phase;state["bytes"]=progress.completed;state["total"]=progress.total;},[this]{return cancel.load();});result={{"ok",r.ok},{"committed",r.committed},{"error",r.error},{"product",r.product},{"version",r.version},{"manifestSha256",r.manifestSha256}};}catch(...){result={{"ok",false},{"committed",false},{"error","component_installation_failed"}};}
   std::lock_guard guard(mutex);state["result"]=result;state["phase"]=result.value("ok",false)?"installed":"failed";state["completed"]=true;busy=false;
  });return {{"ok",true},{"accepted",true},{"completed",false},{"job_id",id}};
 }
};
}
std::shared_ptr<ComponentInstaller> engineComponentInstaller(const std::filesystem::path& config){
 try{
 auto path=config.parent_path()/"components.json";if(!std::filesystem::exists(path))return std::make_shared<Adapter>(J{{"schema",1},{"components",J::array()}});
 std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()<0||f.tellg()>128*1024)throw std::runtime_error("component_catalog_invalid");f.seekg(0);J j;f>>j;if(j.at("schema")!=1||!j.at("components").is_array()||j.at("components").size()>16)throw std::runtime_error("component_catalog_invalid");
 std::ifstream configFile(config);J main;configFile>>main;auto data=std::filesystem::u8path(main.at("dataRoot").get<std::string>());data=std::filesystem::absolute(data.is_absolute()?data:config.parent_path()/data).lexically_normal();std::set<std::string> ids,roots;
 auto canonical=[](std::filesystem::path p){auto s=p.lexically_normal().generic_string()+"/";std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;};
 for(const auto& e:j.at("components")){for(const auto* k:{"id","programRoot","origin","path","manifestSha256"})if(!e.at(k).is_string())throw std::runtime_error("component_catalog_invalid");auto root=std::filesystem::u8path(e.at("programRoot").get<std::string>());if(!e.at("allowedOrigins").is_array()||!root.is_absolute()||!ids.insert(e.at("id")).second||!roots.insert(canonical(root)).second)throw std::runtime_error("component_catalog_invalid");for(const auto& protect:{data,std::filesystem::absolute(config.parent_path())})if(canonical(root).starts_with(canonical(protect))||canonical(protect).starts_with(canonical(root)))throw std::runtime_error("component_root_overlaps_user_data");}
 return std::make_shared<Adapter>(std::move(j));
 }catch(...){return std::make_shared<InvalidCatalog>();}
}
}
