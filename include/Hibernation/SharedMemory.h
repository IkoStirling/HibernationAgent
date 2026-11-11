#pragma once
#include <Hibernation/MemoryLibrary.h>
#include <Hibernation/Vault.h>
#include <sstream>
#include <iomanip>
namespace hibernation {
struct SharedMemoryRoot {std::filesystem::path path;std::string tag;bool desktop=false,searchable=true;};
inline std::string memoryRootTag(const std::filesystem::path& path){
    auto text=path.lexically_normal().generic_u8string();std::uint64_t hash=14695981039346656037ull;
    for(auto ch:text){auto c=static_cast<unsigned char>(ch);if(c>='A'&&c<='Z')c+=32;hash=(hash^c)*1099511628211ull;}
    std::ostringstream out;out<<"shared-"<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<":";return out.str();
}
// Only fixed application-owned directories are discovered. Model text cannot
// supply roots, paths or SQL. Junctions escaping the archive root are excluded.
inline std::vector<SharedMemoryRoot> sharedMemoryRoots(const std::filesystem::path& main,const std::filesystem::path& current){
    std::vector<SharedMemoryRoot> roots;if(main.empty())return roots;
    std::error_code ec;const auto base=std::filesystem::weakly_canonical(main,ec);if(ec)return roots;
    const auto own=std::filesystem::weakly_canonical(current,ec);if(ec)return roots;
    auto add=[&](const std::filesystem::path& p,bool desktop=false){if(roots.size()>=256)return;std::error_code error;const auto absolute=std::filesystem::weakly_canonical(p,error);if(error)return;
        auto rel=absolute.lexically_relative(base);if(rel.empty()||rel.is_absolute()||*rel.begin()=="..")return;
        if(!std::filesystem::is_regular_file(absolute/"archive/records.sqlite",error)||!std::filesystem::is_regular_file(absolute/"runtime/memory-key.dpapi",error))return;
        if(std::none_of(roots.begin(),roots.end(),[&](const auto& r){return r.path==absolute;}))roots.push_back({absolute,memoryRootTag(absolute),desktop,absolute!=own});
    };
    add(base,true);
    auto numericDirs=[](const std::filesystem::path& parent){std::vector<std::filesystem::path> result;std::error_code error;
        for(std::filesystem::directory_iterator it(parent,error),end;!error&&it!=end&&result.size()<256;it.increment(error)){
            const auto name=it->path().filename().string();if(!name.empty()&&name.size()<=20&&std::all_of(name.begin(),name.end(),[](unsigned char c){return c>='0'&&c<='9';})&&it->is_directory(error))result.push_back(it->path());
        }std::sort(result.begin(),result.end());return result;};
    for(const auto& bot:numericDirs(base/"channels/napcat")){
        for(const auto& user:numericDirs(bot/"private"))add(user);
        for(const auto& group:numericDirs(bot/"groups"))for(const auto& user:numericDirs(group/"members"))add(user);
    }
    return roots;
}
inline bool sharedMemoryId(const std::string& id){return id.starts_with("shared-")&&id.size()>24&&id[23]==':';}
struct SharedMemoryResult {Json rows=Json::array();bool limited=false;};
inline SharedMemoryResult querySharedMemory(const std::vector<SharedMemoryRoot>& roots,bool archive,const std::string& query,const std::string& key={},std::size_t limit=8,std::size_t offset=0,std::size_t length=2400,const std::function<bool()>& cancelled={},unsigned depth=0,std::size_t* inheritedBudget=nullptr,bool nativeProvenance=false){
    struct Match{Json value;int score;std::int64_t time;};std::vector<Match> matches;SharedMemoryResult output;std::size_t scannedBytes=0;
    std::size_t localBudget=128;auto& accessBudget=inheritedBudget?*inheritedBudget:localBudget;
    if(depth>8||accessBudget==0){output.limited=true;return output;}
    for(const auto& root:roots){if(cancelled&&cancelled())break;if(key.empty()&&!root.searchable)continue;if(!key.empty()&&!key.starts_with(root.tag))continue;
        try{ayt::storage::RecordStore store;if(!store.open(root.path/"archive/records.sqlite",{openVaultCodec(root.path),false,false}))continue;
            ayt::storage::RecordQuery mq;mq.scope="ha.memory";mq.limit=65536;mq.maxPayloadBytes=16*1024*1024;mq.cancelled=cancelled;
            const auto memories=store.query({{"ha.memory"}},mq);if(!memories||memories.records.size()==mq.limit){output.limited=true;continue;}
            auto hidden=store.query({{"ha.access"}},{"ha.access","current"});if(!hidden)continue;Json ids=Json::array();if(!hidden.records.empty())ids=Json::parse(hidden.records.front().payload).at("hidden");
            const auto policy=memoryVisibility(memories.records,ids);auto records=memories;
            if(archive){ayt::storage::RecordQuery q;q.scope="ha.archive";q.limit=8192;q.maxPayloadBytes=8*1024*1024;q.cancelled=cancelled;if(!key.empty()){q.id=key.substr(root.tag.size());q.limit=1;}
                records=store.query({{"ha.archive"}},q);if(!records){output.limited=true;continue;}if(records.records.size()==8192)output.limited=true;}
            for(const auto& row:records.records){scannedBytes+=row.payload.size();if(scannedBytes>32*1024*1024){output.limited=true;break;}if(!key.empty()&&root.tag+row.id!=key)continue;
                auto data=Json::parse(row.payload);if(archive?policy.archives.contains(row.id)||(!(nativeProvenance&&!key.empty())&&!libraryArchiveKind(data.value("kind",std::string{})))||isLegacyMetaReply(data):policy.memories.contains(row.id)||!policy.allows(data))continue;
                const auto text=memorySearchText(data);int score=query.empty()?1:memoryRelevance(query,text);if(!query.empty()&&text.find(query)!=std::string::npos)score+=50;if(key.empty()&&score==0)continue;
                bool accessible=true;
                for(const auto& ref:data.value("sources",Json::array())){
                    const auto scope=ref.value("scope",std::string{}),id=ref.at("id").get<std::string>();
                    const bool external=sharedMemoryId(id);
                    // Local raw-source hiding is already in MemoryVisibility;
                    // local memory and foreign references may carry another
                    // channel's revoked source, so follow their provenance too.
                    if(!external&&scope!="ha.memory")continue;
                    if(accessBudget==0){output.limited=true;accessible=false;break;}--accessBudget;
                    const auto reference=querySharedMemory(roots,scope.ends_with("ha.archive"),"",external?id:root.tag+id,1,0,1,cancelled,depth+1,&accessBudget,true);
                    output.limited=output.limited||reference.limited;if(reference.rows.empty()){accessible=false;break;}
                }
                if(!accessible)continue;
                const auto stamp=data.value("created_at",row.occurredAtMs);
                if(!data.contains("sources"))data["sources"]=Json::array();
                for(auto& ref:data["sources"]){
                    const auto scope=ref.value("scope",std::string{});if(scope=="ha.archive"||scope=="ha.memory"){ref["id"]=root.tag+ref.at("id").get<std::string>();ref["scope"]=root.tag+scope;}}
                data=libraryPage(std::move(data),key.empty()?0:offset,key.empty()?900:length);data["id"]=root.tag+row.id;data["revision"]=row.revision;data["scope"]=root.tag+(archive?"ha.archive":"ha.memory");data["storage_origin"]=root.desktop?"desktop":"qq";
                matches.push_back({std::move(data),score,stamp});
            }
            if(scannedBytes>32*1024*1024)break;
        }catch(...){output.limited=true;}
    }
    std::stable_sort(matches.begin(),matches.end(),[](const auto& a,const auto& b){return a.score!=b.score?a.score>b.score:a.time!=b.time?a.time>b.time:a.value.at("id")<b.value.at("id");});
    for(auto& match:matches){if(output.rows.size()>=limit)break;output.rows.push_back(std::move(match.value));}return output;
}
}
