#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <Windows.h>
using Json=nlohmann::json;
int main(int argc,char** argv){
 if(argc<2)return 2;const auto root=std::filesystem::path(argv[1]).parent_path();
 Json c;{std::ifstream f(root/"config"/"webui.json");f>>c;}
 httplib::Server server;server.new_task_queue=[] {return new httplib::ThreadPool(2);}; server.set_pre_routing_handler([&](const auto& r,auto&){std::ofstream f(root/"fixture-requests.txt",std::ios::app);f<<r.method<<" "<<r.path<<" bytes="<<r.body.size()<<"\n";return httplib::Server::HandlerResponse::Unhandled;});
 auto reply=[](httplib::Response& r,Json j){r.set_content(j.dump(),"application/json");};
 auto auth=[&](const httplib::Request& r){return r.get_header_value("Authorization")=="Bearer fixture-credential";};
 server.Post("/api/auth/login",[&](const auto& r,auto& out){auto body=Json::parse(r.body,nullptr,false);reply(out,body.is_object()&&body.value("hash",std::string{})==c.at("expectedHash").get<std::string>()?Json{{"code",0},{"data",{{"Credential","fixture-credential"}}}}:Json{{"code",-1},{"data",nullptr}});});
 int qrRevision=0;
 auto status=[&](const auto& r,auto& out){if(!auth(r)){out.status=401;reply(out,{{"code",-1}});return;}const bool online=std::filesystem::exists(root/"fixture-online");const auto url=std::filesystem::exists(root/"fixture-invalid-qr")?"https://example.invalid/not-a-qq-link":"https://ssl.ptlogin2.qq.com/fixture-qr-"+std::to_string(qrRevision);reply(out,{{"code",0},{"data",{{"isLogin",online},{"qrcodeurl",online?"":url}}}});};
 server.Post("/api/QQLogin/CheckLoginStatus",status);
 server.Post("/api/QQLogin/GetQQLoginQrcode",[&](const auto& r,auto& out){if(!auth(r)){out.status=401;return;}reply(out,{{"code",0},{"data",{{"qrcode",std::filesystem::exists(root/"fixture-invalid-qr")?"https://example.invalid/not-a-qq-link":"https://ssl.ptlogin2.qq.com/fixture-qr-"+std::to_string(qrRevision)}}}});});
 server.Post("/api/QQLogin/RefreshQRcode",[&](const auto& r,auto& out){if(!auth(r)){out.status=401;return;}++qrRevision;reply(out,{{"code",0},{"data",{{"qrcodeurl","https://ssl.ptlogin2.qq.com/fixture-qr-"+std::to_string(qrRevision)}}}});});
 server.Get("/fixture/health",[&](const auto&,auto& out){reply(out,{{"pid",GetCurrentProcessId()}});});
 if(!server.bind_to_port("127.0.0.1",c.at("port")))return 3;
 {std::ofstream f(root/"fixture.pid");f<<GetCurrentProcessId();}
 std::cout<<"SYNTHETIC_CHILD_OUTPUT_SECRET_MUST_NOT_ESCAPE\n"<<std::flush;
 return server.listen_after_bind()?0:4;
}
