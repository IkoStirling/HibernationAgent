#include <Hibernation/VaultPortability.h>
#include <Hibernation/Vault.h>
#include <AYCrypto.h>
#include <AYIO/File.h>
#include <AYStorage/ISqlite.h>
#include <Windows.h>
#include <Wincrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace hibernation::portableVault {
namespace {
namespace fs=std::filesystem;
constexpr std::size_t maxBundle=10*1024*1024,maxDecoded=4*1024*1024,maxStores=128,maxManifest=1024*1024;
[[noreturn]] void fail(const char* code){throw std::runtime_error(code);}
void checkpoint(const Cancellation& cancel){if(cancel&&cancel->load())fail("portable_cancelled");}
struct Secret {
    std::string value;
    Secret()=default;
    explicit Secret(std::string&& text):value(text){ayt::crypto::secureZero(text.data(),text.size());text.clear();}
    Secret(const Secret&)=delete;Secret& operator=(const Secret&)=delete;
    Secret(Secret&& other)noexcept:value(std::move(other.value)){ayt::crypto::secureZero(other.value.data(),other.value.size());other.value.clear();}
    ~Secret(){ayt::crypto::secureZero(value.data(),value.size());}
};
struct Master {
    std::string root;
    std::array<std::uint8_t,32> key{};
    Master()=default;Master(const Master&)=delete;Master& operator=(const Master&)=delete;
    Master(Master&& other)noexcept:root(std::move(other.root)),key(other.key){ayt::crypto::secureZero(other.key.data(),other.key.size());}
    ~Master(){ayt::crypto::secureZero(key.data(),key.size());}
};
std::string text(const fs::path& p){const auto v=p.u8string();return {reinterpret_cast<const char*>(v.data()),v.size()};}
std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return s;}
fs::path native(const fs::path& p){const auto s=fs::absolute(p).native();return s.starts_with(L"\\\\?\\")?p:fs::path(L"\\\\?\\"+s);}
void plain(const fs::path& path){for(auto p=fs::absolute(path);!p.empty();){const auto flags=GetFileAttributesW(native(p).c_str());if(flags!=INVALID_FILE_ATTRIBUTES&&(flags&FILE_ATTRIBUTE_REPARSE_POINT))fail("portable_path_rejected");const auto parent=p.parent_path();if(parent==p)break;p=parent;}}
bool contains(const fs::path& a,const fs::path& b){const auto x=fs::weakly_canonical(native(a)),y=fs::weakly_canonical(native(b));auto i=x.begin(),j=y.begin();for(;i!=x.end();++i,++j){if(j==y.end())return false;const auto l=i->native(),r=j->native();if(CompareStringOrdinal(l.data(),static_cast<int>(l.size()),r.data(),static_cast<int>(r.size()),TRUE)!=CSTR_EQUAL)return false;}return true;}
void segment(std::string_view s){
    if(s.empty()||s=="."||s==".."||s.back()=='.'||s.back()==' '||s.find_first_of("<>:\"|?*\\")!=std::string_view::npos||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32;}))fail("portable_path_rejected");
    const auto dot=s.find('.');const auto base=lower(std::string(s.substr(0,dot)));if(base=="con"||base=="prn"||base=="aux"||base=="nul"||base=="clock$"||(base.size()==4&&(base.starts_with("com")||base.starts_with("lpt"))&&base[3]>='0'&&base[3]<='9'))fail("portable_path_rejected");
}
void relative(std::string_view s){if(s.empty()||s.size()>2048||s.front()=='/'||s.back()=='/')fail("portable_path_rejected");std::size_t offset=0,parts=0;while(offset<s.size()){const auto end=s.find('/',offset);segment(s.substr(offset,end==std::string_view::npos?s.size()-offset:end-offset));if(++parts>32)fail("portable_path_rejected");if(end==std::string_view::npos)break;offset=end+1;}}
void rootPath(const fs::path& path){const auto s=text(path);if(s.size()<4||s.size()>2048||!std::isalpha(static_cast<unsigned char>(s[0]))||s[1]!=':'||(s[2]!='/'&&s[2]!='\\')||!path.is_absolute()||path==path.root_path())fail("portable_path_rejected");auto tail=s.substr(3);std::replace(tail.begin(),tail.end(),'\\','/');relative(tail);plain(path);}
fs::path child(const fs::path& root,const std::string& rel){relative(rel);const auto out=root/fs::u8path(rel);plain(out);if(!contains(root,out))fail("portable_path_rejected");return out;}
fs::path storeRoot(const fs::path& root,const std::string& rel){return rel=="."?root:child(root,rel);}
void storeName(const std::string& name){if(name==".")return;relative(name);if(!lower(name).starts_with("channels/")||name.size()>1024)fail("portable_bundle_invalid");}
std::string read(const fs::path& path,std::size_t limit){plain(path);std::ifstream in(native(path),std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||static_cast<std::uint64_t>(in.tellg())>limit)fail("portable_backup_invalid");std::string out(static_cast<std::size_t>(in.tellg()),'\0');in.seekg(0);if(!out.empty()&&!in.read(out.data(),out.size()))fail("portable_backup_invalid");return out;}
std::string hex(std::string_view s){constexpr char chars[]="0123456789abcdef";std::string out;out.reserve(s.size()*2);for(unsigned char c:s){out+=chars[c>>4];out+=chars[c&15];}return out;}
std::string unhex(const std::string& s,std::size_t limit){if(s.size()%2||s.size()>limit*2)fail("portable_bundle_invalid");std::string out;out.reserve(s.size()/2);auto n=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};for(std::size_t i=0;i<s.size();i+=2){const auto a=n(s[i]),b=n(s[i+1]);if(a<0||b<0)fail("portable_bundle_invalid");out+=static_cast<char>((a<<4)|b);}return out;}
std::string hashFile(const fs::path& path,const Cancellation& cancel){plain(path);auto hash=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);if(!hash)fail("portable_crypto_unavailable");hash->init();std::ifstream in(native(path),std::ios::binary);if(!in)fail("portable_backup_invalid");std::array<char,65536> buffer{};while(in){checkpoint(cancel);in.read(buffer.data(),buffer.size());hash->update(reinterpret_cast<const std::uint8_t*>(buffer.data()),static_cast<std::size_t>(in.gcount()));}if(!in.eof())fail("portable_backup_invalid");std::array<std::uint8_t,32> digest{};hash->final(digest.data());return hex({reinterpret_cast<const char*>(digest.data()),digest.size()});}
void passwordValid(const std::string& p){if(p.size()<12||p.size()>1024||p.find('\0')!=std::string::npos||std::all_of(p.begin(),p.end(),[](unsigned char c){return std::isspace(c);}))fail("portable_password");}
void derive(const Secret& password,std::string_view salt,std::array<std::uint8_t,32>& key){auto kdf=ayt::crypto::createKeyDerivation("Argon2id");if(!kdf||!kdf->derive(reinterpret_cast<const std::uint8_t*>(password.value.data()),password.value.size(),reinterpret_cast<const std::uint8_t*>(salt.data()),salt.size(),3,key.data(),key.size()))fail("portable_crypto_unavailable");}
struct Derived{std::array<std::uint8_t,32> value{};~Derived(){ayt::crypto::secureZero(value.data(),value.size());}};
void put32(std::string& out,std::uint32_t n){for(int i=0;i<4;++i)out+=static_cast<char>((n>>(i*8))&255);}
std::uint32_t get32(std::string_view in,std::size_t& at){if(at+4>in.size())fail("portable_unlock_failed");std::uint32_t n=0;for(int i=0;i<4;++i)n|=static_cast<std::uint32_t>(static_cast<unsigned char>(in[at++]))<<(i*8);return n;}
#include "MentorPortability.inc"
PortableMaterial unlock(const Json& bundle,Secret& password,const Cancellation& cancel)try{
    passwordValid(password.value);checkpoint(cancel);
    if(!bundle.is_object()||bundle.size()!=4||bundle.dump().size()>maxBundle)fail("portable_bundle_invalid");const auto format=bundle.value("format",std::string{});const bool v2=format=="hibernation.portable-keys.v2";if(!v2&&format!="hibernation.portable-keys.v1")fail("portable_bundle_invalid");
    const auto& k=bundle.at("kdf");const auto& c=bundle.at("cipher");
    if(!k.is_object()||k.size()!=5||k.value("name",std::string{})!="argon2id"||k.value("iterations",0)!=3||k.value("memory_kib",0)!=65536||k.value("parallelism",0)!=1||!c.is_object()||c.size()!=2||c.value("name",std::string{})!="xchacha20-poly1305")fail("portable_bundle_invalid");
    const auto salt=unhex(k.at("salt_hex").get<std::string>(),16),nonce=unhex(c.at("nonce_hex").get<std::string>(),24),packet=unhex(bundle.at("ciphertext_hex").get<std::string>(),maxDecoded+16);
    if(salt.size()!=16||nonce.size()!=24||packet.size()<17)fail("portable_bundle_invalid");
    Derived key;derive(password,salt,key.value);checkpoint(cancel);auto cipher=ayt::crypto::createAeadCipher(ayt::crypto::AeadAlgo::XChaCha20_Poly1305);if(!cipher||!cipher->setKey(key.value.data(),key.value.size())||!cipher->setNonce(reinterpret_cast<const std::uint8_t*>(nonce.data()),nonce.size()))fail("portable_crypto_unavailable");
    Secret decoded;decoded.value.resize(packet.size()-16);std::size_t size=decoded.value.size();if(!cipher->decrypt(reinterpret_cast<const std::uint8_t*>(packet.data()),size,reinterpret_cast<const std::uint8_t*>(packet.data()+size),reinterpret_cast<std::uint8_t*>(decoded.value.data()),&size))fail("portable_unlock_failed");decoded.value.resize(size);
    const std::string_view magic=v2?"HAPORTKEY2\n":"HAPORTKEY1\n";if(!decoded.value.starts_with(magic))fail("portable_unlock_failed");std::size_t at=magic.size();const auto headerBytes=get32(decoded.value,at);const Json header={{"format",bundle.at("format")},{"kdf",k},{"cipher",c}};if(headerBytes>4096||at+headerBytes>size||std::string_view(decoded.value).substr(at,headerBytes)!=header.dump())fail("portable_unlock_failed");at+=headerBytes;
    const auto count=get32(decoded.value,at);if(count<1||count>maxStores)fail("portable_bundle_invalid");PortableMaterial out;out.stores.reserve(count);std::set<std::string> names;
    for(std::size_t i=0;i<count;++i){checkpoint(cancel);const auto n=get32(decoded.value,at);if(n<1||n>1024||at+n+32>size)fail("portable_unlock_failed");Master m;m.root=decoded.value.substr(at,n);storeName(m.root);if(!names.insert(lower(m.root)).second)fail("portable_bundle_invalid");at+=n;std::copy_n(reinterpret_cast<const std::uint8_t*>(decoded.value.data()+at),32,m.key.begin());at+=32;out.stores.push_back(std::move(m));}
    if(v2){const auto attachments=get32(decoded.value,at);if(attachments<1||attachments>maxMentorFiles)fail("portable_mentor_limit");out.mentor.reserve(attachments);for(std::uint32_t i=0;i<attachments;++i){checkpoint(cancel);MentorAttachment item;const auto n=get32(decoded.value,at);if(n<1||n>128||at+n+64>size)fail("portable_bundle_invalid");item.path=decoded.value.substr(at,n);at+=n;item.digest=decoded.value.substr(at,64);at+=64;const auto bytes=get32(decoded.value,at);if(bytes>1024*1024||at+bytes>size)fail("portable_mentor_limit");item.plain.value=decoded.value.substr(at,bytes);at+=bytes;out.mentor.push_back(std::move(item));}validateMentor(out.mentor);}
    if(at!=size||!names.contains("."))fail("portable_bundle_invalid");return out;
}catch(const nlohmann::json::exception&){fail("portable_bundle_invalid");}
bool keyReceipt(const std::string& path){const auto p=lower(path);return p=="data/runtime/memory-key.dpapi"||(p.starts_with("data/channels/")&&p.ends_with("/runtime/memory-key.dpapi"));}
bool runtimeReceipt(const std::string& path){const auto p=lower(path);return p.starts_with("data/runtime/")||(p.starts_with("data/channels/")&&p.find("/runtime/")!=std::string::npos);}
struct Snapshot{Json manifest;std::size_t stores=0,records=0;std::uintmax_t bytes=0;};
struct EncryptedScratch {
    fs::path path;
    explicit EncryptedScratch(const fs::path& original,const Cancellation& cancel){
        std::array<std::uint8_t,16> random{};if(!ayt::crypto::tryGenerateRandomBytes(random.data(),random.size()))fail("portable_crypto_unavailable");
        path=original.parent_path()/fs::u8path(".portable-verify-"+hex({reinterpret_cast<const char*>(random.data()),random.size()})+".sqlite");plain(path);
        const auto bytes=fs::file_size(native(original));if(bytes>UINT64_MAX-64*1024*1024ULL||fs::space(native(original.parent_path())).available<bytes+64*1024*1024ULL)fail("portable_operation_failed");
        const auto progress=[](LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE,LPVOID context)->DWORD{const auto* signal=static_cast<const Cancellation*>(context);return *signal&&(*signal)->load()?PROGRESS_CANCEL:PROGRESS_CONTINUE;};
        if(!CopyFileExW(native(original).c_str(),native(path).c_str(),progress,const_cast<Cancellation*>(&cancel),nullptr,COPY_FILE_FAIL_IF_EXISTS)){
            const auto problem=GetLastError();if(problem!=ERROR_FILE_EXISTS&&problem!=ERROR_ALREADY_EXISTS)try{plain(path);if(fs::is_regular_file(native(path)))fs::remove(native(path));}catch(...){}checkpoint(cancel);fail("portable_operation_failed");
        }
    }
    ~EncryptedScratch(){for(const auto* suffix:{"","-wal","-shm","-journal"})try{const auto p=fs::path(path.native()+fs::u8path(suffix).native());plain(p);if(fs::is_regular_file(native(p)))fs::remove(native(p));}catch(...) {}}
};
std::size_t authenticate(const fs::path& path,const std::shared_ptr<const ayt::storage::IRecordPayloadCodec>& codec,const Cancellation& cancel){
    plain(path);if(!fs::is_regular_file(native(path)))fail("portable_backup_invalid");
    // ISqlite::open changes journal mode, so even a query-only connection must
    // use an encrypted scratch copy. The original archive bytes are never opened.
    EncryptedScratch scratch(path,cancel);auto db=ayt::storage::ISqlite::create();if(!db||!db->open(text(native(scratch.path)))||!db->execute("PRAGMA query_only=ON"))fail("portable_backup_invalid");
    auto encoding=db->prepare("SELECT codec FROM ay_record_encoding");if(!encoding||encoding->stepChecked()!=ayt::storage::SqliteStepResult::Row||!codec->canReadIdentity(encoding->getText(0))||encoding->stepChecked()!=ayt::storage::SqliteStepResult::Done)fail("portable_backup_mismatch");encoding.reset();
    auto bounds=db->prepare("SELECT 1 FROM ay_record_versions WHERE length(CAST(payload AS BLOB))>33554432 OR length(CAST(scope AS BLOB))>1024 OR length(CAST(id AS BLOB))>1024 OR length(CAST(source AS BLOB))>4096 OR length(CAST(correlation AS BLOB))>4096 LIMIT 1");if(!bounds||bounds->stepChecked()!=ayt::storage::SqliteStepResult::Done)fail("portable_backup_invalid");bounds.reset();
    auto q=db->prepare("SELECT scope,id,revision,schema_version,payload,source,correlation,occurred_ms FROM ay_record_versions ORDER BY scope,id,revision");if(!q)fail("portable_backup_invalid");std::size_t count=0;
    for(;;){checkpoint(cancel);const auto result=q->stepChecked();if(result==ayt::storage::SqliteStepResult::Done)break;if(result!=ayt::storage::SqliteStepResult::Row||++count>10000000)fail("portable_backup_invalid");ayt::storage::VersionedRecord m{q->getText(0),q->getText(1),q->getInt64(2),q->getInt(3),{},q->getText(5),q->getText(6),q->getInt64(7)};const auto encoded=q->getText(4);if(encoded.size()>32*1024*1024)fail("portable_backup_invalid");Secret decoded;if(!codec->decode(m,encoded,decoded.value))fail("portable_backup_mismatch");}
    q.reset();db->close();return count;
}
Snapshot checkSnapshot(const fs::path& root,const PortableMaterial& material,const Cancellation& cancel)try{
    const auto& masters=material.stores;std::map<std::string,const MentorAttachment*> mentor;for(const auto& file:material.mentor)mentor.emplace(file.path,&file);std::set<std::string> mentorSeen;
    rootPath(root);Snapshot out;try{out.manifest=Json::parse(read(root/"manifest.json",maxManifest));}catch(const nlohmann::json::exception&){fail("portable_backup_invalid");}
    const auto& m=out.manifest;if(m.value("format",std::string{})!="hibernation.archive-backup.v1"||!m.value("complete",false)||!m.value("originals_encrypted",false)||m.value("api_credentials_included",true)||!m.contains("files")||!m.at("files").is_array()||m.at("files").empty()||m.at("files").size()>4096)fail("portable_backup_invalid");
    std::map<std::string,const Master*> keys;for(const auto& key:masters)keys.emplace(lower(key.root),&key);std::set<std::string> seen,stores;
    for(const auto& file:m.at("files")){
        checkpoint(cancel);const auto rel=file.at("path").get<std::string>();const auto path=child(root,rel);if(!seen.insert(lower(rel)).second||lower(rel)=="data/recovery_incomplete.json"||lower(rel)=="data/portable-recovery.json")fail("portable_backup_invalid");const auto bytes=file.at("bytes").get<std::uintmax_t>();const auto digest=file.at("sha256").get<std::string>();if(digest.size()!=64||!file.at("bytes").is_number_unsigned())fail("portable_backup_invalid");
        // The portable bundle deliberately replaces machine-bound master blobs.
        // A copy may omit only these blobs; every archive still must match.
        if(keyReceipt(rel)&&!fs::exists(native(path)))continue;
        if(!fs::is_regular_file(native(path))||fs::file_size(native(path))!=bytes||hashFile(path,cancel)!=digest)fail("portable_backup_mismatch");
        if(rel.starts_with("data/")&&mentorBlob(rel.substr(5))){const auto sub=rel.substr(5);if(!mentorPath(sub))fail("portable_mentor_unsupported");const auto found=mentor.find(sub);if(found==mentor.end())fail("portable_mentor_bundle_required");if(found->second->digest!=digest)fail("portable_mentor_backup_mismatch");mentorSeen.insert(sub);}
        const auto folded=lower(rel);if(folded.starts_with("data/")){if(bytes>UINT64_MAX-out.bytes)fail("portable_backup_invalid");out.bytes+=bytes;}
        const auto personAt=folded.rfind("/people/");
        if(folded.starts_with("data/")&&personAt!=std::string::npos&&folded.ends_with("/records.sqlite")){
            const auto person=rel.substr(personAt+8,rel.size()-personAt-8-15);if(person.empty()||person.size()>64||!std::all_of(person.begin(),person.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='-'||c=='_');}))fail("portable_backup_invalid");
            const auto parent=personAt==4?std::string("."):rel.substr(5,personAt-5);storeName(parent);const auto found=keys.find(lower(parent));if(found==keys.end())fail("portable_backup_mismatch");const auto& master=*found->second;
            out.records+=authenticate(path,vaultCodecFromMasterKey({reinterpret_cast<const char*>(master.key.data()),master.key.size()}),cancel);continue;
        }
        std::string store;if(folded=="data/archive/records.sqlite")store=".";else if(folded.starts_with("data/channels/")&&folded.ends_with("/archive/records.sqlite"))store=rel.substr(5,rel.size()-5-23);else continue;
        storeName(store);const auto found=keys.find(lower(store));if(found==keys.end()||!stores.insert(lower(store)).second)fail("portable_backup_mismatch");const auto& key=*found->second;const auto codec=vaultCodecFromMasterKey({reinterpret_cast<const char*>(key.key.data()),key.key.size()});out.records+=authenticate(path,codec,cancel);if(hashFile(path,cancel)!=digest)fail("portable_backup_mismatch");++out.stores;
    }
    if(!stores.contains("."))fail("portable_backup_invalid");if(mentorSeen.size()!=material.mentor.size())fail("portable_mentor_backup_mismatch");return out;
}catch(const nlohmann::json::exception&){fail("portable_backup_invalid");}
Json summary(const Snapshot& s){return {{"verified",true},{"stores",s.stores},{"records",s.records},{"files",s.manifest.at("files").size()},{"archive_bytes",s.bytes}};}
void write(const fs::path& path,const std::string& s){plain(path);if(!ayt::io::File::atomicWriteUtf8(text(path),s.data(),s.size()))fail("portable_operation_failed");}
}

