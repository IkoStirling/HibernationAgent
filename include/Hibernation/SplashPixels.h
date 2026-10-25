#pragma once
#include <Hibernation/WidgetAppearance.h>
namespace hibernation::splashPixels {
inline constexpr int width=240,height=200;
inline double smooth(double p){p=std::clamp(p,0.0,1.0);return p*p*(3-2*p);}
inline void circle(std::uint32_t* pixels,double x,double y,double radius,std::uint32_t rgb,double opacity){
    const int left=std::max(0,static_cast<int>(x-radius-1)),right=std::min(width-1,static_cast<int>(x+radius+1)),top=std::max(0,static_cast<int>(y-radius-1)),bottom=std::min(height-1,static_cast<int>(y+radius+1));
    for(int row=top;row<=bottom;++row)for(int col=left;col<=right;++col){const auto alpha=static_cast<unsigned>(std::clamp(radius+.6-std::hypot(col+.5-x,row+.5-y),0.0,1.0)*opacity*255);if(!alpha)continue;auto& pixel=pixels[row*width+col];const auto inverse=255-alpha;auto channel=[&](int shift){return ((((rgb>>shift)&255)*alpha+((pixel>>shift)&255)*inverse)+127)/255;};const auto outAlpha=alpha+(((pixel>>24)&255)*inverse+127)/255;pixel=(outAlpha<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);}
}
// Premultiplied BGRA for UpdateLayeredWindow. Every frame starts transparent.
inline void render(std::uint32_t* pixels,double milliseconds,double leaving,bool reduced,const appearance::Palette& colors){
    const double opacity=smooth(milliseconds/(reduced?100:160))*(1-std::clamp(leaving,0.0,1.0)),gather=reduced?1:smooth((milliseconds-90)/490),middleX=width/2.0,middleY=height/2.0-(reduced?0:leaving*6);
    std::fill(pixels,pixels+width*height,0);const double breath=reduced?1:1+std::sin(milliseconds/430)*.025;
    circle(pixels,middleX,middleY,43*gather*breath,colors.rgb[3],opacity*.2);circle(pixels,middleX,middleY,36*gather*breath,colors.rgb[5],opacity*.52);
    const double fromX[]={-46,39,8},fromY[]={-27,-22,43},toX[]={0,-10,10},toY[]={-10,9,9};
    for(int dot=0;dot<3;++dot)circle(pixels,middleX+fromX[dot]*(1-gather)+toX[dot]*gather,middleY+fromY[dot]*(1-gather)+toY[dot]*gather,3.2+gather*.3,colors.rgb[1],opacity);
}
}
