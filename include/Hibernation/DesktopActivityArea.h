#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
namespace hibernation {
struct ActivityArea {double left,top,right,bottom;};
inline std::vector<ActivityArea> activityAreas(const std::vector<RECT>& work,int originX,int originY,int width,int height,int size,int inset=16){
    std::vector<ActivityArea> result;
    for(const auto& r:work){ActivityArea area{std::max(0.0,double(r.left-originX))+inset,std::max(0.0,double(r.top-originY))+inset,std::min(double(width),double(r.right-originX))-size-inset,std::min(double(height),double(r.bottom-originY))-size-inset};if(area.right>=area.left&&area.bottom>=area.top)result.push_back(area);}
    return result;
}
inline std::pair<double,double> constrainActivityPoint(const std::vector<ActivityArea>& areas,double x,double y){
    double best=1e100;std::pair<double,double> result{x,y};
    for(const auto& area:areas){const auto px=std::clamp(x,area.left,area.right),py=std::clamp(y,area.top,area.bottom);const auto d=std::hypot(px-x,py-y);if(d<best){best=d;result={px,py};}}return result;
}
inline std::vector<ActivityArea> desktopActivityAreas(int width,int height,int size){
    std::vector<RECT> work;EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{MONITORINFO info{sizeof(info)};if(GetMonitorInfoW(monitor,&info))reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcWork);return TRUE;},reinterpret_cast<LPARAM>(&work));
    auto result=activityAreas(work,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),width,height,size);
    if(result.empty())result.push_back({0,0,double(std::max(0,width-size)),double(std::max(0,height-size))});return result;
}
template<class Random> std::pair<double,double> sampleActivityPoint(const std::vector<ActivityArea>& areas,Random& random){
    std::vector<double> weights;for(const auto& a:areas)weights.push_back(std::max(1.0,(a.right-a.left)*(a.bottom-a.top)));
    const auto& a=areas.at(std::discrete_distribution<std::size_t>(weights.begin(),weights.end())(random));return {std::uniform_real_distribution<double>(a.left,a.right)(random),std::uniform_real_distribution<double>(a.top,a.bottom)(random)};
}
}
