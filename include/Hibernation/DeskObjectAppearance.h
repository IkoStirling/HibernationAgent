#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>
namespace hibernation::deskAppearance {
struct Frame {
    static constexpr int extent=16;
    std::array<std::uint8_t,extent*extent> cells{};
    std::array<std::uint32_t,5> colors{};
    int offsetY=0,pixelSize=3;
    void put(int x,int y,int color){if(x>=0&&x<extent&&y>=0&&y<extent)cells[y*extent+x]=static_cast<std::uint8_t>(color);}
    int at(int x,int y)const{return x>=0&&x<extent&&y>=0&&y<extent?cells[y*extent+x]:0;}
};
inline Frame make(std::string_view kind,int growth,double seconds,bool reduced=false,double age=1){
    Frame frame;frame.colors={0,0x3d5d3e,0x7ca767,0xf3dda7,0xc2d699};
    const char* const berry[]={"    11    ","   1221   ","  122221  ","  133331  "," 13344331 "," 13333331 "," 13333331 ","  133331  ","   1111   "};
    const char* const leaf[]={"       11 ","     1221 ","   122221 ","  1224421 "," 12244221 "," 1244221  "," 144221   "," 1111     "};
    const char* const seed[]={"   11   ","  1221  "," 122221 "," 124221 "," 122221 "," 122221 ","  1111  "};
    const char* const star[]={"    1    ","   121   ","111222111","122333221"," 1233321 ","  12321  "," 1232321 "," 1111111 "};
    const char* const sprout[]={" 11    11 ","1221  1221","1222112221"," 11222211 ","   1221   ","    11    ","    11    ","   1221   "};
    const char* const leaves[]={"  11       "," 1221      "," 12221 11  ","  12221221 ","   1222221 ","    12111  "," 112221    ","1222221    "," 111221    ","    121    ","    121    ","   12221   "};
    const char* const bud[]={"    11     ","   1331    ","   1341    ","   1221    ","    11     ","  1121     "," 12221 11  "," 122212221 ","  11222221 ","    12111  "," 112221    ","1222221    "," 111221    ","    121    ","   12221   "};
    const char* const flower[]={"   1331    ","  133331   ","1333443331 ","1334444331 "," 13344331  ","  133331   ","   1221    ","    11     "," 11221 11  ","1222212221 "," 112222221 ","   121111  "," 112221    ","1222221    "," 111221    ","   12221   "};
    const char* const* pixels=star;int rows=8;
    if(kind=="berry"){pixels=berry;rows=9;frame.colors={0,0x6f404b,0x7ca767,0xbe6d80,0xedb0b0};}
    else if(kind=="leaf")pixels=leaf;
    else if(kind=="seed"){pixels=seed;rows=7;frame.colors={0,0x7b603e,0xdbba77,0xf3dda7,0xc2d699};}
    else if(kind=="paper_star")frame.colors={0,0x7b603e,0xdbba77,0xf3dda7,0xf5e8b5};
    else if(kind=="sprout"){pixels=growth<=0?sprout:growth==1?leaves:growth==2?bud:flower;rows=growth<=0?8:growth==1?12:growth==2?15:16;if(growth>=2)frame.colors[4]=0xe6bd67;}
    for(int row=0;row<rows;++row)for(int col=0;pixels[row][col];++col)if(pixels[row][col]>='1'&&pixels[row][col]<='4')frame.put(col+3,row+16-rows,pixels[row][col]-'0');
    if(!reduced){frame.offsetY=static_cast<int>(std::sin(seconds*2.5)*1.5);if(kind=="paper_star"&&std::fmod(seconds,4)<.2){frame.put(2,4,4);frame.put(12,8,4);}if(kind=="sprout"&&growth>=3&&std::fmod(seconds,5)<.2){frame.put(2,2,4);frame.put(14,5,4);}if(age<.35)frame.pixelSize=2;}
    return frame;
}
}
