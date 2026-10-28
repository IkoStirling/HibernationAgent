#include "../plugins/pixel-canvas/PixelSurface.h"
#include "../plugins/pixel-canvas/PixelPlayback.h"
#include <Windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace hibernation;
namespace {
int checks=0;
void check(bool okay,const char* name){if(!okay)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<'\n';}
PluginJson artwork(){
    std::vector<int> pixels(256,1);pixels[0]=0;auto second=pixels;std::fill(second.begin(),second.end(),2);
    return {{"id","surface-test"},{"title","synthetic artwork"},{"revision",1},{"width",16},{"height",16},{"palette",PluginJson::array({"#00000000","#ff000080","#00ff00ff"})},{"frames",PluginJson::array({{{"duration_ms",100},{"pixels",pixels}},{{"duration_ms",100},{"pixels",second}}})}};
}
std::vector<std::uint32_t> capture(HWND window,int width,int height){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;std::uint32_t* data=nullptr;
    auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&data),nullptr,0);if(!bitmap)throw std::runtime_error("capture bitmap");auto dc=CreateCompatibleDC(nullptr);auto previous=SelectObject(dc,bitmap);SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    std::vector<std::uint32_t> pixels(data,data+width*height);SelectObject(dc,previous);DeleteDC(dc);DeleteObject(bitmap);return pixels;
}
}
int main(){
    const auto original=GetThreadDesktop(GetCurrentThreadId());const auto name=L"HibernationPixelSurfaceTest-"+std::to_wstring(GetCurrentProcessId());
    auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop)){if(desktop)CloseDesktop(desktop);std::cerr<<"private desktop unavailable\n";return 2;}
    int code=0;
    try{
        std::size_t frame=0;double elapsed=0;
        check(pixel::advancePixelFrames({100,200},.11,true,frame,elapsed)&&frame==1,"playback clock advances exactly at per-frame duration");
        check(!pixel::advancePixelFrames({100,200},1,false,frame,elapsed)&&frame==1,"suspended playback does not accumulate deferred time");
        check(pixel::advancePixelFrames({100,200},.2,true,frame,elapsed)&&frame==0,"playback clock loops through unequal frame durations");
        check(!pixel::advancePixelFrames({100,200},-1,true,frame,elapsed),"invalid elapsed time is refused");
        pixel::DesktopSurface surface;std::vector<PluginJson> events;const auto doc=artwork();
        surface.configure(false,PluginJson::object());check(!surface.show(doc,true,3).at("ok"),"headless display is refused without a window");
        surface.configure(true,{{"reducedMotion",false}},[&](PluginJson value){events.push_back(std::move(value));});
        auto invalid=doc;invalid["frames"][0]["pixels"][5]=99;check(!surface.show(invalid,true,3).at("ok"),"invalid palette reference never reaches rendering");
        check(!surface.show(doc,true,1.5).at("ok"),"fractional pixel scale is refused");
        surface.show(doc,true,3);surface.event({{"kind","runtime.paused"},{"paused",true}});surface.event({{"kind","runtime.paused"},{"paused",false}});surface.tick(0);
        check(!surface.snapshot().at("visible")&&!surface.snapshot().at("pending")&&events.back().at("kind")=="pixel.display_cancelled","pause cancels queued display rather than showing it late on resume");
        surface.event({{"kind","runtime.paused"},{"paused",true}});check(!surface.show(doc,true,3).at("ok"),"display requested during pause is refused immediately");
        surface.event({{"kind","runtime.paused"},{"paused",false}});surface.tick(0);check(!surface.snapshot().at("pending")&&!surface.snapshot().at("visible"),"refused paused request cannot appear after resume");
        check(surface.show(doc,true,3).at("result").at("completed")==false&&surface.snapshot().at("pending")==true&&!surface.snapshot().at("visible"),"worker request queues without prematurely confirming display");
        surface.tick(0);const auto first=surface.snapshot();auto window=FindWindowW(L"HibernationPixelArtworkV1",nullptr);
        check(window&&first.at("visible")==true&&first.at("width")==48&&first.at("height")==48,"UI owner displays the real layered window");
        check(!events.empty()&&events.back().at("kind")=="pixel.displayed"&&events.back().at("completed")==true,"actual display produces a completed receipt");
        auto pixels=capture(window,48,48);check(pixels[0]==0&&pixels[3]==0x80800000u&&pixels[4]==pixels[3]&&pixels[5]==pixels[3],"transparent and premultiplied pixels keep exact integer blocks");
        surface.tick(.11);
        if(!first.at("reduced_motion").get<bool>()){
            check(surface.snapshot().at("frame")==1,"enabled animation advances actual desktop artwork");pixels=capture(window,48,48);check(pixels[0]==0xff00ff00u,"second animation frame is painted");
        }else{
            check(surface.snapshot().at("frame")==0,"operating system reduced-motion policy is respected");
            auto green=doc;green["revision"]=2;green["frames"][0]=green["frames"][1];surface.show(green,true,3);surface.tick(0);pixels=capture(window,48,48);check(pixels[0]==0xff00ff00u,"updated artwork frame reaches the actual layered window");
        }
        const auto beforePause=surface.snapshot().at("frame");
        surface.event({{"kind","runtime.paused"},{"paused",true}});surface.tick(.2);check(surface.snapshot().at("frame")==beforePause,"pause freezes animation without hiding the artwork");
        surface.event({{"kind","runtime.paused"},{"paused",false}});surface.configure(true,{{"reducedMotion",true}},[&](PluginJson value){events.push_back(std::move(value));});surface.tick(.2);check(surface.snapshot().at("frame")==beforePause,"reduced motion prevents frame changes");
        SendMessageW(window,WM_CLOSE,0,0);check(!surface.snapshot().at("visible")&&!IsWindowVisible(window),"closing hides artwork without deleting its document");
        surface.event({{"kind","surface.show"},{"surface","pixel-canvas"}});surface.tick(0);check(surface.snapshot().at("visible")==true,"surface can be reopened after closing");
        check(!surface.placement(100001,0)&&surface.placement(120,120),"restored placement is bounded before screen clamping");
        check(!surface.hide("different-artwork").at("ok"),"hiding a different document does not affect the displayed one");
        check(surface.hide("surface-test").at("ok"),"already owned artwork can be hidden without rereading its file");surface.tick(0);check(!surface.snapshot().at("visible"),"cached hide completes on its UI owner");
        check(!surface.snapshot().dump().contains("pixels")&&!surface.snapshot().dump().contains("palette"),"high frequency snapshot carries no pixel grids");
        surface.reset();surface.event({{"kind","surface.show"},{"surface","pixel-canvas"}});surface.tick(0);
        check(surface.snapshot().at("document_id")==""&&!surface.snapshot().at("visible")&&!surface.snapshot().at("pending"),"workspace reset cannot reopen a previously authorized artwork");
        surface.shutdown();check(!IsWindow(window),"shutdown destroys the owned window");
        std::cout<<"Pixel surface passed: "<<checks<<" checks on a private desktop.\n";
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';code=1;}
    SetThreadDesktop(original);CloseDesktop(desktop);return code;
}
