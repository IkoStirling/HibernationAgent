// Operator diagnostic: metadata only; never prints decrypted conversation text.
#include <Hibernation/SharedMemory.h>
#include <fstream>
#include <iostream>
using namespace hibernation;
int main(int argc,char** argv){if(argc!=4)return 2;const auto main=std::filesystem::u8path(argv[1]),current=std::filesystem::u8path(argv[2]);ayt::storage::RecordStore local;if(!local.open(current/"archive/records.sqlite",{openVaultCodec(current),false,false}))return 3;
 if(std::string(argv[3])=="--outbox"){ayt::storage::RecordQuery q;q.scope="ha.plugin-events";q.contains="\"journaled\":false";q.limit=256;const auto result=local.query({{"ha.plugin-events"}},q);std::cout<<Json{{"outbox_status",static_cast<int>(result.status)},{"rows",result.records.size()}}.dump()<<'\n';return 0;}
 const auto trace=local.query({{"ha.trace"}},{"ha.trace",argv[3]});if(!trace||trace.records.empty())return 4;auto t=Json::parse(trace.records[0].payload);const auto roots=sharedMemoryRoots(main,current);const auto m=local.query({{"ha.memory"}},{"ha.memory","","",false,65536,16*1024*1024});const auto a=local.query({{"ha.access"}},{"ha.access","current"});const auto hidden=a.records.empty()?Json::array():Json::parse(a.records[0].payload).at("hidden");const auto policy=memoryVisibility(m.records,hidden);
 for(const auto& ref:t.at("selected_sources")){const auto id=ref.at("id").get<std::string>(),source=ref.at("source").get<std::string>();const auto scope=source.substr(0,source.rfind('@'));Json row;bool available=false,limited=false;if(sharedMemoryId(id)){const auto found=querySharedMemory(roots,scope.ends_with("ha.archive"),"",id,1,0,65536);available=!found.rows.empty();limited=found.limited;if(available)row=found.rows[0];}else{const auto found=local.query({{scope}},{scope,id});available=found&&!found.records.empty();if(available)row=Json::parse(found.records[0].payload);}
 Json result={{"id",id},{"available",available},{"limited",limited}};if(available){result["legacy_meta"]=isLegacyMetaReply(row);result["visible"]=policy.source({id,1,"",scope});result["references_allowed"]=!row.contains("sources")||policy.allows(row);result["kind"]=row.value("kind",std::string{});}std::cout<<result.dump()<<'\n';}
}
