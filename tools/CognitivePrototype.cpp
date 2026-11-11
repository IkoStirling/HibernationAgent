#include <Hibernation/CognitiveLoop.h>
#include <Hibernation/CognitiveModel.h>
#include <Hibernation/Vault.h>
#include <AYStorage/RecordStore.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using hibernation::cognitive::Json;
namespace {
constexpr const char* marker="ha.cognitive-prototype.synthetic.v1";
void require(bool ok,const char* issue){if(!ok)throw std::runtime_error(issue);}
std::string read(const fs::path& path,std::size_t maxBytes){
    require(fs::file_size(path)<=maxBytes,"file_too_large");std::ifstream input(path,std::ios::binary);
    require(static_cast<bool>(input),"file_read_failed");return {std::istreambuf_iterator<char>(input),{}};
}
void plainPath(const fs::path& input){
    for(auto path=fs::absolute(input);!path.empty();){
        const auto attributes=GetFileAttributesW(path.c_str());
        require(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"linked_path_rejected");
        const auto parent=path.parent_path();if(parent==path)break;path=parent;
    }
}
struct Lock {
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit Lock(const fs::path& path){handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);require(handle!=INVALID_HANDLE_VALUE,"prototype_already_running");}
    ~Lock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
Json defaultScript(){return Json::array({
    Json{{"event_id","seed"},{"type","seed"},{"energy",.8},{"hunger",.8},{"x",.1}},
    Json{{"event_id","apple-seen"},{"type","observe"},{"object",{{"id","apple"},{"revision",1},{"x",.8},{"salience",.4},{"ttl_ms",60000}}}},
    Json{{"event_id","walk-a-little"},{"type","advance"},{"ms",1000},{"executor","automatic"}},
    Json{{"event_id","hello"},{"type","message"},{"text","先停一下，等会儿再吃。"}},
    Json{{"event_id","conversation-wait"},{"type","advance"},{"ms",2000},{"executor","automatic"}},
    Json{{"event_id","finished-talking"},{"type","conversation_done"}},
    Json{{"event_id","finish-action"},{"type","advance"},{"ms",8000},{"executor","automatic"}},
    Json{{"event_id","quiet"},{"type","advance"},{"ms",1000},{"executor","automatic"}}
});}
}

