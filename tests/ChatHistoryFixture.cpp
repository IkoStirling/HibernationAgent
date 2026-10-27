#include <Hibernation/Application.h>
#include <Hibernation/Vault.h>
#include <chrono>
#include <thread>
#include <iostream>
using namespace hibernation;
int main(int argc,char** argv){try{
    if(argc<3)return 2;const auto configFile=std::filesystem::u8path(argv[1]);const auto root=configFile.parent_path();if(!std::filesystem::exists(root/".synthetic-chat-history"))return 2;const auto cfg=loadConfig(configFile);
    std::filesystem::create_directories(cfg.dataRoot/"archive");
    {ayt::storage::RecordStore store;if(!store.open(cfg.dataRoot/"archive/records.sqlite",{openVaultCodec(cfg.dataRoot),false}))return 4;
        Json recent=Json::array();std::vector<ayt::storage::RecordMutation> changes;for(int i=0;i<200;++i){const auto key="history-"+std::to_string(1000+i);recent.push_back(key);changes.push_back({"ha.archive",key,0,1,Json{{"kind",i%2?"assistant_message":"user_message"},{"text","synthetic conversation "+std::to_string(i)},{"created_at",i+1000}}.dump(),"synthetic-history"});}
        changes.push_back({"ha.archive","private-tool",0,1,Json{{"kind","tool_observation"},{"text","HIDDEN_TOOL_TRAFFIC"}}.dump(),"synthetic-history"});recent.push_back("private-tool");auto index=store.query({{"ha.index"}},{"ha.index","recent"});if(!index)return 4;changes.push_back({"ha.index","recent",index.records.empty()?0:index.records.front().revision,1,recent.dump(),"synthetic-history"});if(!store.commit(changes,"synthetic-history",1000))return 4;
    }
    Application app(cfg);if(!app.initialize())return 3;Json ids=Json::array(),media=Json::array();for(int i=2;i<argc;++i){const auto result=app.importChatMedia(std::filesystem::u8path(argv[i]));if(result.status!=201){std::cerr<<result.body;return 5;}auto value=Json::parse(result.body);ids.push_back(value.at("id"));media.push_back(value);}
    if(app.request({"POST","/api/message",Json{{"request_id","history-attachment"},{"text","synthetic attachments"},{"attachments",ids}}.dump()}).status!=202)return 6;
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(std::chrono::steady_clock::now()<end){app.tick();if(!Json::parse(app.request({"GET","/api/chat/status",""}).body).at("busy").get<bool>())break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    const auto page=app.request({"POST","/api/chat/page","{}"});if(page.status!=200)return 7;const auto value=Json::parse(page.body);if(value.at("total")!=202||value.at("messages").size()!=24)return 8;app.stop();std::cout<<Json{{"seeded",true},{"total",202},{"media",media}}.dump();return 0;
}catch(...){std::cerr<<"synthetic_chat_history_failed";return 1;}}
