#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <cstdint>
#include <utility>

namespace hibernation {
enum class WidgetPointerRelease {none,click,drag};
class WidgetPointerGesture {
    bool down_=false,drag_=false;int x_=0,y_=0,limitX_=6,limitY_=6;
public:
    void press(int x,int y,int limitX=6,int limitY=6){down_=true;drag_=false;x_=x;y_=y;limitX_=std::max(1,limitX);limitY_=std::max(1,limitY);}
    bool move(int x,int y){if(!down_||drag_)return false;if(std::abs(x-x_)<limitX_&&std::abs(y-y_)<limitY_)return false;drag_=true;return true;}
    bool pressed()const{return down_;}
    WidgetPointerRelease release(){const auto result=!down_?WidgetPointerRelease::none:drag_?WidgetPointerRelease::drag:WidgetPointerRelease::click;cancel();return result;}
    void cancel(){down_=drag_=false;}
};
// Presentation height is independent of the saved desktop position.
class WidgetDragMotion {
    double since_=0,from_=0,to_=0,duration_=0;
    bool released_=false;
public:
    double height(double now)const {
        const double t=duration_>0?std::clamp((now-since_)/duration_,0.0,1.0):1;
        const double blend=released_?t*t:1-std::pow(1-t,3);
        return from_+(to_-from_)*blend;
    }
    void grab(double now,double lift,bool reduced=false){from_=height(now);to_=reduced?0:std::max(0.0,lift);since_=now;duration_=reduced?0:.12;released_=false;}
    void release(double now,bool reduced=false){from_=height(now);to_=0;since_=now;duration_=reduced||from_<.01?0:.20;released_=true;}
    bool falling(double now)const{return released_&&now<since_+duration_;}
    void reset(){*this={};}
};

struct WidgetPoseRequest {std::string state;double seconds=0;};
// One replaceable intent, scoped to its conversation epoch; never replay on restart.
class WidgetPoseQueue {
    std::optional<WidgetPoseRequest> pending_;
    double expires_=0;std::uint64_t epoch_=0;
public:
    void request(std::string state,double seconds,double now,std::uint64_t epoch){pending_=WidgetPoseRequest{std::move(state),seconds};expires_=now+120;epoch_=epoch;}
    void cancel(){pending_.reset();}
    void attention(std::uint64_t epoch){if(epoch!=epoch_)cancel();}
    bool pending()const{return pending_.has_value();}
    std::string state()const{return pending_?pending_->state:std::string{};}
    std::optional<WidgetPoseRequest> take(double now,bool ready){if(now>=expires_)cancel();if(!ready)return {};auto result=std::move(pending_);pending_.reset();return result;}
};
}
