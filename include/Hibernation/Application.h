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
};
struct ValidatedOutput {bool ok=false;std::string code;Json value;};
std::string outputSchema();
ValidatedOutput validateOutput(std::string_view text,const ProposalContext& context);
bool safeRelative(std::string_view path);
struct Profile {std::string id,label;bool enabled=true;ayt::inference::ModelProfile model;std::uint32_t maxOutputTokens=2048;};
struct Config {
    std::filesystem::path dataRoot,characterRoot,uiRoot;
    std::filesystem::path configPath,projectRoot,pluginRoot,draftsRoot,materialsRoot;
    Json computer={{"enabled",true},{"allowScreenCapture",false},{"readRoots",Json::array()}};
    Json plugins=Json::object(),remoteStorage={{"kind","none"},{"url",""},{"enabled",false}};
    std::size_t maxToolRounds=6;
    bool reviewEnabled=false;
    std::string reviewProfileId;
    std::size_t maxReviewRewrites=2;
    bool desktopEnabled=false;
    bool pluginRootExplicit=false;
    int port=0;
    std::string credentialEnvironment="HIBERNATION_SESSION_KEY";
    std::size_t contextBytes=24000,recentMessageCount=12,maxMemoriesInContext=16;
    std::int64_t solitudeIntervalMs=900000,idleBeforeSolitudeMs=120000;
    int maxStartsPerHour=60;
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
    void tick();
    void stop();
    bool stopping() const;
    const Config& config() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string readFile(const std::filesystem::path& path,std::size_t maxBytes=1024*1024);
}
