#include <Hibernation/Vault.h>
#include <Hibernation/BackgroundBudget.h>
#include <Hibernation/PlanningCadence.h>
#include <AYApplication/WorkerHost.h>
#include <fstream>
#include <iostream>
using Json=nlohmann::json;
int main(int argc,char** argv){try{
    if(argc!=3)return 2;const auto root=std::filesystem::weakly_canonical(std::filesystem::u8path(argv[1]));const std::string mode=argv[2];
    if(mode!="hourly"&&mode!="review-budget"&&mode!="wake"&&mode!="balance"&&mode!="failed-timer"&&mode!="cache-budget"&&mode!="pursuit-kind"&&mode!="inspect-pursuit-kind")return 2;
    std::ifstream marker(root/".activity-recovery-fixture");std::string purpose;std::getline(marker,purpose);if(purpose!="hibernation.activity-recovery.v1")return 3;
    const auto data=root/"data";
    if(mode=="inspect-pursuit-kind"){
        if(!std::filesystem::is_regular_file(data/"archive/records.sqlite")||!std::filesystem::is_regular_file(data/"runtime/memory-key.dpapi"))return 4;
        ayt::storage::RecordStore ledger;if(!ledger.open(data/"archive/records.sqlite",{hibernation::openVaultCodec(data),false}))return 5;
        const auto rows=ledger.query({{"ha.archive"}},{"ha.archive","downgrade-keep"});if(!rows||rows.records.size()!=1)return 14;
        const auto& row=rows.records.front();const auto value=Json::parse(row.payload);
        if(row.revision!=1||value.value("text",std::string{})!="SYNTHETIC_NOTE_KEEP")return 15;
        std::cout<<Json{{"synthetic",true},{"original_record_preserved",true},{"revision",row.revision}}.dump()<<'\n';return 0;
    }
    if(std::filesystem::exists(data/"archive/records.sqlite")||std::filesystem::exists(data/"runtime/memory-key.dpapi"))return 4;
    std::filesystem::create_directories(data/"archive");std::filesystem::create_directories(data/"runtime");
    auto codec=hibernation::openVaultCodec(data);ayt::storage::RecordStore ledger;if(!ledger.open(data/"archive/records.sqlite",{codec,false}))return 5;
    const auto now=ayt::app::services::serviceNowMs();const auto reset=now+45000;
    if(mode=="cache-budget"){
        Json before={{"version",1},{"connections",{{"actor",{{"purposes",{{"activity",{{"calls",10},{"input_tokens",800},{"cached_input_tokens",600},{"uncached_input_tokens",200}}}}}}}}}};
        auto after=before;after["connections"]["actor"]["purposes"]["activity"]={{"calls",22},{"input_tokens",2000},{"cached_input_tokens",1700},{"uncached_input_tokens",300}};
        Json old={{"version",1},{"day_index",hibernation::background::dayIndex(now)},{"calls",0},{"input_tokens",0},{"reported_calls",0},{"last_seen_ms",now-2000}};auto used=old;used["calls"]=14;used["input_tokens"]=1200;used["reported_calls"]=12;used["last_seen_ms"]=now;
        if(!ledger.commit({{"ha.model-cache","usage",0,1,before.dump(),"cache-budget-fixture"}},"cache-budget-fixture",now-3000))return 10;
        if(!ledger.commit({{"ha.budget","background",0,1,old.dump(),"cache-budget-fixture"}},"cache-budget-fixture",now-2000))return 11;
        if(!ledger.commit({{"ha.model-cache","usage",1,1,after.dump(),"cache-budget-fixture"}},"cache-budget-fixture",now-1000))return 12;
        if(!ledger.commit({{"ha.budget","background",1,1,used.dump(),"cache-budget-fixture"}},"cache-budget-fixture",now))return 13;
        std::cout<<Json{{"synthetic",true},{"mode",mode},{"seeded_ms",now}}.dump()<<'\n';return 0;
    }
    Json hourly={{"started_ms",mode=="hourly"?reset-3600000:now},{"count",mode=="hourly"?800:0}};
    auto cadence=hibernation::emptyPlanningCadence();if(mode=="wake"){cadence["quiet"]=4;cadence["last_ms"]=now;cadence["next_ms"]=now+120000;}
    const std::vector<ayt::storage::RecordMutation> changes={
        {"ha.settings","model",0,1,Json{{"id","actor"}}.dump(),"synthetic-recovery-seed"},
        {"ha.budget","models",0,1,hourly.dump(),"synthetic-recovery-seed"},
        {"ha.budget","background",0,1,hibernation::background::fresh(now).dump(),"synthetic-recovery-seed"},
        {"ha.autonomy","cadence",0,1,cadence.dump(),"synthetic-recovery-seed"}};
    if(!ledger.commit(changes,"synthetic-recovery-seed",now))return 6;
    if(mode=="pursuit-kind"){
        Json control={{"paused",true},{"offline",false},{"manual_offline",false},{"balance_offline",false},{"balance_blocks",Json::object()},{"pause_owned",Json::array()}};
        Json note={{"kind","user_message"},{"text","SYNTHETIC_NOTE_KEEP"},{"created_at",now}};
        if(!ledger.commit({{"ha.control","current",0,1,control.dump(),"synthetic-kind-fixture"},{"ha.archive","downgrade-keep",0,1,note.dump(),"synthetic-kind-fixture"}},"synthetic-kind-fixture",now))return 16;
        using namespace ayt::app::services;QueueOptions options;options.name="hibernation";DurableQueue queue(ledger,options);
        JobSpec task;task.id="autonomy.personal-pursuit";task.kind="role.personal-pursuit";task.payload=Json{{"force",true},{"topic","SYNTHETIC_PLAN_MUST_NOT_RUN"}}.dump();task.dueAtMs=now-1000;task.maxAttempts=1;
        if(queue.enqueue(task,now)!=QueueStatus::Ok)return 17;
    }
    if(mode=="failed-timer"){
        using namespace ayt::app::services;QueueOptions options;options.name="hibernation";DurableQueue queue(ledger,options);
        JobSpec timer;timer.id="solitude.timer";timer.kind="role.solitude";timer.payload=Json{{"force",false}}.dump();timer.dueAtMs=now-1000;timer.intervalMs=60000;timer.maxAttempts=1;
        if(queue.enqueue(timer,now)!=QueueStatus::Ok)return 7;const auto claim=queue.claimNext(now);if(!claim.claim)return 8;
        if(queue.finish(*claim.claim,{false,false,Json{{"status","failed"},{"code","synthetic_old_failure"}}.dump(),{}},now)!=QueueStatus::Ok)return 9;
    }
    std::cout<<Json{{"synthetic",true},{"mode",mode},{"seeded_ms",now},{"hourly_resets_ms",reset},{"daily_calls",0},{"real_model_calls",0}}.dump()<<'\n';return 0;
}catch(...){std::cerr<<"activity_recovery_seed_failed\n";return 1;}}
