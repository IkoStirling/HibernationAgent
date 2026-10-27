#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <string>
namespace hibernation {
std::string contentDigest(std::string_view text);
bool validSkillId(std::string_view id);
bool skillGrantableTool(const std::string& name);
void validateSkillSettings(const nlohmann::json& settings);
// Immutable snapshots. Loading never executes a package or follows a reparse point.
class SkillRegistry {
    struct Package {
        std::string id,name,description,digest,issue;
        std::map<std::string,std::string> documents;
        std::set<std::string> tools;
        bool approved=false;
    };
    bool enabled_=true;
    std::map<std::string,Package> packages_;
public:
    SkillRegistry(const std::filesystem::path& root,const nlohmann::json& settings);
    nlohmann::json catalog(const nlohmann::json& tools,bool management=false) const;
    nlohmann::json read(const std::string& id,const std::string& path="SKILL.md") const;
    bool authorize(const std::string& id,const std::string& digest,std::set<std::string>& tools) const;
};
}
