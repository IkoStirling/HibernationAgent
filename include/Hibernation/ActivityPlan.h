#pragma once
#include <Hibernation/ReactionCache.h>
namespace hibernation {
std::string activitySchema();
std::string activitySchemaFor(const Json& availableKinds);
ValidatedOutput validateActivities(std::string_view text);
struct ActivityBatch : ReactionBatch {
    Json carryForward=Json::array(),closeThreads=Json::array(),evidence=Json::array(),narrativeSources=Json::array(),appraisal=nullptr;
    std::chrono::steady_clock::time_point createdAt=std::chrono::steady_clock::now();
};
class ActivityGenerator {
public:
    ~ActivityGenerator();
    void start(Profile actor,std::optional<Profile> reviewer,Json persona,Json context,std::string key,std::function<bool()> reserveCall={},std::function<void(const Profile&,int)> observeResult={});
    bool running()const;void cancel();void stop();std::optional<ActivityBatch> poll();
private:std::future<ActivityBatch> future_;std::shared_ptr<std::atomic<bool>> cancelled_;
};
}
