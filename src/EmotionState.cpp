#include <Hibernation/EmotionState.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace hibernation {
namespace {
using Json=nlohmann::json;
constexpr std::array<std::string_view,7> emotionIds={"joy","anger","sadness","worry","affection","disgust","surprise"};
constexpr std::array<std::string_view,6> desireIds={"rest","explore","company","express","play","replenish"};
constexpr std::array<double,7> emotionScale={.8,.85,.75,.7,.55,.75,.8};
// Active-time seconds. Absence, pause and process downtime never accumulate
// dependence or secretly replay feelings. Surprise naturally fades faster.
constexpr std::array<double,7> emotionDecay={180,150,240,150,300,150,18};
double approach(double value,double target,double step){return value+std::clamp(target-value,-step,step);}
bool sourceText(const std::string& value){if(value.empty()||value.size()>256||value.find('\0')!=std::string::npos)return false;try{(void)Json(value).dump();return true;}catch(...){return false;}}
template<std::size_t N> Json values(const std::array<double,N>& a,const std::array<std::string_view,N>& ids){Json value=Json::object();for(std::size_t i=0;i<N;++i)value[std::string(ids[i])]=a[i];return value;}
template<std::size_t N> std::array<double,N> readValues(const Json& value,const std::array<std::string_view,N>& ids,double low,double high){
    if(!value.is_object()||value.size()!=N)throw std::runtime_error("emotion_state_values");std::array<double,N> result{};
    for(std::size_t i=0;i<N;++i){const auto& v=value.at(std::string(ids[i]));if(!v.is_number())throw std::runtime_error("emotion_state_values");const double x=v.get<double>();if(!std::isfinite(x)||x<low||x>high)throw std::runtime_error("emotion_state_values");result[i]=x;}return result;
}
template<std::size_t N> std::string dominant(const std::array<double,N>& data,const std::array<std::string_view,N>& ids,double threshold){const auto it=std::max_element(data.begin(),data.end());return *it>=threshold?std::string(ids[static_cast<std::size_t>(it-data.begin())]):std::string{};}
template<std::size_t N> bool proposalValues(const Json& value,const std::array<std::string_view,N>& ids,std::array<double,N>& result,double& total){
    if(!value.is_object()||value.size()>3)return false;
    for(auto it=value.begin();it!=value.end();++it){const auto found=std::find(ids.begin(),ids.end(),it.key());if(found==ids.end()||!it.value().is_number())return false;const double x=it.value().get<double>();if(!std::isfinite(x)||std::abs(x)>1)return false;result[static_cast<std::size_t>(found-ids.begin())]=x;total+=std::abs(x);}return true;
}
}

void EmotionState::restore(const Json& saved){
    if(!saved.is_object())throw std::runtime_error("emotion_state_shape");if(saved.empty()){*this=EmotionState{};return;}
    // Validate into a temporary so invalid snapshots cannot partly replace a
    // live emotional state or erase its duplicate-source protection.
    EmotionState candidate;
    try{
        if(saved.size()!=8||saved.at("version")!=1)throw std::runtime_error("emotion_state_version");
        candidate.emotions_=readValues(saved.at("emotions"),emotionIds,0,1);candidate.desires_=readValues(saved.at("desires"),desireIds,0,1);
        candidate.emotionDrive_=readValues(saved.at("emotion_drive"),emotionIds,-1,1);candidate.desireDrive_=readValues(saved.at("desire_drive"),desireIds,-1,1);
        for(const auto* field:{"clock_seconds","last_stimulus_seconds"})if(!saved.at(field).is_number())throw std::runtime_error("emotion_state_clock");
        candidate.clock_=saved.at("clock_seconds").get<double>();candidate.lastStimulus_=saved.at("last_stimulus_seconds").get<double>();
        if(!std::isfinite(candidate.clock_)||candidate.clock_<0||candidate.clock_>1e10||!std::isfinite(candidate.lastStimulus_)||(candidate.lastStimulus_<0&&candidate.lastStimulus_!=-1)||candidate.lastStimulus_>candidate.clock_)throw std::runtime_error("emotion_state_clock");
        const auto& ledger=saved.at("sources");if(!ledger.is_array()||ledger.size()>128)throw std::runtime_error("emotion_state_sources");
        for(const auto& item:ledger){if(!item.is_object()||item.size()!=2||!item.at("id").is_string()||!item.at("clock").is_number())throw std::runtime_error("emotion_state_sources");Source source{item.at("id").get<std::string>(),item.at("clock").get<double>()};if(!sourceText(source.id)||!std::isfinite(source.clock)||source.clock<0||source.clock>candidate.clock_||std::any_of(candidate.sources_.begin(),candidate.sources_.end(),[&](const Source& x){return x.id==source.id;}))throw std::runtime_error("emotion_state_sources");candidate.sources_.push_back(std::move(source));}
        if(candidate.sources_.empty()?candidate.lastStimulus_!=-1:candidate.lastStimulus_!=candidate.sources_.back().clock)throw std::runtime_error("emotion_state_sources");
        for(std::size_t i=1;i<candidate.sources_.size();++i)if(candidate.sources_[i].clock<candidate.sources_[i-1].clock)throw std::runtime_error("emotion_state_sources");
    }catch(const nlohmann::json::exception&){throw std::runtime_error("emotion_state_shape");}
    *this=std::move(candidate);
}

