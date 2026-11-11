#pragma once
#include <Hibernation/CognitiveLoop.h>
#include <Hibernation/Application.h>
namespace hibernation::cognitive {
struct ChoiceResult {bool approved=false;Json proposal=nullptr;Json audit=Json::array();std::string issue;};
// Uses engine inference only. Non-mock routes must go through the bounded local lab relay.
class ModelSession {
public:
    explicit ModelSession(const Json& connection);
    ChoiceResult decide(const Loop& loop)const;
private:
    Profile actor_,reviewer_;
    Json persona_;
};
}
