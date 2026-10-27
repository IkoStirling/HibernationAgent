#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
namespace hibernation {
struct ScreenBox {int x=0,y=0,width=0,height=0;};
inline nlohmann::json screenBoxJson(ScreenBox b){return {{"x",b.x},{"y",b.y},{"width",b.width},{"height",b.height}};}
// Desktop origin may be negative. Movement coordinates are deliberately NOT
// used here: they cover desktop minus sprite size, unlike screenshot regions.
inline nlohmann::json widgetScreenGeometry(ScreenBox desktop,ScreenBox canvas,ScreenBox hitbox,bool available,int padding=16){
    using Json=nlohmann::json;Json result={{"available",false},{"coordinate_space","physical_screen_pixels"},{"desktop",screenBoxJson(desktop)},{"canvas",screenBoxJson(canvas)},{"hitbox",screenBoxJson(hitbox)},{"observed_at_ms",0}};
    if(!available||desktop.width<=0||desktop.height<=0||canvas.width<=0||canvas.height<=0)return result;
    const int left=std::max(desktop.x,canvas.x-padding),top=std::max(desktop.y,canvas.y-padding);
    const int right=std::min(desktop.x+desktop.width,canvas.x+canvas.width+padding),bottom=std::min(desktop.y+desktop.height,canvas.y+canvas.height+padding);
    if(right<=left||bottom<=top)return result;
    result["available"]=true;result["capture_rectangle"]=screenBoxJson({left,top,right-left,bottom-top});
    const double x=double(left-desktop.x)/desktop.width,y=double(top-desktop.y)/desktop.height;
    result["capture_region"]={{"x",x},{"y",y},{"width",double(right-desktop.x)/desktop.width-x},{"height",double(bottom-desktop.y)/desktop.height-y}};
    result["note"]="程序计算的角色画布、命中区域和带16像素边距的截图范围；不等于覆盖整个屏幕的透明宿主窗口。x/y移动坐标不能直接用作截图比例。截图自己的外观优先 computer.screenshot target=widget，执行时重新读取范围。";return result;
}
}
