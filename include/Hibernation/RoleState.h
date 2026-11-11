#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
#include <optional>
#include <Hibernation/EmotionState.h>
namespace hibernation {
/// Continuous bounded values and discrete activities have independent clocks.
/// One snapshot is authoritative; the model proposes stimuli, never replacements.
class RoleState {
public:
    void restore(const nlohmann::json& snapshot,std::int64_t now);
    nlohmann::json snapshot()const;
    void tick(std::int64_t now,bool advance=true);
    bool propose(std::string activity,std::optional<double> intensity,const std::string& evidence,std::int64_t now,std::string& issue,std::optional<double> valence={},std::string focus={});
    bool appraise(double valence,const std::string& evidence,std::int64_t now,std::string& issue);
    /// The host must first authorize the source. This only submits a bounded
    /// stimulus; current emotion/desire values never change in this call.
    bool stimulate(const nlohmann::json& stimulus,const std::string& sourceId,std::int64_t now,std::string& issue);
    /// Model-safe projection: excludes latent drives and source-deduplication IDs.
    nlohmann::json emotionalContext()const;
    void contact(std::int64_t now);
    void weather(double temperature,int weatherCode,std::int64_t observedAt);
private:
    mutable std::mutex mutex_;
    double energy_=0.72,openness_=0.55,mood_=0.5,attention_=0.2;
    double targetMood_=0.5,targetOpenness_=0.55,targetAttention_=0.2;
    double willingness_=0.55,socialLoad_=0,targetSocialLoad_=0,targetValence_=0,weatherMood_=0.5;
    std::string mode_="balanced",focus_,appraisalEvidence_;
    double appraisalValence_=0;
    std::string activity_="idle",desired_="idle",evidence_;
    std::int64_t lastMs_=0,enteredMs_=0,stimulusMs_=0,weatherMs_=0,appraisalMs_=0;
    int weatherCode_=-1;
    double weatherComfort_=1.0;
    EmotionState affect_;
};
}
