#include <Hibernation/Vault.h>
#include <Hibernation/Application.h>
#include <iostream>
using namespace hibernation;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;const std::string mode=argv[1];const auto root=std::filesystem::u8path(argv[2]);
        if((mode!="inspect"&&mode!="mark-pending")||!std::filesystem::is_regular_file(root/".synthetic-world-journal-test"))return 2;
        ayt::storage::RecordStore store;if(!store.open(root/"data/archive/records.sqlite",{openVaultCodec(root/"data"),false}))return 3;
        ayt::storage::RecordQuery query;query.scope="ha.plugin-events";query.limit=512;auto events=store.query({{query.scope}},query);if(!events)return 4;
        int completed=0,pending=0,experiences=0,objects=-1;std::string source;
        for(const auto& row:events.records){const auto value=Json::parse(row.payload);if(value.value("kind",std::string{})!="habitat.consumed")continue;++completed;source=row.id;if(!value.value("journaled",true))++pending;
            if(mode=="mark-pending"){auto injected=value;injected["journaled"]=false;if(!store.commit({{row.scope,row.id,row.revision,row.schemaVersion,injected.dump(),"fault-fixture"}},"fault-fixture",value.at("created_at")))return 5;pending=1;}
        }
        query.scope="ha.archive";auto archive=store.query({{query.scope}},query);if(!archive)return 6;for(const auto& row:archive.records){const auto value=Json::parse(row.payload);if(value.value("kind",std::string{})=="outer_event"&&value.value("plugin_event_id",std::string{})==source)++experiences;}
        query.scope="ha.plugins";query.id="states";auto states=store.query({{query.scope}},query);if(!states)return 7;if(!states.records.empty()){const auto value=Json::parse(states.records[0].payload);if(value.contains("habitat"))objects=static_cast<int>(value.at("habitat").at("objects").size());}
        std::cout<<Json{{"completed",completed},{"pending",pending},{"experiences",experiences},{"checkpoint_objects",objects},{"source_id",source}}.dump()<<'\n';return 0;
    }catch(...){std::cerr<<"synthetic_world_archive_check_failed\n";return 1;}
}
