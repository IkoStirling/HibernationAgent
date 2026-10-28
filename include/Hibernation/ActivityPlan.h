#pragma once
#include <Hibernation/ReactionCache.h>
namespace hibernation {
std::string activitySchema();
std::string activitySchemaFor(const Json& availableKinds);
ValidatedOutput validateActivities(std::string_view text,Json* formatNormalizations=nullptr);
struct ActivityBatch : ReactionBatch {
    Json promptHistory=Json::object();
    Json carryForward=Json::array(),closeThreads=Json::array(),evidence=Json::array(),narrativeSources=Json::array(),appraisal=nullptr,formatNormalizations=Json::array();
    std::chrono::steady_clock::time_point createdAt=std::chrono::steady_clock::now();
};
class ActivityGenerator {
public:
    ~ActivityGenerator();
    // An observeUsage callback owns immediate budget and lifetime usage recording.
    // Returned batches mark that ownership so host fallback accounting does not repeat it.
    void start(Profile actor,std::optional<Profile> reviewer,Json persona,Json context,std::string key,std::function<bool()> reserveCall={},std::function<void(const Profile&,int)> observeResult={},std::function<void(const Profile&,const std::string&,const ayt::inference::Usage&)> observeUsage={},Json promptHistory=Json::object(),std::function<bool()> stillAuthorized={});
    bool running()const;void cancel();void stop();std::optional<ActivityBatch> poll();
private:std::future<ActivityBatch> future_;std::shared_ptr<std::atomic<bool>> cancelled_;
};
}
