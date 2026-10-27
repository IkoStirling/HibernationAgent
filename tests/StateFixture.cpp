#include <Hibernation/Application.h>
#include <Hibernation/RoleState.h>
#include <Hibernation/Vault.h>
#include <fstream>
#include <iostream>
using namespace hibernation;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;const auto root=std::filesystem::weakly_canonical(std::filesystem::u8path(argv[1]));const std::string mode=argv[2];
        if(mode!="low-energy"&&mode!="withdrawn"&&mode!="engaged")return 2;
        std::ifstream marker(root/".synthetic-ai-evaluation");std::string purpose;std::getline(marker,purpose);if(purpose!="hibernation.dialogue-evaluation.v1")return 2;
        const auto data=root/"data";if(std::filesystem::exists(data/"archive/records.sqlite")||std::filesystem::exists(data/"runtime/memory-key.dpapi"))return 3;
        const auto now=ayt::app::services::serviceNowMs();Json value={{"version",2},{"updated_ms",now},{"entered_ms",now},{"evidence",""},{"focus",""}};
        if(mode=="low-energy")value.update({{"energy",.16},{"openness",.45},{"mood",.45},{"willingness",.18},{"social_load",.1},{"conversation_mode","quiet"},{"activity","idle"},{"desired_activity","idle"},{"weather_mood",.45}});
        if(mode=="withdrawn")value.update({{"energy",.58},{"openness",.4},{"mood",.38},{"willingness",.16},{"social_load",.7},{"conversation_mode","quiet"},{"activity","withdrawing"},{"desired_activity","withdrawing"},{"weather_mood",.38}});
        if(mode=="engaged")value.update({{"energy",.84},{"openness",.64},{"mood",.6},{"willingness",.76},{"social_load",0.0},{"conversation_mode","balanced"},{"activity","engaging"},{"desired_activity","engaging"},{"weather_mood",.6}});
        RoleState life;life.restore(value,now);const auto snapshot=life.snapshot();
        std::filesystem::create_directories(data/"archive");ayt::storage::RecordStore store;
        if(!store.open(data/"archive/records.sqlite",{openVaultCodec(data),false}))return 4;
        if(!store.commit({{"ha.life","current",0,1,snapshot.dump(),"synthetic-state-seed"}},"synthetic-state-seed",now))return 5;
        std::cout<<Json{{"synthetic",true},{"mode",mode},{"continuous_state",snapshot},{"experience_records_created",0}}.dump()<<'\n';return 0;
    }catch(...){std::cerr<<"synthetic_state_seed_failed\n";return 1;}
}
