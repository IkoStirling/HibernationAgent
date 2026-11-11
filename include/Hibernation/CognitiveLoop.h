#pragma once
#include <Hibernation/RoleState.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hibernation::cognitive {
using Json = nlohmann::json;

// Experimental application policy, not a second production event bus.
struct Settings {
    bool attention = true;
    std::int64_t dwellMs = 2000;
    double switchMargin = .15;
    bool automaticPlanning = true;
};

class Loop {
public:
    explicit Loop(Settings settings = {});
    // All events are synthetic. Invalid events leave the state unchanged.
    Json dispatch(const Json& event);
    Json snapshot() const;
    Json view() const;
    // restart=true discards unconfirmed execution, never replays it.
    void restore(const Json& snapshot, bool restart = true);
    bool validateProposal(const Json& proposal, std::string& issue) const;
    Json decisionContext() const;
    bool needsDecision() const;
    bool validateChoice(const Json& proposal, std::string& issue) const;
    static std::string choiceSchema();
private:
    struct Object {
        std::string id;
        std::int64_t revision = 0, expiresAt = 0;
        double x = 0, salience = 0;
    };
    struct Goal {
        std::string id, kind, target;
        std::int64_t targetRevision = 0, startedAt = 0;
        std::int64_t durationMs = 0;
    };
    struct Operation {
        std::string token, kind, expected;
        std::int64_t startedAt = 0, deadline = 0;
        bool accepted = false;
    };
    struct Candidate {
        std::string id, kind, target;
        std::int64_t revision = 0;
        double priority = 0;
    };
    Settings settings_;
    RoleState body_;
    std::int64_t now_ = 1000, revision_ = 0, serial_ = 0, focusAt_ = 0, restAfter_ = 0;
    double hunger_ = .8, x_ = .1;
    bool paused_ = false;
    bool decisionNeeded_ = false;
    Json dialogueHistory_ = Json::array();
    std::string focus_, mode_ = "normal", decisionKey_, sourceEvent_;
    std::map<std::string, Object> objects_;
    std::map<std::string, std::int64_t> lastRevision_;
    std::set<std::string> blocked_;
    std::map<std::string, std::string> seen_;
    std::optional<Goal> goal_, suspended_;
    std::optional<Operation> operation_;
    Json dialogue_ = nullptr, metrics_, trace_ = Json::array(), emitted_ = Json::array();

    std::vector<Candidate> candidates() const;
    void settle();
    void startOperation(std::string kind);
    void stopGoal(std::string outcome, std::string reason, bool block);
    void advance(std::int64_t ms, bool automatic);
    bool receipt(const Json& event);
    void note(std::string kind, Json data = Json::object());
    void count(const std::string& name);
    std::string key(const std::string& id, std::int64_t revision) const;
    Json request() const;
    Json mock(const Json& request) const;
    Json goalJson(const Goal& goal) const;
    void load(const Json& snapshot);
    void apply(const Json& event);
    static void checkEvent(const Json& event);
    void applyChoice(const Json& proposal);
};
}
