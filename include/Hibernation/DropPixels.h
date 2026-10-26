#pragma once
#include <Hibernation/DeskObjectAppearance.h>
#include <algorithm>
#include <cstdint>
namespace hibernation::deskAppearance {
inline std::uint32_t over(std::uint32_t target,std::uint32_t rgb,unsigned alpha){
    const unsigned inverse=255-alpha;auto channel=[&](int shift){return (((rgb>>shift)&255)*alpha+((target>>shift)&255)*inverse+127)/255;};
    const unsigned opacity=alpha+(((target>>24)&255)*inverse+127)/255;
    return (opacity<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
}
// Premultiplied BGRA for a real per-pixel-alpha layered window. Transparent
// pixels remain zero; no color-key background is blended into fading edges.
inline void raster(std::uint32_t* pixels,int width,int height,const Frame& frame,int center,int bottom,unsigned alpha){
    if(!pixels||width<=0||height<=0||!alpha)return;
    for(int row=0;row<Frame::extent;++row)for(int col=0;col<Frame::extent;++col){const auto color=frame.at(col,row);if(!color)continue;const int size=frame.pixelSize,left=center+(col-8)*size,top=bottom+(row-16)*size+frame.offsetY;
        for(int y=std::max(0,top);y<std::min(height,top+size);++y)for(int x=std::max(0,left);x<std::min(width,left+size);++x)pixels[y*width+x]=over(pixels[y*width+x],frame.colors[color],std::min(alpha,255u));
    }
}
}
