#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hibernation {
struct EmotionEnvironment {
    double energy=.72,openness=.55,attention=.2,socialLoad=0;
    bool resting=false;
};
// A computational affect layer, not a claim about consciousness or a scientific
// taxonomy. RoleState serializes access; standalone callers must do the same.
class EmotionState {
public:
    using Json=nlohmann::json;
    void restore(const Json& saved);
    Json snapshot()const;
    Json context()const;
    void tick(double seconds,const EmotionEnvironment& environment);
    bool stimulate(const Json& proposal,const std::string& sourceId,std::int64_t now,std::string& issue);
    double moodOffset()const;
    double willingnessOffset()const;
private:
    struct Source {std::string id;double clock=0;};
    std::array<double,7> emotions_{},emotionDrive_{};
    std::array<double,6> desires_{.3,.35,.25,.2,.25,.12},desireDrive_{};
    std::vector<Source> sources_;
    double clock_=0,lastStimulus_=-1;
};
}
