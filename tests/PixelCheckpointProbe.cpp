#include <Hibernation/Vault.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
// Test-only checkpoint observation. It refuses ordinary/formal archives unless
// the private scene fixture has explicitly marked its own existing data root.
// No decrypted record, credential or filesystem path is printed.
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const std::string expected=argv[2];if(expected!="true"&&expected!="false")return 2;
    try{
        std::error_code error;const auto root=std::filesystem::canonical(std::filesystem::u8path(argv[1]),error);if(error||!std::filesystem::is_directory(root))return 2;
        const auto marker=root/"pixel-scene-fixture";if(!std::filesystem::is_regular_file(marker)||std::filesystem::file_size(marker)>128)return 2;
        std::ifstream input(marker,std::ios::binary);std::string purpose;std::getline(input,purpose);if(!purpose.empty()&&purpose.back()=='\r')purpose.pop_back();if(purpose!="hibernation.pixel-scene.synthetic.v1")return 2;
        const auto database=root/"archive/records.sqlite",key=root/"runtime/memory-key.dpapi";
        if(!std::filesystem::is_regular_file(database)||!std::filesystem::is_regular_file(key))return 2;
        ayt::storage::RecordStore store;if(!store.open(database,{hibernation::openVaultCodec(root),false,false}))return 1;
        ayt::storage::RecordQuery query;query.scope="ha.plugins";query.id="states";query.limit=1;query.maxPayloadBytes=2*1024*1024;
        const auto rows=store.query({{"ha.plugins"},{"states"}},query);if(!rows||rows.records.size()!=1)return 1;
        const auto saved=nlohmann::json::parse(rows.records[0].payload,nullptr,false);if(!saved.is_object()||!saved.contains("pixel-canvas")||!saved.at("pixel-canvas").is_object())return 1;
        const auto& pixel=saved.at("pixel-canvas");if(!pixel.contains("surface")||!pixel.at("surface").is_object())return 1;
        const auto& surface=pixel.at("surface");if(!surface.contains("visible")||!surface.at("visible").is_boolean())return 1;
        return surface.at("visible").get<bool>()==(expected=="true")?0:1;
    }catch(...){return 1;}
}
