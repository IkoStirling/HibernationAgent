#pragma once
#include <Hibernation/DeskObjectAppearance.h>
#include <Windows.h>
namespace hibernation::deskAppearance {
inline void draw(HDC dc,const Frame& frame,int center,int bottom){
    std::array<HBRUSH,5> brushes{};for(int i=1;i<5;++i){const auto color=frame.colors[i];brushes[i]=CreateSolidBrush(RGB((color>>16)&255,(color>>8)&255,color&255));}
    for(int row=0;row<Frame::extent;++row)for(int col=0;col<Frame::extent;++col){const auto color=frame.at(col,row);if(!color)continue;const int size=frame.pixelSize;RECT r{center+(col-8)*size,bottom+(row-16)*size+frame.offsetY,center+(col-7)*size,bottom+(row-15)*size+frame.offsetY};FillRect(dc,&r,brushes[color]);}
    for(int i=1;i<5;++i)DeleteObject(brushes[i]);
}
}
