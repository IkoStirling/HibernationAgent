#pragma once
#include <algorithm>
namespace hibernation {
struct TelevisionBox {int left=0,top=0,right=0,bottom=0;int width()const{return right-left;}int height()const{return bottom-top;}};
struct TelevisionLayout {TelevisionBox screen,footer,power,play,stop,login;};
inline TelevisionLayout televisionLayout(int width,int height,bool framed){
    width=std::max(160,width);height=std::max(120,height);
    if(!framed)return {{0,0,width,height-34},{14,height-27,width-14,height-4},{},{},{},{}};
    const int left=std::min(18,width/12),top=std::min(20,height/10),rail=std::min(62,width/4),foot=std::min(48,height/4),button=std::min(30,width/10),x=width-rail/2-button/2;
    return {{left,top,width-rail,height-foot},{left,height-25,width-rail,height-5},{x,12,x+button,12+button},{x,height/2-34,x+button,height/2-4},{x,height/2+6,x+button,height/2+36},{}};
}
}
