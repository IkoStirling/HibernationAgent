#include <Hibernation/OwnerContinuity.h>
#include <Hibernation/Review.h>
#include <Hibernation/MemoryContext.h>
#include <Hibernation/CacheContext.h>
#include <Hibernation/SkillRegistry.h>
#include <AYTestFixtures.h>
using namespace hibernation;
TEST_SUITE(OwnerContinuity)
TEST_CASE(exact_note_schema_is_typed_versioned_and_not_a_skill_grant){
 Json note={{"id","address"},{"expected_revision",0},{"kind","preference"},{"status","active"},{"quote","以后叫我松果。"},{"meaning","称呼为松果。"},{"source_id","actual-user"}};
 CHECK(ayt::serializer::validateJsonDocument(note.dump(),continuity::noteParameters().dump()));note["expected_revision"]=-1;CHECK(!ayt::serializer::validateJsonDocument(note.dump(),continuity::noteParameters().dump()));note["expected_revision"]=0;note["grant_computer_access"]=true;CHECK(!ayt::serializer::validateJsonDocument(note.dump(),continuity::noteParameters().dump()));
 CHECK(!skillGrantableTool("continuity.note"));CHECK(!skillGrantableTool("continuity.list"));
}
TEST_CASE(reviewer_can_optionally_preserve_exact_agreements_without_breaking_old_verdicts){
 Json verdict={{"decision","approve"},{"reason","ok"},{"guidance",""}};CHECK(validateReview(verdict.dump(),false,true).ok);verdict["continuity_updates"]=Json::array();CHECK(validateReview(verdict.dump(),false,true).ok);verdict["focus_update"]=nullptr;CHECK(validateReview(verdict.dump(),true,true).ok);verdict.erase("focus_update");CHECK(!validateReview(verdict.dump(),false,false).ok);
}
TEST_CASE(review_keeps_source_packet_tail_under_a_small_history_budget_and_cache_order){
 const auto longText=std::string(5000,'x')+"精确口令：琥珀-27";
 Json event={{"event_kind","user_message"},{"event_id","current"},{"input","换个话题"},{"unprocessed_user_context",Json::array({{{"source_id","long"}}})},{"authorized_context",Json::array({{{"id","long"},{"data",{{"kind","user_message"},{"text",longText}}}},{{"id","old"},{"data",{{"kind","assistant_message"},{"text",std::string(3000,'y')}}}}})}};
 const auto reduced=continuity::reviewContext(event.dump(),{{"kind","reply"},{"text","你好"}},1024);CHECK(std::any_of(reduced.at("authorized_context").begin(),reduced.at("authorized_context").end(),[&](const auto&r){return r.at("id")=="long"&&r.at("data").at("text")==longText;}));
 const auto wire=cacheOrderedReview({{"event_context",reduced.dump()},{"authoritative_state",{{"tick",9}}},{"candidate",{{"text","你好"}}}});CHECK(wire.find("authorized_context")<wire.find("authoritative_state"));CHECK(Json::parse(Json::parse(wire).at("event_context").get<std::string>()).at("authorized_context")==reduced.at("authorized_context"));
}
TEST_CASE(owner_dialogue_reads_trusted_indices_instead_of_losing_old_long_originals){
 ayt::test::ScratchDirectory scratch("owner-raw-packets");const auto root=scratch.path();std::filesystem::create_directories(root/"archive");ayt::storage::RecordStore store;CHECK(store.open(root/"archive/records.sqlite",{openVaultCodec(root),false,false}));
 const auto raw=Json{{"kind","user_message"},{"text",std::string(2100,'x')+"口令尾部"},{"created_at",1}};
 CHECK(store.commit({{"ha.archive","long",0,1,raw.dump(),"fixture"},{"ha.index","recent",0,1,Json::array({"new"}).dump(),"fixture"},{"ha.continuity-packets","index",0,1,Json::array({"long"}).dump(),"fixture"},{"ha.archive","new",0,1,Json{{"kind","user_message"},{"text","新话题"},{"created_at",2}}.dump(),"fixture"}},"fixture",1));
 const std::vector<SharedMemoryRoot> roots={{root,memoryRootTag(root),true,true}};const auto found=continuity::dialogue(roots,root,true);CHECK(found.size()==1);CHECK(found[0].id=="long");CHECK(found[0].data.at("text")==raw.at("text"));CHECK(found[0].scope=="ha.archive");
}
TEST_CASE(hidden_memory_propagates_to_qualified_cross_channel_sources){
 const auto row=ayt::storage::VersionedRecord{"ha.memory","derived",1,1,Json{{"sources",Json::array({{{"scope","shared-0123456789abcdef:ha.archive"},{"id","shared-0123456789abcdef:original"}}})}}.dump(),"fixture"};
 const auto policy=memoryVisibility({row},Json::array({"derived"}));CHECK(policy.archives.contains("shared-0123456789abcdef:original"));CHECK(!policy.source({"shared-0123456789abcdef:original",1,"user_message","shared-0123456789abcdef:ha.archive"}));
}
TEST_SUITE_END
