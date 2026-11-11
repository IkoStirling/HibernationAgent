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
    /// Startup only: preserve independent available plugins when a requested
    /// plugin fails. Live settings updates keep configure's atomic behavior.
    bool configureStartup(const PluginJson& settings,PluginJson& failures,std::string& issue);
    void workspaceRoots(std::filesystem::path drafts,std::filesystem::path materials);
    PluginJson catalog()const;
    PluginJson status()const;
    PluginJson invoke(const std::string& tool,const PluginJson& args,const std::function<bool()>& cancelled);
    // Serialized native world/inventory transaction; never a model-visible tool.
    PluginJson commitCollection(const std::string& token);
    PluginJson enqueueChannelReply(const PluginJson& envelope);
    void tick(double elapsedSeconds);
    void event(const PluginJson& value);
    std::vector<PluginJson> takeEvents();
    PluginJson snapshots()const;
    /// Current loaded instances only; archived disabled snapshots are not a body.
    PluginJson liveSnapshots()const;
    PluginJson liveSnapshot(const std::string& id)const;
    bool enabled(const std::string& id)const;
    void restore(const PluginJson& value);
    void stop();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
