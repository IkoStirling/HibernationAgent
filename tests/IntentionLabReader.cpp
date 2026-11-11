#include <Hibernation/Vault.h>
#include <Hibernation/ActivityPlan.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
// Read only synthetic lab records. This is a test target, never packaged.
int main(int argc,char** argv){try{
    if(argc!=2)return 2;const auto root=std::filesystem::weakly_canonical(std::filesystem::u8path(argv[1]));
    std::ifstream marker(root/".synthetic-intention-life");std::string purpose;std::getline(marker,purpose);if(purpose!="hibernation.intention-life.v1")return 3;
    const auto data=root/"data";if(!std::filesystem::is_regular_file(data/"runtime/memory-key.dpapi")||!std::filesystem::is_regular_file(data/"archive/records.sqlite"))return 4;
    ayt::storage::RecordStore store;if(!store.open(data/"archive/records.sqlite",{hibernation::openVaultCodec(data),false}))return 5;
    const auto inlineSchema=hibernation::compactActivitySchema(hibernation::activitySchema());nlohmann::json result={{"schema_measurement",{{"inline_bytes",inlineSchema.size()},{"prompt_bytes",hibernation::activityPromptSchema(inlineSchema).size()}}}};
    for(const auto* scope:{"ha.activity-generations","ha.personal-effects","ha.personal"}){
        ayt::storage::RecordQuery query;query.scope=scope;query.limit=64;query.maxPayloadBytes=2*1024*1024;
        const auto rows=store.query({{scope}},query);if(!rows)return 6;result[scope]=nlohmann::json::array();
        for(const auto& row:rows.records)result[scope].push_back({{"id",row.id},{"revision",row.revision},{"value",nlohmann::json::parse(row.payload)}});
    }
    std::cout<<result.dump();return 0;
}catch(...){std::cerr<<"synthetic_intention_read_failed";return 1;}}
