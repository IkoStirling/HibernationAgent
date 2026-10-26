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
    bool operator==(const Frame&)const=default;
    void put(int x,int y,int color){if(x>=0&&x<extent&&y>=0&&y<extent)cells[y*extent+x]=static_cast<std::uint8_t>(color);}
    int at(int x,int y)const{return x>=0&&x<extent&&y>=0&&y<extent?cells[y*extent+x]:0;}
};
inline Frame make(std::string_view kind,int growth,double seconds,bool reduced=false,double age=1,std::string_view tint="original",bool rare=false){
    Frame frame;frame.colors={0,0x3d5d3e,0x7ca767,0xf3dda7,0xc2d699};
    const char* const berry[]={"    11    ","   1221   ","  122221  ","  133331  "," 13344331 "," 13333331 "," 13333331 ","  133331  ","   1111   "};
    const char* const leaf[]={"       11 ","     1221 ","   122221 ","  1224421 "," 12244221 "," 1244221  "," 144221   "," 1111     "};
    const char* const seed[]={"   11   ","  1221  "," 122221 "," 124221 "," 122221 "," 122221 ","  1111  "};
    const char* const star[]={"    1    ","   121   ","111222111","122333221"," 1233321 ","  12321  "," 1232321 "," 1111111 "};
    const char* const apple[]={"     11    ","    12111  ","  11313211 "," 1333333331","13334433331","13344333331","13333333331"," 133333331 ","  1331331  ","   11 11   "};
    const char* const bread[]={"   111111   ","  12222221  "," 1233233221 ","12322322221 ","12222222221 ","12222222221 "," 122222221  ","  1111111   "};
    const char* const pudding[]={"    11    ","   1331   ","  111111  "," 12222221 "," 12322321 ","1222222221","1222222221"," 11111111 ","1333333331"};
    const char* const tea[]={"   4 4     ","    4      ","   4       "," 1111111   ","1222222211 ","12333332121","12222222121"," 122222111 ","  111111   ","1333333331 "};
    const char* const crystal[]={"    11    ","   1231   ","  123331  "," 12343331 ","1234433331","1233333331"," 12333331 ","  123331  ","   1231   ","    11    "};
    const char* const sprout[]={" 11    11 ","1221  1221","1222112221"," 11222211 ","   1221   ","    11    ","    11    ","   1221   "};
    const char* const leaves[]={"  11       "," 1221      "," 12221 11  ","  12221221 ","   1222221 ","    12111  "," 112221    ","1222221    "," 111221    ","    121    ","    121    ","   12221   "};
    const char* const bud[]={"    11     ","   1331    ","   1341    ","   1221    ","    11     ","  1121     "," 12221 11  "," 122212221 ","  11222221 ","    12111  "," 112221    ","1222221    "," 111221    ","    121    ","   12221   "};
    const char* const flower[]={"   1331    ","  133331   ","1333443331 ","1334444331 "," 13344331  ","  133331   ","   1221    ","    11     "," 11221 11  ","1222212221 "," 112222221 ","   121111  "," 112221    ","1222221    "," 111221    ","   12221   "};
    const char* const* pixels=star;int rows=8;
    if(kind=="berry"){pixels=berry;rows=9;frame.colors={0,0x6f404b,0x7ca767,0xbe6d80,0xedb0b0};}
    else if(kind=="leaf")pixels=leaf;
    else if(kind=="seed"){pixels=seed;rows=7;frame.colors={0,0x7b603e,0xdbba77,0xf3dda7,0xc2d699};}
    else if(kind=="paper_star")frame.colors={0,0x7b603e,0xdbba77,0xf3dda7,0xf5e8b5};
    else if(kind=="apple"){pixels=apple;rows=10;frame.colors={0,0x794947,0x80a267,0xd77c6d,0xf6c19a};}
    else if(kind=="bread"){pixels=bread;rows=8;frame.colors={0,0x806043,0xd7a865,0xf4d79b,0xfbe9bf};}
    else if(kind=="pudding"){pixels=pudding;rows=9;frame.colors={0,0x886442,0xf0cf83,0xf7ebc3,0xb37f56};}
    else if(kind=="tea"){pixels=tea;rows=10;frame.colors={0,0x4d6d63,0xb4d1b5,0xadb976,0xdbe8df};}
    else if(kind=="crystal"){pixels=crystal;rows=10;frame.colors={0,0x596786,0x9aafd3,0xc8d6ee,0xecedfc};}
    else if(kind=="sprout"){pixels=growth<=0?sprout:growth==1?leaves:growth==2?bud:flower;rows=growth<=0?8:growth==1?12:growth==2?15:16;if(growth>=2)frame.colors[4]=0xe6bd67;}
    for(int row=0;row<rows;++row)for(int col=0;pixels[row][col];++col)if(pixels[row][col]>='1'&&pixels[row][col]<='4')frame.put(col+3,row+16-rows,pixels[row][col]-'0');
    if(tint=="rose")frame.colors={0,0x774755,0xba8092,0xf0c7ce,0xffe2df};
    if(tint=="mint")frame.colors={0,0x3f6c5c,0x81ba9a,0xccebd3,0xedf5da};
    if(tint=="amber")frame.colors={0,0x795e3b,0xc7a268,0xf3dfaa,0xffeec3};
    if(tint=="sky")frame.colors={0,0x46657d,0x82b6cb,0xc9e4ed,0xf1f5e8};
    if(tint=="violet")frame.colors={0,0x62517b,0xaa91c4,0xe0cfee,0xffe1e9};
    if(rare&&(reduced||std::fmod(seconds,3)<.35)){frame.put(1,2,4);frame.put(2,1,4);frame.put(3,2,4);frame.put(2,3,4);}
    if(!reduced){frame.offsetY=static_cast<int>(std::sin(seconds*2.5)*1.5);if(kind=="paper_star"&&std::fmod(seconds,4)<.2){frame.put(2,4,4);frame.put(12,8,4);}if(kind=="sprout"&&growth>=3&&std::fmod(seconds,5)<.2){frame.put(2,2,4);frame.put(14,5,4);}if(age<.35)frame.pixelSize=2;}
    return frame;
}
}
