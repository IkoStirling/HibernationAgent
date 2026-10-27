#pragma once
#include <Hibernation/ReactionCache.h>
namespace hibernation {
std::string activitySchema();
std::string activitySchemaFor(const Json& availableKinds);
ValidatedOutput validateActivities(std::string_view text);
class ActivityGenerator {
public:
    ~ActivityGenerator();
    void start(Profile actor,std::optional<Profile> reviewer,Json persona,Json context,std::string key,std::function<bool()> reserveCall={},std::function<void(const Profile&,int)> observeResult={});
    bool running()const;void cancel();void stop();std::optional<ReactionBatch> poll();
private:std::future<ReactionBatch> future_;std::shared_ptr<std::atomic<bool>> cancelled_;
};
}
