#include <Hibernation/SkillRegistry.h>
#include <Hibernation/WorkspaceText.h>
#include <AYCrypto.h>
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <cctype>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace hibernation {
using Json=nlohmann::json;
std::string contentDigest(std::string_view text){
    auto hash=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);
    if(!hash)throw std::runtime_error("digest_unavailable");
    auto value=hash->hashString(reinterpret_cast<const std::uint8_t*>(text.data()),text.size());
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;
}
bool validSkillId(std::string_view id){return !id.empty()&&id.size()<=48&&id.front()!='-'&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';});}
bool skillGrantableTool(const std::string& name){return std::regex_match(name,std::regex("[A-Za-z][A-Za-z0-9_.-]{0,95}"))&&!name.starts_with("continuity.")&&!name.starts_with("skills.")&&name!="mentor.execute"&&name!="activity.enqueue"&&(!name.starts_with("schedule.")||name=="schedule.list")&&(!name.starts_with("task.")||name=="task.status");}
void validateSkillSettings(const Json& settings){
    if(!settings.is_object()||settings.dump().size()>16384)throw std::runtime_error("skills_config");
    for(const auto& [key,value]:settings.items())if(key!="enabled"&&key!="root"&&key!="useBuiltins"&&key!="grants")throw std::runtime_error("skills_field");
    for(const auto* field:{"enabled","useBuiltins"})if(settings.contains(field)&&!settings.at(field).is_boolean())throw std::runtime_error("skills_boolean");
    if(settings.contains("root")){if(!settings.at("root").is_string())throw std::runtime_error("skills_root");const auto value=settings.at("root").get<std::string>();if(value.size()>2048||value.find_first_of("\r\n")!=std::string::npos||value.find('\0')!=std::string::npos)throw std::runtime_error("skills_root");}
    const auto grants=settings.value("grants",Json::array());if(!grants.is_array()||grants.size()>32)throw std::runtime_error("skills_grants");
    std::set<std::string> ids;
    for(const auto& grant:grants){
        if(!grant.is_object()||grant.size()<3||grant.size()>4||!grant.contains("id")||!grant.contains("digest")||!grant.contains("tools")||!grant.at("id").is_string()||!validSkillId(grant.at("id").get<std::string>())||!ids.insert(grant.at("id")).second||!grant.at("digest").is_string()||!std::regex_match(grant.at("digest").get<std::string>(),std::regex("[a-f0-9]{64}"))||!grant.at("tools").is_array()||grant.at("tools").size()>32)throw std::runtime_error("skills_grant");
        for(const auto& [key,value]:grant.items())if(key!="id"&&key!="digest"&&key!="tools"&&(key!="enabled"||!value.is_boolean()))throw std::runtime_error("skills_grant_field");
        std::set<std::string> names;for(const auto& tool:grant.at("tools"))if(!tool.is_string()||!skillGrantableTool(tool.get<std::string>())||!names.insert(tool).second)throw std::runtime_error("skills_tool");
    }
}
static bool reparse(const std::filesystem::path& path){
    for(auto part=path;!part.empty();){
#ifdef _WIN32
        const auto attrs=GetFileAttributesW(part.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return true;
#else
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(part)))return true;
#endif
        const auto parent=part.parent_path();if(parent==part)break;part=parent;
    }return false;
}
static Json builtinTrust(){return Json::array({
    {{"id","workspace-helper"},{"digest","22d02853e0090da966c473632b03de706744cbcb5a10f8c5febed727dc5df6c5"},{"tools",Json::array({"workspace.list","workspace.read","workspace.search","workspace.write","workspace.edit"})}},
    {{"id","desktop-observer"},{"digest","a2976f91e4c858d6ebde3558dd6d8492c210e8259d8e1324ff45cb33dc626fbb"},{"tools",Json::array({"computer.screenshot","computer.list_directory","body.observe","widget.status"})}}
});}
static void metadata(const std::string& guide,std::string& name,std::string& description){
    // Only display metadata is recognized. All other front matter is inert text.
    std::istringstream lines(guide);std::string line;bool front=false;std::size_t count=0;
    while(std::getline(lines,line)&&++count<48){if(!line.empty()&&line.back()=='\r')line.pop_back();if(count==1&&line=="---"){front=true;continue;}if(front&&line=="---")break;
        if(front){for(const auto* key:{"name:","description:"})if(line.starts_with(key)){auto value=line.substr(std::char_traits<char>::length(key));const auto start=value.find_first_not_of(" \t");if(start==std::string::npos)continue;value.erase(0,start);if(value.size()>=2&&((value.front()=='"'&&value.back()=='"')||(value.front()=='\''&&value.back()=='\'')))value=value.substr(1,value.size()-2);if(value=="|"||value==">")continue;if(key==std::string_view("name:"))name=value;else description=value;}}
        else if(line.starts_with("# ")){name=line.substr(2);break;}
    }auto limit=[](std::string& text,std::size_t maximum){if(text.size()<=maximum)return;while(maximum&&((static_cast<unsigned char>(text[maximum])&0xc0)==0x80))--maximum;text.resize(maximum);};limit(name,120);limit(description,512);
}
SkillRegistry::SkillRegistry(const std::filesystem::path& root,const Json& settings){
    validateSkillSettings(settings);enabled_=settings.value("enabled",true);if(!enabled_||!std::filesystem::exists(root))return;
    if(reparse(root)||!std::filesystem::is_directory(root))throw std::runtime_error("skills_root_denied");
    auto grants=settings.value("grants",Json::array());if(settings.value("useBuiltins",true))for(const auto& grant:builtinTrust())if(std::none_of(grants.begin(),grants.end(),[&](const auto& g){return g.at("id")==grant.at("id");}))grants.push_back(grant);
    std::vector<std::filesystem::path> dirs;std::size_t visited=0;
    for(const auto& entry:std::filesystem::directory_iterator(root)){if(++visited>128)throw std::runtime_error("skills_catalog_limit");if(entry.is_directory()&&!reparse(entry.path()))dirs.push_back(entry.path());}
    if(dirs.size()>32)throw std::runtime_error("skills_catalog_limit");std::sort(dirs.begin(),dirs.end());
    for(const auto& dir:dirs){
        const auto bytes=dir.filename().u8string();const std::string id(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!validSkillId(id))continue;
        Package package;package.id=id;package.name=id;
        try{
            std::map<std::string,std::string> hashes;std::size_t total=0,entries=0;
            for(const auto& entry:std::filesystem::recursive_directory_iterator(dir)){
                if(++entries>96||reparse(entry.path()))throw std::runtime_error("skill_path_denied");if(entry.is_directory())continue;if(!entry.is_regular_file()||entry.file_size()>65536||hashes.size()>=64)throw std::runtime_error("skill_file_limit");
                const auto relative=entry.path().lexically_relative(dir).generic_u8string();const std::string path(reinterpret_cast<const char*>(relative.data()),relative.size());
                if(path.size()>240)throw std::runtime_error("skill_path_limit");std::ifstream input(entry.path(),std::ios::binary);std::string text((std::istreambuf_iterator<char>(input)),{});if(!input||text.size()>65536||(total+=text.size())>262144)throw std::runtime_error("skill_bytes_limit");
                hashes[path]=contentDigest(text);const auto ext=entry.path().extension().string();
                if((ext==".md"||ext==".txt"||ext==".json"||ext==".csv")&&utf8WorkspaceText(text))package.documents[path]=std::move(text);
            }
            if(!package.documents.contains("SKILL.md")||package.documents.at("SKILL.md").empty()||package.documents.at("SKILL.md").size()>24576)throw std::runtime_error("skill_guide_missing_or_large");
            Json fingerprint=Json::array();for(const auto& [path,hash]:hashes)fingerprint.push_back(Json::array({path,hash}));package.digest=contentDigest("hibernation-skill-v1\n"+fingerprint.dump());
            metadata(package.documents.at("SKILL.md"),package.name,package.description);package.issue="approval_required";
            for(const auto& grant:grants)if(grant.at("id")==id){if(!grant.value("enabled",true)){package.issue="disabled_by_owner";break;}if(grant.at("digest")!=package.digest){package.issue="content_changed";break;}package.approved=true;package.issue.clear();for(const auto& tool:grant.at("tools"))package.tools.insert(tool.get<std::string>());break;}
        }catch(const std::exception&){package.documents.clear();package.issue="invalid_or_unsafe_package";package.approved=false;}
        packages_.emplace(id,std::move(package));
    }
}
Json SkillRegistry::catalog(const Json& tools,bool management)const{
    std::set<std::string> available;for(const auto& tool:tools)available.insert(tool.at("name").get<std::string>());Json values=Json::array();
    for(const auto& [id,p]:packages_){if(!management&&!p.approved)continue;Json effective=Json::array(),resources=Json::array();for(const auto& tool:p.tools)if(available.contains(tool))effective.push_back(tool);for(const auto& [path,unused]:p.documents)resources.push_back(path);
        Json item={{"id",id},{"name",p.name},{"description",p.description},{"digest",p.digest},{"tools",effective},{"resources",resources},{"scripts_executable",false}};
        if(management){item["approved"]=p.approved;item["issue"]=p.issue;item["granted_tools"]=p.tools;}values.push_back(std::move(item));
    }return values;
}
bool SkillRegistry::authorize(const std::string& id,const std::string& digest,std::set<std::string>& tools)const{
    const auto p=packages_.find(id);if(!enabled_||p==packages_.end()||!p->second.approved||p->second.digest!=digest)return false;tools=p->second.tools;return true;
}
Json SkillRegistry::read(const std::string& id,const std::string& path)const{
    const auto p=packages_.find(id);if(!enabled_||p==packages_.end()||!p->second.approved)return {{"ok",false},{"error",{{"code","skill_not_approved"},{"retryable",false}}}};
    const auto doc=p->second.documents.find(path);if(doc==p->second.documents.end())return {{"ok",false},{"error",{{"code","skill_resource_unavailable"},{"retryable",false}}}};
    return {{"ok",true},{"result",{{"id",id},{"digest",p->second.digest},{"path",path},{"text",doc->second},{"instruction_authority","procedure_only"},{"scripts_executable",false}}}};
}
}
