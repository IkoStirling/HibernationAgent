#pragma once
#include <Hibernation/WidgetAppearance.h>
#include <Windows.h>

namespace hibernation::appearance {
inline COLORREF nativeColor(std::uint32_t rgb){return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
inline void draw(HDC dc,const Frame& frame,const Palette& colors,int x,int y,int size,bool right,double turnScale=1){
    const int pixel=std::max(2,size/24),actual=pixel*24;
    const double middle=x+size/2.0,bottom=y+(size-actual)/2.0+actual;
    // A small, solid shadow retains crisp transparency on the color-key layer.
    auto shadow=CreateSolidBrush(nativeColor(colors.rgb[5]));auto oldBrush=SelectObject(dc,shadow),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    Ellipse(dc,x+size/3,static_cast<int>(bottom-2),x+size*2/3,static_cast<int>(bottom+2));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(shadow);
    std::array<HBRUSH,6> brushes{};for(int i=1;i<=5;++i)brushes[i]=CreateSolidBrush(nativeColor(colors.rgb[i]));
    for(int row=0;row<24;++row)for(int col=0;col<24;++col){const auto cell=frame.at(col,row);if(!cell)continue;const int column=right?col:23-col;const double lean=(12-row)*frame.lean*pixel*(right?1:-1);
        auto px=[&](double p){return static_cast<LONG>(std::round(middle+(p-12)*pixel*frame.scaleX*turnScale+lean));};
        auto py=[&](double p){return static_cast<LONG>(std::round(bottom+(p-24)*pixel*frame.scaleY+frame.offsetY*pixel));};
        RECT box{px(column),py(row),px(column+1),py(row+1)};if(box.right>box.left&&box.bottom>box.top)FillRect(dc,&box,brushes[cell]);
    }
    for(int i=1;i<=5;++i)DeleteObject(brushes[i]);
}
}
