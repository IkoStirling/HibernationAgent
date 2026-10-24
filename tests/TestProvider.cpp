#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
int main(int argc,char** argv){
    if(argc!=2)return 2;const auto root=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(root);httplib::Server server;server.new_task_queue=[]{return new httplib::ThreadPool(1);};server.set_payload_max_length(1024*1024);std::atomic<int> calls=0;
    server.Post("/v1/chat/completions",[&](const auto& request,auto& response){
        ++calls;{std::ofstream capture(root/"last-request.json",std::ios::binary);capture<<request.body;}
        std::ifstream input(root/"response.json",std::ios::binary);const std::string output((std::istreambuf_iterator<char>(input)),{});
        auto envelope=nlohmann::json{{"id","fixture-request"},{"model","fixture-model"},{"choices",nlohmann::json::array({{{"message",{{"role","assistant"},{"content",output}}},{"finish_reason","stop"}}})}};
        response.set_content(envelope.dump(),"application/json");
    });
    const auto port=server.bind_to_any_port("127.0.0.1");if(port<=0)return 3;std::ofstream(root/"port")<<port;
    std::jthread host([&]{server.listen_after_bind();});std::this_thread::sleep_for(std::chrono::seconds(110));server.stop();host.join();return calls?0:4;
}
