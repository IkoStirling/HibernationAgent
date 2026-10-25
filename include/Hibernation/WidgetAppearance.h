#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace hibernation::appearance {
// Original, code-native pixel art. Zero is transparent; the other indices use
// the selected palette. Pose selection is deterministic and has no model calls.
struct Frame {
    static constexpr int extent=24;
    std::array<std::uint8_t,extent*extent> cells{};
    double offsetY=0,scaleX=1,scaleY=1,lean=0;
    bool blink=false,mouthOpen=false;
    unsigned phase=0;
    void put(int x,int y,int value){if(x>=0&&x<extent&&y>=0&&y<extent)cells[y*extent+x]=static_cast<std::uint8_t>(value);}
    int at(int x,int y)const{return x>=0&&x<extent&&y>=0&&y<extent?cells[y*extent+x]:0;}
};
struct Palette {std::array<std::uint32_t,6> rgb;};
inline bool validSkin(std::string_view id){return id=="dinosaur"||id=="moonbird"||id=="lumen";}
inline bool validPalette(std::string_view id){return id=="forest"||id=="mist"||id=="amber";}
inline Palette palette(std::string_view id){
    if(id=="mist")return {{{0,0x455a6c,0x9bb4c5,0xe1e9df,0x273a50,0xc8d4df}}};
    if(id=="amber")return {{{0,0x74523c,0xd6a66b,0xf3dfb0,0x463b34,0xeac48b}}};
    return {{{0,0x305543,0x7ba769,0xd2dea3,0x192b21,0xafca92}}};
}
inline void stamp(Frame& f,const char* const* rows,int count,int left=0,int top=0){
    for(int y=0;y<count;++y)for(int x=0;rows[y][x];++x)if(rows[y][x]>='1'&&rows[y][x]<='5')f.put(x+left,y+top,rows[y][x]-'0');
}
inline Frame make(std::string_view skin,std::string_view state,double seconds,bool reduced=false,double landingAge=1,bool mouthBeat=false){
    Frame f;const double time=std::max(0.0,seconds);f.phase=static_cast<unsigned>(time*10)%8;
    const bool rest=state=="resting",walk=state=="walk",drag=state=="dragged",curious=state=="curious"||state=="listening";
    f.blink=rest||(!drag&&std::fmod(time,6.7)>6.52);f.mouthOpen=(state=="eating"&&std::fmod(time*4,1.0)<0.5)||(state=="speaking"&&mouthBeat);
    if(skin=="moonbird"){
        static const char* const rows[]={"      11      11      ","     1221    1221     ","    12222111122221    ","    12222222222221    ","   1223332222333221   ","   1233333223333321   ","   1233433223343321   ","   1233333223333321   ","   1223332222333221   ","   1222224412222221   ","  122222224222222221  ","  122222333322222221  ","  122223333332222221  ","  11223333333322111  ","   12233333333221    ","    122333333221     ","     1222222221      ","      11222211       ","       141141        ","       111111        "};
        stamp(f,rows,20,0,2);
        if(f.blink){for(int x:{7,14}){f.put(x,8,3);f.put(x-1,9,4);f.put(x,9,4);f.put(x+1,9,4);}}
        if(f.mouthOpen){f.put(10,12,4);f.put(11,12,4);f.put(10,13,3);}
        if(walk&&!reduced){for(int x=6;x<=13;++x)for(int y=20;y<=23;++y)f.put(x,y,0);const int step=f.phase<4?0:1;for(int x:{8,11}){f.put(x,20,1);f.put(x,21+(x==8?step:1-step),1);f.put(x+1,21+(x==8?step:1-step),1);}}
        if(drag&&!reduced){f.put(1,12,2);f.put(0,11,1);f.put(21,12,2);f.put(22,11,1);}
    }else if(skin=="lumen"){
        static const char* const rows[]={"          11          ","         1221         ","        122221        ","       12233221       ","      1223333221      ","     122333333221     ","    12233333333221    ","   1223333333333221   ","   1233333333333321   ","  123333343343333321  ","  123333333333333321  ","  123333333333333321  ","   1233333443333321   ","   122333333333221   ","    1223333333221    ","     12233333221     ","      122222221      ","       1111111       "};
        stamp(f,rows,18,0,3);
        if(f.blink){for(int x:{8,11}){f.put(x,12,3);f.put(x-1,13,4);f.put(x,13,4);}}
        if(f.mouthOpen){f.put(10,15,4);f.put(11,15,4);f.put(10,16,4);f.put(11,16,4);}
        if(!reduced){const int side=f.phase<4?-1:1;f.put(3,18+side,5);f.put(19,6-side,5);}
    }else{
        static const char* const rows[]={"            111111111   ","           12222222221  ","           12222422221  ","           12222222221  ","           12222211111  ","           1222221      ","           1222221111   ","1         12222221      ","11       122222221      ","121     122222222111    ","1221   12222222221      ","122211122222222221      ","122222222222223321      "," 122222222222233321     ","  12222222222233321     ","   1222222222233321     ","    122222222233321     ","     1222222222221      ","      11222222211       ","       12111221         ","       121 1221         ","       111 1111         "};
        stamp(f,rows,22,0,1);
        if(f.blink){f.put(16,3,2);f.put(15,4,4);f.put(16,4,4);f.put(17,4,4);}
        if(f.mouthOpen){for(int x=17;x<=21;++x)f.put(x,7,4);for(int x=17;x<=20;++x)f.put(x,8,1);}
        if(walk&&!reduced){for(int x=6;x<=14;++x)for(int y=20;y<24;++y)f.put(x,y,0);const int front=f.phase<4?1:0;for(int leg=0;leg<2;++leg){const int x=8+leg*4+(leg?front:-front);f.put(x,20,1);f.put(x,21,2);f.put(x,22-(leg==front?1:0),1);f.put(x+1,22-(leg==front?1:0),1);}}
        if(drag&&!reduced){for(int y=21;y<24;++y)for(int x=7;x<=14;++x)f.put(x,y,0);for(int x:{8,12}){f.put(x,21,1);f.put(x,22,2);f.put(x,23,1);}}
        if(!reduced&&!rest){const int swing=static_cast<int>(std::sin(time*(walk?8:2.2))*1.4);if(swing){auto before=f.cells;for(int y=7;y<16;++y)for(int x=0;x<5;++x)f.put(x,y,0);for(int y=7;y<16;++y)for(int x=0;x<5;++x)if(before[y*24+x])f.put(x,y+swing,before[y*24+x]);}}
    }
    if(rest){f.scaleY=0.84;f.scaleX=1.04;}
    if(!reduced){
        f.offsetY=walk?-std::abs(std::sin(time*10))*1.4:rest?0:std::sin(time*2)*0.45;
        if(curious){f.lean=0.06*std::sin(std::min(1.0,std::fmod(time,4.0))*3.141592653589793);f.scaleY=1.02;}
        if(drag){f.lean=std::sin(time*8)*0.05;f.scaleX=0.96;f.scaleY=1.04;}
        if(state=="landing"&&landingAge<0.4){const double pulse=std::sin(std::clamp(landingAge/0.4,0.0,1.0)*3.141592653589793);f.scaleX=1+0.1*pulse;f.scaleY=1-0.15*pulse;}
        if(state=="eating")f.offsetY+=std::sin(time*12)*0.6;
        if(skin=="lumen"&&!rest)f.offsetY+=std::sin(time*2.5)*1.1;
    }
    return f;
}
}