void clearSensitiveBuffer(std::string& value)noexcept{ayt::crypto::secureZero(value.data(),value.size());value.clear();}
Json exportBundle(const fs::path& dataRoot,std::string password,const Cancellation& cancel){
    Secret secret(std::move(password));passwordValid(secret.value);rootPath(dataRoot);std::vector<Master> masters;masters.reserve(maxStores);std::size_t visited=0;std::set<std::string> names;
    for(const auto& e:fs::recursive_directory_iterator(native(dataRoot))){checkpoint(cancel);plain(e.path());if(++visited>100000)fail("portable_key_unavailable");if(!e.is_regular_file())continue;auto rel=text(e.path().lexically_relative(native(dataRoot)));std::replace(rel.begin(),rel.end(),'\\','/');const auto folded=lower(rel);std::string name;if(folded=="archive/records.sqlite")name=".";else if(folded.starts_with("channels/")&&folded.ends_with("/archive/records.sqlite"))name=rel.substr(0,rel.size()-23);else continue;storeName(name);if(masters.size()>=maxStores||!names.insert(lower(name)).second)fail("portable_key_unavailable");Master m;m.root=name;Secret raw;try{raw.value=loadUserSecret(storeRoot(dataRoot,name)/"runtime/memory-key.dpapi");}catch(...){fail("portable_key_unavailable");}if(raw.value.size()!=32)fail("portable_key_unavailable");std::copy(raw.value.begin(),raw.value.end(),m.key.begin());masters.push_back(std::move(m));}
    if(!names.contains("."))fail("portable_key_unavailable");
    auto mentor=collectMentor(dataRoot,cancel);
    std::array<std::uint8_t,16> salt{};std::array<std::uint8_t,24> nonce{};if(!ayt::crypto::tryGenerateRandomBytes(salt.data(),salt.size())||!ayt::crypto::tryGenerateRandomBytes(nonce.data(),nonce.size()))fail("portable_crypto_unavailable");
    Json header={{"format",mentor.empty()?"hibernation.portable-keys.v1":"hibernation.portable-keys.v2"},{"kdf",{{"name","argon2id"},{"iterations",3},{"memory_kib",65536},{"parallelism",1},{"salt_hex",hex({reinterpret_cast<const char*>(salt.data()),salt.size()})}}},{"cipher",{{"name","xchacha20-poly1305"},{"nonce_hex",hex({reinterpret_cast<const char*>(nonce.data()),nonce.size()})}}}};
    Secret payload;payload.value=mentor.empty()?"HAPORTKEY1\n":"HAPORTKEY2\n";const auto metadata=header.dump();std::size_t payloadBytes=payload.value.size()+8+metadata.size();for(const auto& m:masters)payloadBytes+=4+m.root.size()+m.key.size();payload.value.reserve(payloadBytes);put32(payload.value,static_cast<std::uint32_t>(metadata.size()));payload.value+=metadata;put32(payload.value,static_cast<std::uint32_t>(masters.size()));for(const auto& m:masters){put32(payload.value,static_cast<std::uint32_t>(m.root.size()));payload.value+=m.root;payload.value.append(reinterpret_cast<const char*>(m.key.data()),m.key.size());}
    if(!mentor.empty()){put32(payload.value,static_cast<std::uint32_t>(mentor.size()));for(const auto& file:mentor){if(payload.value.size()+8+file.path.size()+file.digest.size()+file.plain.value.size()>maxDecoded)fail("portable_mentor_limit");put32(payload.value,static_cast<std::uint32_t>(file.path.size()));payload.value+=file.path;payload.value+=file.digest;put32(payload.value,static_cast<std::uint32_t>(file.plain.value.size()));payload.value+=file.plain.value;}}
    Derived key;derive(secret,{reinterpret_cast<const char*>(salt.data()),salt.size()},key.value);checkpoint(cancel);auto cipher=ayt::crypto::createAeadCipher(ayt::crypto::AeadAlgo::XChaCha20_Poly1305);if(!cipher||!cipher->setKey(key.value.data(),key.value.size())||!cipher->setNonce(nonce.data(),nonce.size()))fail("portable_crypto_unavailable");std::string packet(payload.value.size()+16,'\0');std::size_t size=payload.value.size();if(!cipher->encrypt(reinterpret_cast<const std::uint8_t*>(payload.value.data()),payload.value.size(),reinterpret_cast<std::uint8_t*>(packet.data()),&size,reinterpret_cast<std::uint8_t*>(packet.data()+payload.value.size()))||size!=payload.value.size())fail("portable_crypto_unavailable");header["ciphertext_hex"]=hex(packet);if(header.dump().size()>maxBundle)fail("portable_bundle_invalid");return {{"bundle",header},{"filename",mentor.empty()?"hibernation-portable-keys.v1.json":"hibernation-portable-keys.v2.json"},{"stores",masters.size()},{"mentor_files",mentor.size()},{"includes_memory_contents",!mentor.empty()}};
}
Json verifyBackup(const Json& bundle,std::string password,const fs::path& backupRoot,const Cancellation& cancel){Secret secret(std::move(password));auto keys=unlock(bundle,secret,cancel);return summary(checkSnapshot(backupRoot,keys,cancel));}
Json restoreBackup(const Json& bundle,std::string password,const fs::path& backupRoot,const fs::path& targetRoot,const std::vector<fs::path>& protectedRoots,const Cancellation& cancel){
    Secret secret(std::move(password));rootPath(targetRoot);rootPath(backupRoot);if(contains(backupRoot,targetRoot)||contains(targetRoot,backupRoot))fail("portable_path_rejected");for(const auto& protectedRoot:protectedRoots)if(!protectedRoot.empty()&&(contains(protectedRoot,targetRoot)||contains(targetRoot,protectedRoot)))fail("portable_path_rejected");if(fs::exists(native(targetRoot)))fail("portable_target_exists");
    auto keys=unlock(bundle,secret,cancel);auto checked=checkSnapshot(backupRoot,keys,cancel);checkpoint(cancel);
    // A random sibling staging directory is retained on failure, never deleted.
    // Publishing is a final no-replace rename, after DPAPI and row verification.
    std::array<std::uint8_t,12> random{};if(!ayt::crypto::tryGenerateRandomBytes(random.data(),random.size()))fail("portable_crypto_unavailable");const auto stage=fs::path(targetRoot.native()+L".recovering-"+fs::u8path(hex({reinterpret_cast<const char*>(random.data()),random.size()})).native());rootPath(stage);if(fs::exists(native(stage)))fail("portable_target_exists");fs::create_directories(native(stage.parent_path()));plain(stage.parent_path());if(checked.bytes>UINT64_MAX-64*1024*1024ULL||fs::space(native(stage.parent_path())).available<checked.bytes+64*1024*1024ULL)fail("portable_operation_failed");if(!fs::create_directory(native(stage)))fail("portable_target_exists");write(stage/"RECOVERY_INCOMPLETE.json","{\"complete\":false}");
    Json restoredManifest=checked.manifest;restoredManifest["files"]=Json::array();
    for(const auto& file:checked.manifest.at("files")){
        checkpoint(cancel);const auto rel=file.at("path").get<std::string>();if(!lower(rel).starts_with("data/")||runtimeReceipt(rel)||mentorBlob(rel.substr(5)))continue;const auto src=child(backupRoot,rel),dest=child(stage,rel.substr(5));plain(src);fs::create_directories(native(dest.parent_path()));plain(dest.parent_path());if(hashFile(src,cancel)!=file.at("sha256").get<std::string>()||!fs::copy_file(native(src),native(dest),fs::copy_options::none)||hashFile(dest,cancel)!=file.at("sha256").get<std::string>())fail("portable_backup_mismatch");restoredManifest["files"].push_back(file);
    }
    for(const auto& key:keys.stores){checkpoint(cancel);const auto store=storeRoot(stage,key.root);if(!fs::exists(native(store/"archive/records.sqlite")))continue;fs::create_directories(native(store/"runtime"));plain(store/"runtime");Secret raw(std::string(reinterpret_cast<const char*>(key.key.data()),key.key.size()));if(!saveUserSecret(store/"runtime/memory-key.dpapi",raw.value))fail("portable_operation_failed");authenticate(store/"archive/records.sqlite",openVaultCodec(store),cancel);}
    for(const auto& file:keys.mentor){checkpoint(cancel);const auto dest=child(stage,file.path);fs::create_directories(native(dest.parent_path()));plain(dest.parent_path());if(!saveUserSecret(dest,file.plain.value))fail("portable_operation_failed");auto decoded=unprotectMentor(dest);if(decoded.value!=file.plain.value)fail("portable_mentor_backup_mismatch");}
    // Recheck every encrypted archive byte after validation, then publish only
    // complete data. No configuration or original backup is modified.
    for(const auto& file:restoredManifest.at("files"))if(hashFile(child(stage,file.at("path").get<std::string>().substr(5)),cancel)!=file.at("sha256").get<std::string>())fail("portable_backup_mismatch");checkpoint(cancel);
    auto out=summary(checked);out["mentor_files_rewrapped"]=keys.mentor.size();out["restored"]=true;out["targetRoot"]=text(targetRoot);out["originals_unchanged"]=true;out["automatic_activation"]=false;write(stage/"portable-recovery.json",out.dump(2));plain(targetRoot);if(fs::exists(native(targetRoot)))fail("portable_target_exists");if(!MoveFileExW(native(stage).c_str(),native(targetRoot).c_str(),0)){const auto code=GetLastError();if(code==ERROR_ALREADY_EXISTS||code==ERROR_FILE_EXISTS)fail("portable_target_exists");fail("portable_operation_failed");}if(!fs::remove(native(targetRoot/"RECOVERY_INCOMPLETE.json")))fail("portable_operation_failed");return out;
}
}
