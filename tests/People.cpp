#include <Hibernation/People.h>
#include <Hibernation/Vault.h>
#include <Hibernation/SharedMemory.h>
#include <AYTestFixtures.h>
#include <fstream>
using namespace hibernation;
TEST_SUITE(PeopleArchive)
TEST_CASE(identity_uses_account_and_audience_not_nickname_or_message){
 Json who={{"self_user_id","1"},{"sender_user_id","2"},{"message_type","private"},{"message_id","3"}};auto id=people::subject(who,false);who["message_id"]="4";who["nickname"]="different";CHECK(people::subject(who,false)==id);who["group_id"]="5";who["message_type"]="group";CHECK(people::subject(who,false)!=id);CHECK(people::subject(who,true)=="owner");CHECK(people::subject(Json::object(),false).empty());
}
TEST_CASE(versions_expiry_privacy_and_encryption_survive_reopen){
 ayt::test::ScratchDirectory scratch("people");const auto root=scratch.path();std::filesystem::create_directories(root/"runtime");
 Json a={{"id","animation"},{"expected_revision",0},{"kind","impression"},{"status","active"},{"topic","动画"},{"text","UNIQUE_PRIVATE_PROFILE_NOTE"},{"quote","动画"},{"source_id","u1"},{"valid_for_days",0}},source={{"id","u1"},{"scope","ha.archive"},{"revision",1},{"created_ms",10}};
 auto first=people::note(root,"owner",a,source,"test",100);CHECK(first.at("ok").get<bool>());CHECK(first.at("result").at("expires_ms")==100+30LL*86400000);
 auto yes=[](const Json&){return true;};auto view=people::read(root,"owner",{{"query","动画"}},yes,101,true);CHECK(view.at("entries").size()==1);CHECK(view.at("entries")[0].at("certainty")=="tentative");CHECK(people::read(root,"other",{},yes,101).at("entries").empty());CHECK(people::read(root,"owner",{},[](const Json&){return false;},101).at("entries").empty());
 CHECK(people::read(root,"owner",{},yes,100+31LL*86400000).at("entries").empty());CHECK(people::read(root,"owner",{{"query","unrelated"}},yes,101,true).at("entries").empty());
 auto conflict=a;conflict["text"]="changed";CHECK(!people::note(root,"owner",conflict,source,"conflict",200).at("ok").get<bool>());
 auto revoke=a;revoke["expected_revision"]=1;revoke["status"]="revoked";source["created_ms"]=20;CHECK(people::note(root,"owner",revoke,source,"revoke",200).at("ok").get<bool>());CHECK(people::read(root,"owner",{},yes,201).at("entries").empty());CHECK(people::read(root,"owner",{{"id","animation"},{"history",true}},yes,201).at("entries").size()==2);
 std::ifstream in(root/"people/owner/records.sqlite",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});CHECK(bytes.find("UNIQUE_PRIVATE_PROFILE_NOTE")==bytes.npos);
 std::filesystem::remove(root/"runtime/memory-key.dpapi");CHECK(people::read(root,"owner",{},yes,201).at("unavailable").get<bool>());CHECK(!std::filesystem::exists(root/"runtime/memory-key.dpapi"));
}
TEST_CASE(native_provenance_can_verify_private_observations_but_public_browsing_cannot){
 ayt::test::ScratchDirectory scratch("native-provenance");const auto root=scratch.path();std::filesystem::create_directories(root/"runtime");std::filesystem::create_directories(root/"archive");ayt::storage::RecordStore store;CHECK(store.open(root/"archive/records.sqlite",{openVaultCodec(root),false}));
 const Json observation={{"kind","tool_observation"},{"text","PRIVATE_OBSERVATION_FOR_SOURCE_CHECK"},{"sources",Json::array()}};CHECK(static_cast<bool>(store.commit({{"ha.archive","private-observation",0,1,observation.dump(),"fixture"}},"test",1)));
 const auto tag=memoryRootTag(root);const std::vector<SharedMemoryRoot> roots={{root,tag,true,true}};
 CHECK(querySharedMemory(roots,true,"",tag+"private-observation").rows.empty());
 CHECK(querySharedMemory(roots,true,"",tag+"private-observation",1,0,1,{},0,nullptr,true).rows.size()==1);
 const Json derived={{"kind","conversation"},{"text","derived"},{"sources",Json::array({{{"id","private-observation"},{"revision",1},{"scope","ha.archive"},{"kind","tool_observation"}}})}};
 CHECK(static_cast<bool>(store.commit({{"ha.memory","blocked",0,1,derived.dump(),"fixture"},{"ha.access","current",0,1,Json{{"hidden",Json::array({"blocked"})}}.dump(),"operator"}},"hide",2)));
 CHECK(querySharedMemory(roots,true,"",tag+"private-observation",1,0,1,{},0,nullptr,true).rows.empty());
}
TEST_SUITE_END
