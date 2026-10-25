#include <Hibernation/Vault.h>
#include <AYCrypto.h>
#include <AYIO/File.h>
#include <AYStorage/ISqlite.h>
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <stdexcept>
#include <Windows.h>
#include <wincrypt.h>
namespace hibernation {
namespace {
using Json=nlohmann::json;
std::string pathUtf8(const std::filesystem::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string read(const std::filesystem::path& p,std::size_t max=65536){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f||f.tellg()<0||static_cast<std::size_t>(f.tellg())>max)throw std::runtime_error("vault_file");std::string s(static_cast<std::size_t>(f.tellg()),'\0');f.seekg(0);if(!s.empty()&&!f.read(s.data(),s.size()))throw std::runtime_error("vault_read");return s;}
std::string hex(std::string_view in){constexpr char chars[]="0123456789abcdef";std::string out;out.reserve(in.size()*2);for(unsigned char c:in){out.push_back(chars[c>>4]);out.push_back(chars[c&15]);}return out;}
bool unhex(std::string_view in,std::string& out){if(in.size()%2)return false;out.clear();out.reserve(in.size()/2);auto n=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};for(size_t i=0;i<in.size();i+=2){auto a=n(in[i]),b=n(in[i+1]);if(a<0||b<0)return false;out.push_back(static_cast<char>((a<<4)|b));}return true;}
Json binding(const ayt::storage::VersionedRecord& m){return Json::array({m.scope,m.id,m.revision,m.schemaVersion,m.source,m.correlation,m.occurredAtMs});}
struct SensitiveText {std::string value;~SensitiveText(){ayt::crypto::secureZero(value.data(),value.size());}};
class VaultCodec final:public ayt::storage::IRecordPayloadCodec {
    std::array<uint8_t,32> legacyKey_{},key_{};
public:
    explicit VaultCodec(std::string_view key){
        if(key.size()!=legacyKey_.size())throw std::runtime_error("vault_key_size");
        auto hmac=ayt::crypto::createHMAC(ayt::crypto::HashAlgo::SHA256);if(!hmac)throw std::runtime_error("vault_derivation");
        constexpr std::string_view label="HibernationAgent/vault/HV2/XChaCha20-Poly1305";
        hmac->compute(reinterpret_cast<const uint8_t*>(key.data()),key.size(),reinterpret_cast<const uint8_t*>(label.data()),label.size(),key_.data());
        std::copy(key.begin(),key.end(),legacyKey_.begin());
    }
    ~VaultCodec(){ayt::crypto::secureZero(legacyKey_.data(),legacyKey_.size());ayt::crypto::secureZero(key_.data(),key_.size());}
    std::string identity()const override{return "hibernation.xchacha20poly1305.v2";}
    bool canReadIdentity(std::string_view stored)const override{return stored==identity()||stored=="hibernation.chacha20poly1305.v1";}
    bool encode(const ayt::storage::VersionedRecord& m,std::string_view p,std::string& out)const override {
        out.clear();auto cipher=ayt::crypto::createAeadCipher(ayt::crypto::AeadAlgo::XChaCha20_Poly1305);if(!cipher)return false;
        std::array<uint8_t,24> nonce{};if(!ayt::crypto::tryGenerateRandomBytes(nonce.data(),nonce.size()))return false;
        if(!cipher->setKey(key_.data(),key_.size())||!cipher->setNonce(nonce.data(),nonce.size()))return false;
        SensitiveText input;input.value=binding(m).dump();input.value+='\n';input.value.append(p);std::string encrypted(input.value.size(),'\0');size_t size=encrypted.size();std::array<uint8_t,16> tag{};
        if(!cipher->encrypt(reinterpret_cast<const uint8_t*>(input.value.data()),input.value.size(),reinterpret_cast<uint8_t*>(encrypted.data()),&size,tag.data()))return false;
        std::string packet(reinterpret_cast<const char*>(nonce.data()),nonce.size());packet.append(encrypted.data(),size);packet.append(reinterpret_cast<const char*>(tag.data()),tag.size());
        out="HV2:"+hex(packet);return true;
    }
    bool decode(const ayt::storage::VersionedRecord& m,std::string_view encoded,std::string& out)const override {
        ayt::crypto::secureZero(out.data(),out.size());out.clear();const bool legacy=encoded.starts_with("HV1:");if(!legacy&&!encoded.starts_with("HV2:"))return false;
        const size_t nonceBytes=legacy?12:24;encoded.remove_prefix(4);std::string packet;if(!unhex(encoded,packet)||packet.size()<=nonceBytes+16)return false;
        auto cipher=ayt::crypto::createAeadCipher(legacy?ayt::crypto::AeadAlgo::ChaCha20_Poly1305:ayt::crypto::AeadAlgo::XChaCha20_Poly1305);const auto& key=legacy?legacyKey_:key_;if(!cipher||!cipher->setKey(key.data(),key.size())||!cipher->setNonce(reinterpret_cast<const uint8_t*>(packet.data()),nonceBytes))return false;
        const auto bytes=packet.size()-nonceBytes-16;SensitiveText decoded;decoded.value.resize(bytes);size_t size=decoded.value.size();
        if(!cipher->decrypt(reinterpret_cast<const uint8_t*>(packet.data()+nonceBytes),bytes,reinterpret_cast<const uint8_t*>(packet.data()+nonceBytes+bytes),reinterpret_cast<uint8_t*>(decoded.value.data()),&size))return false;
        try {const auto delimiter=decoded.value.find('\n');if(delimiter==std::string::npos||Json::parse(decoded.value.substr(0,delimiter))!=binding(m))return false;out=decoded.value.substr(delimiter+1);return true;}catch(...){return false;}
    }
};
}
bool saveUserSecret(const std::filesystem::path& path,const std::string& value){
    DATA_BLOB input{static_cast<DWORD>(value.size()),reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))},output{};
    if(!CryptProtectData(&input,L"Hibernation local secret",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))return false;
    const bool ok=ayt::io::File::atomicWriteUtf8(pathUtf8(path),output.pbData,output.cbData);LocalFree(output.pbData);return ok;
}
std::string loadUserSecret(const std::filesystem::path& path){auto bytes=read(path);DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())},output{};
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))throw std::runtime_error("vault_key_locked");
    std::string value(reinterpret_cast<const char*>(output.pbData),output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);return value;
}
std::shared_ptr<const ayt::storage::IRecordPayloadCodec> openVaultCodec(const std::filesystem::path& dataRoot){
    const auto path=dataRoot/"runtime"/"memory-key.dpapi";SensitiveText secret;auto& key=secret.value;
    if(std::filesystem::exists(path))key=loadUserSecret(path);else {
        // Never silently replace a lost key for an already protected ledger.
        const auto db=dataRoot/"archive"/"records.sqlite";
        if(std::filesystem::exists(db)){auto ledger=ayt::storage::ISqlite::create();if(!ledger->open(pathUtf8(db)))throw std::runtime_error("vault_archive_unavailable");if(ledger->tableExists("ay_record_encoding")){auto q=ledger->prepare("SELECT codec FROM ay_record_encoding");if(!q||q->stepChecked()!=ayt::storage::SqliteStepResult::Row||q->getText(0)!="plain")throw std::runtime_error("vault_key_missing");}}
        key.resize(32);if(!ayt::crypto::tryGenerateRandomBytes(reinterpret_cast<uint8_t*>(key.data()),key.size()))throw std::runtime_error("vault_random_unavailable");if(!saveUserSecret(path,key))throw std::runtime_error("vault_key_save");
    }
    return std::make_shared<VaultCodec>(key);
}
}
