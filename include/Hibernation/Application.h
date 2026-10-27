#pragma once
#include <AYApplication/LocalService.h>
#include <AYApplication/WorkerHost.h>
#include <AYEventSystem/EventBus.h>
#include <AYEventSystem/EventJournal.h>
#include <AYInference/Inference.h>
#include <AYIO/Workspace.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <map>
#include <set>

namespace hibernation {
using Json=nlohmann::json;
struct Source {std::string id;std::int64_t revision=1;std::string kind;std::string scope="ha.archive";};
struct ProposalContext {
    std::string eventKind;
    std::string eventId;
    std::string stage;
    std::string nextStage;
    std::size_t minimumEvidence=2;
    std::map<std::string,Source> sources;
    std::string skillId,skillDigest;
    std::set<std::string> skillTools;
};
struct ValidatedOutput {bool ok=false;std::string code;Json value;};
std::string outputSchema();
ValidatedOutput validateOutput(std::string_view text,const ProposalContext& context);
bool safeRelative(std::string_view path);
struct Profile {std::string id,label;bool enabled=true;ayt::inference::ModelProfile model;std::uint32_t maxOutputTokens=2048;};
// Native adapters construct this immutable policy; messages/config JSON cannot.
struct ChannelPolicy {
    Json pricing=Json::object();
    std::string selectedProfile;
    Json identity=Json::object(),roleState=Json::object(),stage=Json::object();
    std::function<bool(int)> reserveModelCalls;
    Json desktopTools=Json::array(),desktopCapabilities=Json::object(),body=Json::object();
    std::filesystem::path sharedMemoryRoot;
    std::function<Json(const std::string&,const Json&,const ProposalContext&,const std::string&,const std::function<bool()>&)> invokeDesktop;
    std::function<Json()> budgetFailure;
    std::function<void(const std::string&,const std::string&,int)> providerFailure;
};
struct Config {
    std::shared_ptr<const ChannelPolicy> channel;
    std::filesystem::path dataRoot,characterRoot,uiRoot;
    std::filesystem::path configPath,projectRoot,pluginRoot,draftsRoot,materialsRoot;
    std::filesystem::path skillsRoot;
    Json skills={{"enabled",true},{"useBuiltins",true},{"grants",Json::array()}};
    Json computer={{"enabled",true},{"allowScreenCapture",false},{"readRoots",Json::array()}};
    Json plugins=Json::object(),remoteStorage={{"kind","none"},{"url",""},{"enabled",false}};
    Json frontend={{"startupAnimation",true},{"reducedMotion",false}};
    Json contextControl=Json::object();
    std::size_t maxToolRounds=6;
    bool reviewEnabled=false;
    bool retainConversationFocus=false;
    bool reflectOnActivities=false;
    std::string reviewProfileId;
    std::size_t maxReviewRewrites=2;
    bool desktopEnabled=false;
    bool pluginRootExplicit=false;
    int port=0;
    std::string credentialEnvironment="HIBERNATION_SESSION_KEY";
    std::size_t contextBytes=24000,recentMessageCount=12,maxMemoriesInContext=16;
    std::int64_t solitudeIntervalMs=900000,idleBeforeSolitudeMs=120000;
    int maxStartsPerHour=600;
    std::vector<Profile> profiles;
};
Config loadConfig(const std::filesystem::path& file);
/// Owner-thread application composition; model calls execute through WorkerHost.
class Application {
public:
    explicit Application(Config config);
    ~Application();
    bool initialize();
    ayt::app::services::LocalReply request(const ayt::app::services::LocalRequest& request);
    /// Owner-only user file-picker import. No HTTP or model path authority.
    ayt::app::services::LocalReply importChatMedia(const std::filesystem::path& userSelectedFile);
    void tick();
    /// Owner-only transient widget UI requests, never model work or archive data.
    Json takeDesktopRequests();
    void setInputActive(bool active);
    void stop();
    bool stopping() const;
    const Config& config() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string readFile(const std::filesystem::path& path,std::size_t maxBytes=1024*1024);
}
