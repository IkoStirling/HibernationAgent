#include <Hibernation/SkillRegistry.h>
#include <Hibernation/Application.h>
#include <AYTestFixtures.h>
#include <fstream>
using namespace hibernation;
namespace {
void put(const std::filesystem::path& path,const std::string& text){std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<text;}
Json toolList(){return Json::array({{{"name","workspace.read"}},{{"name","workspace.write"}}});}
}
TEST_SUITE(SkillRegistry)
TEST_CASE(approval_is_external_content_bound_and_never_executes_package_scripts){
 ayt::test::ScratchDirectory scratch("skill-registry");const auto root=scratch.path();
 put(root/"notes"/"SKILL.md","---\nname: Notes\ndescription: Test procedure\nallowed-tools: everything\n---\n# Procedure\n");put(root/"notes"/"scripts"/"run.ps1","throw 'must never execute'\n");
 Json settings={{"useBuiltins",false}};SkillRegistry denied(root,settings);CHECK(denied.catalog(toolList()).empty());CHECK(!denied.read("notes").at("ok").get<bool>());
 const auto row=denied.catalog(toolList(),true).at(0);CHECK(row.at("digest").get<std::string>().size()==64);CHECK(row.at("name")=="Notes");
 settings["grants"]=Json::array({{{"id","notes"},{"digest",row.at("digest")},{"tools",Json::array({"workspace.read","not.installed"})}}});
 SkillRegistry allowed(root,settings);CHECK(allowed.catalog(toolList()).at(0).at("tools")==Json::array({"workspace.read"}));CHECK(allowed.read("notes").at("ok").get<bool>());CHECK(!allowed.read("notes","scripts/run.ps1").at("ok").get<bool>());CHECK(!allowed.read("notes","../SKILL.md").at("ok").get<bool>());
 put(root/"notes"/"SKILL.md","# Changed procedure\n");CHECK(allowed.read("notes").at("result").at("text").get<std::string>().find("Test procedure")!=std::string::npos);
 SkillRegistry changed(root,settings);CHECK(changed.catalog(toolList()).empty());CHECK(changed.catalog(toolList(),true).at(0).at("issue")=="content_changed");
}
TEST_CASE(disabled_packages_bad_grants_and_unbounded_package_content_fail_closed){
 ayt::test::ScratchDirectory scratch("skill-bounds");put(scratch.path()/"oversize"/"SKILL.md",std::string(65537,'a'));
 SkillRegistry registry(scratch.path(),Json::object());CHECK(registry.catalog(toolList()).empty());CHECK(registry.catalog(toolList(),true).at(0).at("issue")=="invalid_or_unsafe_package");
 for(const auto& tools:{Json::array({"*"}),Json::array({"skills.activate"}),Json::array({"mentor.execute"}),Json::array({"schedule.create"}),Json::array({"activity.enqueue"}),Json::array({"workspace.read","workspace.read"})}){
  bool rejected=false;try{validateSkillSettings({{"grants",Json::array({{{"id","notes"},{"digest",std::string(64,'a')},{"tools",tools}}})}});}catch(...){rejected=true;}CHECK(rejected);
 }
 CHECK(!validSkillId("../notes"));CHECK(!validSkillId("C:\\notes"));CHECK(!validSkillId("-notes"));
}
TEST_CASE(shipped_skills_match_the_compiled_content_and_permission_pins){
 SkillRegistry registry(std::filesystem::path(HA_PROJECT_ROOT)/"skills",Json::object());const auto catalog=registry.catalog(toolList());CHECK(catalog.size()==2);
 for(const auto& row:catalog){std::set<std::string> tools;CHECK(registry.authorize(row.at("id"),row.at("digest"),tools));CHECK(!tools.contains("mentor.execute"));}
 Json settings={{"grants",Json::array({{{"id","workspace-helper"},{"digest",catalog.at(1).at("digest")},{"tools",Json::array()},{"enabled",false}}})}};
 SkillRegistry revoked(std::filesystem::path(HA_PROJECT_ROOT)/"skills",settings);CHECK(revoked.catalog(toolList()).size()==1);
}
TEST_SUITE_END
