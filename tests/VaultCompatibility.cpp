#include <Hibernation/Vault.h>
#include <Hibernation/Application.h>
#include <AYCrypto.h>
#include <AYStorage/ISqlite.h>
#include <AYTestFixtures.h>
#include <array>
using namespace hibernation;
namespace {
std::string hex(std::string_view in){const char* digits="0123456789abcdef";std::string out;for(unsigned char c:in){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string unhex(std::string_view in){auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};std::string out;for(size_t i=0;i<in.size();i+=2)out+=static_cast<char>(nibble(in[i])*16+nibble(in[i+1]));return out;}
std::string binding(const ayt::storage::VersionedRecord& m){return Json::array({m.scope,m.id,m.revision,m.schemaVersion,m.source,m.correlation,m.occurredAtMs}).dump();}
// Exact HV1 writer used by the released application. Synthetic key/data only.
struct LegacyCodec final:ayt::storage::IRecordPayloadCodec {
    std::array<uint8_t,32> key{};
    std::string identity()const override{return "hibernation.chacha20poly1305.v1";}
    bool encode(const ayt::storage::VersionedRecord& m,std::string_view p,std::string& out)const override {
        auto cipher=ayt::crypto::createAeadCipher(ayt::crypto::AeadAlgo::ChaCha20_Poly1305);std::array<uint8_t,12> nonce{};if(!ayt::crypto::tryGenerateRandomBytes(nonce.data(),nonce.size())||!cipher->setKey(key.data(),key.size())||!cipher->setNonce(nonce.data(),nonce.size()))return false;
        const auto text=binding(m)+"\n"+std::string(p);std::string encrypted(text.size(),'\0');size_t size=encrypted.size();std::array<uint8_t,16> tag{};if(!cipher->encrypt(reinterpret_cast<const uint8_t*>(text.data()),text.size(),reinterpret_cast<uint8_t*>(encrypted.data()),&size,tag.data()))return false;
        out="HV1:"+hex(std::string(reinterpret_cast<const char*>(nonce.data()),nonce.size())+encrypted+std::string(reinterpret_cast<const char*>(tag.data()),tag.size()));return true;
    }
    bool decode(const ayt::storage::VersionedRecord& m,std::string_view p,std::string& out)const override {
        if(!p.starts_with("HV1:"))return false;const auto packet=unhex(p.substr(4));if(packet.size()<29)return false;auto cipher=ayt::crypto::createAeadCipher(ayt::crypto::AeadAlgo::ChaCha20_Poly1305);if(!cipher->setKey(key.data(),key.size())||!cipher->setNonce(reinterpret_cast<const uint8_t*>(packet.data()),12))return false;
        std::string plain(packet.size()-28,'\0');size_t size=plain.size();if(!cipher->decrypt(reinterpret_cast<const uint8_t*>(packet.data()+12),size,reinterpret_cast<const uint8_t*>(packet.data()+packet.size()-16),reinterpret_cast<uint8_t*>(plain.data()),&size))return false;
        const auto prefix=binding(m)+"\n";if(!plain.starts_with(prefix))return false;out=plain.substr(prefix.size());return true;
    }
};
void directories(const std::filesystem::path& root){std::filesystem::create_directories(root/"runtime");std::filesystem::create_directories(root/"archive");}
std::vector<std::string> storedPayloads(const std::filesystem::path& file){auto db=ayt::storage::ISqlite::create();if(!db->open(file.string()))throw std::runtime_error("fixture_db");auto q=db->prepare("SELECT payload FROM ay_record_versions ORDER BY scope,id,revision");std::vector<std::string> rows;while(q->stepChecked()==ayt::storage::SqliteStepResult::Row)rows.push_back(q->getText(0));return rows;}
}
TEST_SUITE(VaultCompatibility)
TEST_CASE(hv2_authenticates_all_nonce_bytes_and_metadata_and_clears_failed_output){
    ayt::test::ScratchDirectory scratch("vault-hv2");directories(scratch.path());auto codec=openVaultCodec(scratch.path());ayt::storage::VersionedRecord m{"ha.memory","memory-a",2,1,{},"source-a","turn-a",123};std::string packet,decoded;
    CHECK(codec->identity()=="hibernation.xchacha20poly1305.v2");CHECK(codec->encode(m,"UTF-8记忆\n保留换行",packet));CHECK(packet.starts_with("HV2:"));CHECK(codec->decode(m,packet,decoded));CHECK(decoded=="UTF-8记忆\n保留换行");
    for(size_t byte=0;byte<24;++byte){auto changed=packet;const auto index=4+byte*2;changed[index]=changed[index]=='a'?'b':'a';decoded="stale plaintext";CHECK_FALSE(codec->decode(m,changed,decoded));CHECK(decoded.empty());}
    for(int field=0;field<7;++field){auto changed=m;if(field==0)changed.scope="ha.other";if(field==1)changed.id="other";if(field==2)++changed.revision;if(field==3)++changed.schemaVersion;if(field==4)changed.source="other";if(field==5)changed.correlation="other";if(field==6)++changed.occurredAtMs;CHECK_FALSE(codec->decode(changed,packet,decoded));CHECK(decoded.empty());}
    auto changed=packet;changed[2]='1';CHECK_FALSE(codec->decode(m,changed,decoded));CHECK_FALSE(codec->decode(m,"HV2:invalid",decoded));CHECK_FALSE(codec->decode(m,packet.substr(0,packet.size()-1),decoded));CHECK(decoded.empty());
}
TEST_CASE(legacy_upgrade_keeps_all_original_envelopes_revisions_and_sources){
    ayt::test::ScratchDirectory scratch("vault-legacy-upgrade");directories(scratch.path());const auto file=scratch.path()/"archive/records.sqlite";auto legacy=std::make_shared<LegacyCodec>();for(size_t i=0;i<legacy->key.size();++i)legacy->key[i]=static_cast<uint8_t>(i+1);CHECK(saveUserSecret(scratch.path()/"runtime/memory-key.dpapi",std::string(reinterpret_cast<const char*>(legacy->key.data()),legacy->key.size())));
    ayt::storage::RecordStore old;CHECK(old.open(file,{legacy,false}));CHECK(static_cast<bool>(old.commit({{"ha.memory","a",0,1,"最初经历","source-1"}},"turn-1",100)));CHECK(static_cast<bool>(old.commit({{"ha.memory","a",1,2,"后来的理解","source-2"}},"turn-2",200)));const auto original=storedPayloads(file);CHECK(original.size()==2);
    auto codec=openVaultCodec(scratch.path());ayt::storage::RecordStore current;CHECK_FALSE(current.open(file,{codec,false}));CHECK(current.open(file,{codec,false,true}));CHECK(storedPayloads(file)==original);auto history=current.query({{"ha.memory"}},{"ha.memory","a","",true});CHECK(history.records.size()==2);CHECK(history.records[0].payload=="最初经历"&&history.records[1].payload=="后来的理解");CHECK(history.records[1].revision==2&&history.records[1].schemaVersion==2&&history.records[1].source=="source-2"&&history.records[1].correlation=="turn-2"&&history.records[1].occurredAtMs==200);
    CHECK(old.query({{"ha.memory"}},{"ha.memory"}).status==ayt::storage::RecordStatus::Unavailable);ayt::storage::RecordStore downgraded;CHECK_FALSE(downgraded.open(file,{legacy,false}));CHECK(static_cast<bool>(current.commit({{"ha.memory","a",2,3,"第三次经历","source-3"}},"turn-3",300)));auto packets=storedPayloads(file);CHECK(packets.size()==3);CHECK(packets[0]==original[0]&&packets[1]==original[1]&&packets[2].starts_with("HV2:"));
    ayt::storage::RecordStore reopened;CHECK(reopened.open(file,{openVaultCodec(scratch.path()),false}));CHECK(reopened.query({{"ha.memory"}},{"ha.memory","a","",true}).records.size()==3);
}
TEST_CASE(wrong_master_key_cannot_publish_compatible_upgrade){
    ayt::test::ScratchDirectory scratch("vault-wrong-key-upgrade");directories(scratch.path());const auto file=scratch.path()/"archive/records.sqlite";auto legacy=std::make_shared<LegacyCodec>();CHECK(saveUserSecret(scratch.path()/"runtime/memory-key.dpapi",std::string(32,'x')));ayt::storage::RecordStore old;CHECK(old.open(file,{legacy,false}));CHECK(static_cast<bool>(old.commit({{"ha.memory","a",0,1,"protected","source"}},"turn",1)));const auto original=storedPayloads(file);
    ayt::storage::RecordStore denied;CHECK_FALSE(denied.open(file,{openVaultCodec(scratch.path()),false,true}));CHECK(storedPayloads(file)==original);CHECK(old.query({{"ha.memory"}},{"ha.memory","a"}).records[0].payload=="protected");
}
TEST_SUITE_END