EmotionState::Json EmotionState::snapshot()const{
    Json sources=Json::array();for(const auto& item:sources_)sources.push_back({{"id",item.id},{"clock",item.clock}});
    return {{"version",1},{"emotions",values(emotions_,emotionIds)},{"desires",values(desires_,desireIds)},{"emotion_drive",values(emotionDrive_,emotionIds)},{"desire_drive",values(desireDrive_,desireIds)},{"clock_seconds",clock_},{"last_stimulus_seconds",lastStimulus_},{"sources",std::move(sources)}};
}

EmotionState::Json EmotionState::context()const{
    const bool sharp=emotions_[1]>=.2||emotions_[5]>=.25;
    return {{"emotions",values(emotions_,emotionIds)},{"desires",values(desires_,desireIds)},
            {"dominant_emotion",dominant(emotions_,emotionIds,.12)},{"dominant_desire",dominant(desires_,desireIds,.35)},
            {"behavior",{{"may_decline",true},{"may_remain_silent",true},{"may_use_dry_irony",sharp},
                {"guide","这些是连续的计算情感与虚拟欲望，不是必须执行的任务或体验证明。可以开心、难过、担忧、生气、拒绝或不回应；不要求每轮提及感受或报数。生气时允许简短反驳、不带脏字的克制冷讽，这是许可而非每句必须挖苦；不强制辱骂、羞辱或借敏感身份攻击。喜爱和陪伴不等于恋爱、依附或占有，不因用户暂时离开制造惩罚。欲望不证明资源存在或操作完成。"}}}};
}

bool EmotionState::stimulate(const Json& proposal,const std::string& sourceId,std::int64_t now,std::string& issue){
    if(!sourceText(sourceId)||now<0||!proposal.is_object()||proposal.empty()||proposal.size()>2){issue="invalid_emotion_stimulus";return false;}
    for(auto it=proposal.begin();it!=proposal.end();++it)if(it.key()!="emotions"&&it.key()!="desires"){issue="invalid_emotion_stimulus";return false;}
    std::array<double,7> emotions{};std::array<double,6> desires{};double total=0;
    try{if((proposal.contains("emotions")&&!proposalValues(proposal.at("emotions"),emotionIds,emotions,total))||(proposal.contains("desires")&&!proposalValues(proposal.at("desires"),desireIds,desires,total))||total<=0||total>3){issue="invalid_emotion_stimulus";return false;}}catch(...){issue="invalid_emotion_stimulus";return false;}
    if(std::any_of(sources_.begin(),sources_.end(),[&](const Source& s){return s.id==sourceId;})){issue="emotion_source_already_used";return false;}
    if(lastStimulus_>=0&&clock_-lastStimulus_<5){issue="emotion_stimulus_cooldown";return false;}
    // Only the latent stimulus changes here. Actual values wait for bounded
    // active-time ticks. Different fresh sources cannot stack without limit.
    for(std::size_t i=0;i<emotions.size();++i)if(emotions[i]!=0)emotionDrive_[i]=std::clamp(emotionDrive_[i]*.45+emotions[i]*.55,-1.0,1.0);
    for(std::size_t i=0;i<desires.size();++i)if(desires[i]!=0)desireDrive_[i]=std::clamp(desireDrive_[i]*.45+desires[i]*.55,-1.0,1.0);
    sources_.push_back({sourceId,clock_});if(sources_.size()>128)sources_.erase(sources_.begin());lastStimulus_=clock_;issue.clear();return true;
}

void EmotionState::tick(double seconds,const EmotionEnvironment& environment){
    if(!std::isfinite(seconds)||seconds<=0)return;const double dt=std::min(seconds,.25);
    const auto valid=[](double x){return std::isfinite(x)&&x>=0&&x<=1;};
    if(!valid(environment.energy)||!valid(environment.openness)||!valid(environment.attention)||!valid(environment.socialLoad))return;
    clock_=std::min(1e10,clock_+dt);
    for(std::size_t i=0;i<emotions_.size();++i){emotionDrive_[i]*=std::exp(-dt/emotionDecay[i]);const double target=std::clamp(emotionDrive_[i]*emotionScale[i],0.0,1.0);emotions_[i]=std::clamp(approach(emotions_[i],target,dt*(i==6?.025:.01)),0.0,1.0);}
    const double energy=environment.energy,open=environment.openness,load=environment.socialLoad;
    const std::array<double,6> baseline={
        std::clamp(.12+(1-energy)*.65+load*.15-(environment.resting?.15:0),.05,.9),
        std::clamp(.16+energy*.2+open*.2-load*.1,.05,.7),
        std::clamp(.12+open*.25-load*.35,.03,.5),
        std::clamp(.12+environment.attention*.2+(emotions_[0]+emotions_[1])*.12,.05,.65),
        std::clamp(.12+energy*.22-load*.08,.05,.6),
        std::clamp(.1+(1-energy)*.12,.05,.3)};
    for(std::size_t i=0;i<desires_.size();++i){desireDrive_[i]*=std::exp(-dt/240.);const auto target=std::clamp(baseline[i]+desireDrive_[i]*.4,0.0,1.0);desires_[i]=std::clamp(approach(desires_[i],target,dt*.004),0.0,1.0);}
}

double EmotionState::moodOffset()const{return std::clamp(emotions_[0]*.12+emotions_[4]*.05-emotions_[1]*.10-emotions_[2]*.12-emotions_[3]*.06-emotions_[5]*.07,-.16,.12);}
double EmotionState::willingnessOffset()const{return std::clamp(emotions_[0]*.03+emotions_[4]*.02-emotions_[1]*.12-emotions_[2]*.04-emotions_[3]*.06-emotions_[5]*.08,-.18,.05);}
}
