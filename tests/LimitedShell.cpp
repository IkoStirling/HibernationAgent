#include <Hibernation/LimitedShell.h>
#include <Hibernation/DesktopServices.h>
#include <Hibernation/Review.h>
#include <Hibernation/OperationReplay.h>
#include <AYTestFixtures.h>
#include <fstream>
#include <chrono>
using namespace hibernation;
TEST_SUITE(LimitedShell)
TEST_CASE(commands_are_data_not_operating_system_code){
    CHECK(limited_shell::parse("list \"materials:/with space\"").path=="materials:/with space");
    CHECK(limited_shell::parse("DIR").name=="list");CHECK(limited_shell::parse("type a.txt").name=="cat");
    for(const auto* s:{"powershell","cmd /c","rm a","list; pwd","list | cat","list > out","cat $env:HOME","cat %HOME%","cd a b","cat","pwd extra","cd \"oops","list\ncd"})CHECK(!limited_shell::parse(s).ok);
    CHECK(limited_shell::parse("cat \"a;b.txt\"").ok);CHECK(replayProtectedOperation("shell.run"));
}
TEST_CASE(paths_cannot_escape_or_merge_conversation_directories){
    const Json roots=Json::array({{{"id","drafts"},{"path","C:/Mount/Drafts"}},{{"id","materials"},{"path","C:/Mount/Materials"}}});
    const Json cwd={{"root","drafts"},{"path","sub"}};
    CHECK(limited_shell::resolve("..",cwd,roots).path.empty());CHECK(!limited_shell::resolve("../..",cwd,roots).ok);
    CHECK(!limited_shell::resolve("//server/share",cwd,roots).ok);CHECK(!limited_shell::resolve("C:/secrets/key",cwd,roots).ok);
    CHECK(!limited_shell::resolve("materials:/../../x",cwd,roots).ok);CHECK(!limited_shell::resolve("materials:/a:stream",cwd,roots).ok);
    CHECK(limited_shell::resolve("c:/mount/materials/a.txt",cwd,roots).path=="a.txt");
    CHECK(limited_shell::resolve("materials:/",cwd,roots).root=="materials");CHECK(limited_shell::resolve("/",cwd,roots).root.empty());
    Json a={{"kind","qq"},{"identity",{{"sender_user_id","1"},{"group_id","2"},{"message_id","3"}}}},b=a;
    b["identity"]["message_id"]="4";CHECK(limited_shell::sessionRoute(a)==limited_shell::sessionRoute(b));b["identity"]["group_id"]="5";CHECK(limited_shell::sessionRoute(a)!=limited_shell::sessionRoute(b));
}
TEST_CASE(broker_enforces_read_limits_and_canonical_mounts){
    const auto root=std::filesystem::temp_directory_path()/("ha-shell-unit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root/"drafts");std::filesystem::create_directories(root/"materials");
    std::ofstream(root/"drafts/ok.txt")<<"bounded";std::ofstream(root/"drafts/large.txt")<<std::string(32769,'x');std::ofstream(root/"drafts/binary.bin",std::ios::binary)<<std::string("a\0b",3);
    Config c;c.dataRoot=root;c.projectRoot=root/"project";c.characterRoot=root/"character";c.uiRoot=root/"ui";c.computer={{"enabled",true},{"limitedShellEnabled",true}};std::string error;
    DesktopServices service(c);CHECK(service.configure(error));
    auto read=[&](const char* path){return service.invoke("computer.shell_read",{{"root","drafts"},{"path",path}},{});};
    CHECK(read("ok.txt").at("result").at("text")=="bounded");CHECK(!read("../outside.txt").at("ok").get<bool>());CHECK(!read("large.txt").at("ok").get<bool>());CHECK(!read("binary.bin").at("ok").get<bool>());
    c.computer["limitedShellEnabled"]=false;CHECK(!read("ok.txt").at("ok").get<bool>());
    std::filesystem::remove_all(root);
}
TEST_CASE(persona_rejections_remain_strict_and_transport_retries_stay_selective){
    for(const auto* reason:{"persona","meta_leak","continuity","unknown"})CHECK(!advisoryRoleReview({{"decision","deny"},{"reason",reason}},true));
    for(const auto* reason:{"other","tool_intent","unsupported_claim"}){CHECK(advisoryRoleReview({{"decision","rewrite"},{"reason",reason}},true));CHECK(!advisoryRoleReview({{"decision","rewrite"},{"reason",reason}},false));}
    CHECK(retryableReviewFailure(static_cast<int>(ayt::inference::Status::Timeout),0));CHECK(retryableReviewFailure(0,503));CHECK(retryableReviewFailure(0,429));CHECK(!retryableReviewFailure(0,402));CHECK(!retryableReviewFailure(0,401));
}
TEST_SUITE_END
