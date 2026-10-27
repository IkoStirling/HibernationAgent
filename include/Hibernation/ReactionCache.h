#pragma once
#include <Hibernation/Application.h>
#include <future>
#include <atomic>
#include <optional>
#include <functional>
namespace hibernation {
std::string reactionSchema();
ValidatedOutput validateReactions(std::string_view text);
struct ReactionBatch {std::string key,status;Json entries=Json::array(),proposal,review,usage=Json::object();double modelMs=0,reviewMs=0;};
class ReactionGenerator {
public:
    ~ReactionGenerator();
    void start(Profile actor,std::optional<Profile> reviewer,Json persona,Json state,Json excluded,std::string key,std::function<bool()> reserveCall={},std::function<void(const Profile&,int)> observeResult={});
    bool running()const;void cancel();void stop();std::optional<ReactionBatch> poll();
private:std::future<ReactionBatch> future_;std::shared_ptr<std::atomic<bool>> cancelled_;
};
/// Returns and removes one ready, unexpired candidate. Archives are maintained by host.
Json takeReaction(Json& pool,std::int64_t now);
}
