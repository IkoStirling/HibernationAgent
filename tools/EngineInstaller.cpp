#include <AYApplication/Installer.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using J=nlohmann::json;
int main(int argc,char** argv){
 try{
  if(argc!=3)return 2;std::ifstream f(std::filesystem::u8path(argv[2]));J c;f>>c;
  ayt::app::installer::Policy p;p.trustedManifestSha256=c.at("manifestSha256");p.allowedOrigins=c.at("allowedOrigins").get<std::vector<std::string>>();p.allowLoopbackHttp=c.value("allowLoopbackHttp",false);p.maxPackageBytes=c.value("maxPackageBytes",512ull*1024*1024);
  auto root=std::filesystem::absolute(std::filesystem::u8path(c.at("programRoot").get<std::string>()));std::filesystem::create_directories(root);ayt::app::installer::Service s(root);ayt::app::installer::Result r;std::string action=argv[1];
  if(action=="install"){auto m=s.fetchManifest({c.at("origin"),c.at("path")},p);r=s.install(m,p,[](const auto& progress){std::cerr<<J{{"task",progress.task},{"phase",progress.phase},{"completed",progress.completed},{"total",progress.total}}.dump()<<'\n';});}
  else if(action=="inspect")r=s.inspect(p);else if(action=="run")r=s.run(p);else return 2;
  auto u=r.executable.u8string();std::cout<<J{{"ok",r.ok},{"committed",r.committed},{"task",r.task},{"error",r.error},{"product",r.product},{"version",r.version},{"manifestSha256",r.manifestSha256},{"executable",std::string(reinterpret_cast<const char*>(u.data()),u.size())}}.dump()<<'\n';return r.ok?0:3;
 }catch(...){std::cout<<"{\"ok\":false,\"error\":\"installation_request_failed\"}\n";return 3;}
}
