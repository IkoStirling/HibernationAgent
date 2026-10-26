#pragma once
#include <Hibernation/ReactionCache.h>
namespace hibernation {
std::string activitySchema();
ValidatedOutput validateActivities(std::string_view text);
class ActivityGenerator {
public:
    ~ActivityGenerator();
    void start(Profile actor,std::optional<Profile> reviewer,Json persona,Json context,std::string key);
    bool running()const;void cancel();void stop();std::optional<ReactionBatch> poll();
private:std::future<ReactionBatch> future_;std::shared_ptr<std::atomic<bool>> cancelled_;
};
}
