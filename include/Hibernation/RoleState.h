#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
namespace hibernation {
/// Continuous bounded values and discrete activities have independent clocks.
/// One snapshot is authoritative; the model proposes stimuli, never replacements.
class RoleState {
public:
    void restore(const nlohmann::json& snapshot,std::int64_t now);
    nlohmann::json snapshot()const;
    void tick(std::int64_t now,bool advance=true);
    bool propose(std::string activity,double intensity,const std::string& evidence,std::int64_t now,std::string& issue);
    void weather(double temperature,int weatherCode,std::int64_t observedAt);
private:
    mutable std::mutex mutex_;
    double energy_=0.72,openness_=0.55,mood_=0.5,attention_=0.2;
    double targetMood_=0.5,targetOpenness_=0.55,targetAttention_=0.2;
    std::string activity_="idle",desired_="idle",evidence_;
    std::int64_t lastMs_=0,enteredMs_=0,stimulusMs_=0,weatherMs_=0;
    int weatherCode_=-1;
    double weatherComfort_=1.0;
};
}