int wmain(int argc,wchar_t** argv){
    try{
        fs::path root,script,connection;bool attention=true,statusOnly=false,manual=false,allowLive=false;int primeCount=0;
        for(int i=1;i<argc;++i){const std::wstring argument=argv[i];
            if(argument==L"--root"&&i+1<argc)root=argv[++i];
            else if(argument==L"--script"&&i+1<argc)script=argv[++i];
            else if(argument==L"--attention-off")attention=false;
            else if(argument==L"--status")statusOnly=true;
            else if(argument==L"--manual")manual=true;
            else if(argument==L"--allow-live")allowLive=true;
            else if(argument==L"--connection"&&i+1<argc)connection=argv[++i];
            else if(argument==L"--prime-count"&&i+1<argc){primeCount=std::stoi(argv[++i]);require(primeCount>=0&&primeCount<=16,"invalid_prime_count");}
            else if(argument==L"--help"){std::cout<<"Hibernation_CognitivePrototype --root NEW_SYNTHETIC_DIRECTORY [--script events.json] [--attention-off] [--status] [--manual] [--connection metered.json --allow-live]\nDefault mock only. Live mode requires explicit metered connection; no desktop or production memory access.\n";return 0;}
            else throw std::runtime_error("invalid_arguments");
        }
        require(connection.empty()||allowLive,"live_opt_in_required");
        std::unique_ptr<hibernation::cognitive::ModelSession> session;
        if(!connection.empty()){session=std::make_unique<hibernation::cognitive::ModelSession>(Json::parse(read(connection,16384)));manual=true;}
        require(!root.empty(),"explicit_synthetic_root_required");root=fs::absolute(root).lexically_normal();plainPath(root);
        const auto markerPath=root/".cognitive-prototype";plainPath(markerPath);plainPath(root/"prototype.lock");
        plainPath(root/"archive/records.sqlite");plainPath(root/"runtime/memory-key.dpapi");
        // An existing arbitrary directory is never claimed, even when empty.
        if(fs::exists(root))require(fs::is_regular_file(markerPath)&&read(markerPath,128)==marker,"not_a_prototype_root");
        else{require(!statusOnly,"missing_prototype_root");fs::create_directories(root);std::ofstream output(markerPath,std::ios::binary);output<<marker;require(static_cast<bool>(output),"marker_write_failed");}
        Lock lock(root/"prototype.lock");
        const auto database=root/"archive/records.sqlite";
        if(fs::exists(database))require(fs::exists(root/"runtime/memory-key.dpapi"),"prototype_key_missing");
        fs::create_directories(database.parent_path());
        ayt::storage::RecordStore store;require(store.open(database,{hibernation::openVaultCodec(root),false}),"prototype_store_open_failed");
        const auto old=store.query({{"ha.experiment.cognitive.state"}},{"ha.experiment.cognitive.state","current"});
        require(static_cast<bool>(old),"prototype_state_read_failed");
        hibernation::cognitive::Loop loop({attention,2000,.15,!manual});std::int64_t stateRevision=0;
        if(!old.records.empty()){
            stateRevision=old.records.front().revision;loop.restore(Json::parse(old.records.front().payload),!statusOnly);
            require(loop.snapshot().at("settings").at("attention")==attention,"existing_attention_setting_differs");
            if(!statusOnly)require(loop.snapshot().at("settings").at("automatic_planning")==!manual,"existing_planning_mode_differs");
        }
        if(statusOnly){std::cout<<Json{{"ok",true},{"synthetic",true},{"state_revision",stateRevision},{"view",loop.view()}}.dump()<<'\n';return 0;}
        auto persist=[&](const std::string& id,const Json& input,const Json& result){
            const auto snapshot=loop.snapshot();const auto now=snapshot.at("now").get<std::int64_t>();
            const auto committed=store.commit({{"ha.experiment.cognitive.state","current",stateRevision,1,snapshot.dump(),"synthetic"},
                {"ha.experiment.cognitive.events",id,0,1,Json{{"input",input},{"result",result}}.dump(),"synthetic"}},id,now);
            require(static_cast<bool>(committed),"prototype_commit_failed");++stateRevision;
        };
        // Persist recovery before reading commands, so a later process cannot replay uncertain work.
        if(stateRevision)persist("system-restart-"+std::to_string(stateRevision),{{"type","restart"}},{{"view",loop.view()}});
        auto events=script.empty()?defaultScript():Json::parse(read(script,1024*1024));
        require(events.is_array()&&events.size()<=512&&static_cast<std::size_t>(primeCount)<=events.size(),"invalid_script");
        require(primeCount==0||manual,"priming_requires_manual_mode");
        bool allOk=true;int eventIndex=0;
        for(const auto& event:events){
            const auto before=loop.snapshot();auto result=loop.dispatch(event);
            if(result.at("ok")==true&&result.value("duplicate",false)==false){
                try{persist(event.at("event_id"),event,result);}catch(...){loop.restore(before,false);throw;}
            }
            const auto eventId=event.is_object()&&event.contains("event_id")&&event.at("event_id").is_string()?event.at("event_id"):Json("");
            std::cout<<Json{{"event_id",eventId},{"result",result}}.dump()<<'\n';
            allOk=allOk&&result.at("ok").get<bool>();
            ++eventIndex;
            if(session&&eventIndex>primeCount&&result.at("ok")==true&&!result.value("duplicate",false)&&loop.needsDecision()){
                const auto decision=session->decide(loop);
                Json command={{"event_id","choice-"+std::to_string(stateRevision)},{"type",decision.approved?"decision":"decision_deferred"}};
                if(decision.approved)command["proposal"]=decision.proposal;else command["reason"]=decision.issue;
                const auto beforeDecision=loop.snapshot();auto applied=loop.dispatch(command);
                applied["model_audit"]=decision.audit;applied["candidate"]=decision.proposal;
                if(applied.at("ok")==true){try{persist(command.at("event_id"),command,applied);}catch(...){loop.restore(beforeDecision,false);throw;}}
                std::cout<<Json{{"event_id",command.at("event_id")},{"result",applied}}.dump()<<'\n';
                allOk=allOk&&applied.at("ok").get<bool>();
                if(!decision.approved&&decision.issue!="review_rejected"){allOk=false;break;}
            }
        }
        std::cout<<Json{{"summary",true},{"ok",allOk},{"synthetic",true},{"state_revision",stateRevision},{"view",loop.view()}}.dump()<<'\n';
        return allOk?0:2;
    }catch(const std::exception& e){std::cerr<<Json{{"ok",false},{"issue",e.what()}}.dump()<<'\n';return 1;}
}
