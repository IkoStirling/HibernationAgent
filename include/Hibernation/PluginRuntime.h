#pragma once
#include <Hibernation/RolePlugin.h>
#include <memory>
#include <vector>
namespace hibernation {
class PluginRuntime {
public:
    PluginRuntime(std::filesystem::path root,PluginHostContext host);
    ~PluginRuntime();
    bool configure(const PluginJson& settings,std::string& issue);
    void workspaceRoots(std::filesystem::path drafts,std::filesystem::path materials);
    PluginJson catalog()const;
    PluginJson status()const;
    PluginJson invoke(const std::string& tool,const PluginJson& args,const std::function<bool()>& cancelled);
    void tick(double elapsedSeconds);
    void event(const PluginJson& value);
    std::vector<PluginJson> takeEvents();
    PluginJson snapshots()const;
    void restore(const PluginJson& value);
    void stop();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
