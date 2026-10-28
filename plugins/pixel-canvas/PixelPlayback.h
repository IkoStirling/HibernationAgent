#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
namespace hibernation::pixel {
// A bounded live tick clock; pause/reduced-motion/offscreen policy is supplied
// by the owner. No wall-clock catch-up is invented after process downtime.
inline bool advancePixelFrames(const std::vector<int>& milliseconds,double delta,
                               bool running,std::size_t& frame,double& elapsed){
    if(!running||milliseconds.size()<2||milliseconds.size()>8||frame>=milliseconds.size()||!std::isfinite(delta)||delta<0)return false;
    for(const auto duration:milliseconds)if(duration<50||duration>10000)return false;
    elapsed+=std::min(delta,.25);bool changed=false;
    for(int steps=0;steps<8&&elapsed>=milliseconds[frame]/1000.0;++steps){elapsed-=milliseconds[frame]/1000.0;frame=(frame+1)%milliseconds.size();changed=true;}
    return changed;
}
}
