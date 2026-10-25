#include <Hibernation/StartupSplash.h>
#include <Hibernation/SplashPixels.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace hibernation {
namespace {
using namespace splashPixels;
LRESULT CALLBACK splashProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(message==WM_ERASEBKGND)return 1;return DefWindowProcW(hwnd,message,w,l);
}
}
struct StartupSplash::Impl {
    std::atomic<bool> stop=false,finished=false;std::thread thread;bool reduced=false;std::string palette;
    Impl(bool enabled,bool reduce,std::string color):reduced(reduce),palette(std::move(color)){if(enabled)thread=std::thread([this]{run();});}
    ~Impl(){stop=true;if(thread.joinable())thread.join();}
    void run();
};
void StartupSplash::Impl::run(){
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);reduced=reduced||!animations;
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=splashProcedure;wc.hInstance=instance;wc.lpszClassName=L"HibernationStartup";RegisterClassW(&wc);
    MONITORINFO monitor{sizeof(monitor)};RECT work{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};if(GetMonitorInfoW(MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY),&monitor))work=monitor.rcWork;
    POINT position{(work.left+work.right-width)/2,(work.top+work.bottom-height)/2};SIZE size{width,height};POINT source{};
    auto window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"小爱",WS_POPUP,position.x,position.y,width,height,nullptr,nullptr,instance,nullptr);
    if(!window){UnregisterClassW(wc.lpszClassName,instance);return;}
    auto screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* bits=nullptr;
    auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);HGDIOBJ old=nullptr;
    if(memory&&bitmap&&bits){
        old=SelectObject(memory,bitmap);auto* pixels=static_cast<std::uint32_t*>(bits);BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};const auto colors=appearance::palette(palette);
        const auto start=std::chrono::steady_clock::now();double fadeStart=-1;bool shown=false;
        while(!stop){
            const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();const double minimum=reduced?250:900;
            if((finished&&milliseconds>=minimum)||milliseconds>=8000){if(fadeStart<0)fadeStart=milliseconds;}
            const double leaving=fadeStart<0?0:smooth((milliseconds-fadeStart)/220);if(leaving>=1)break;
            splashPixels::render(pixels,milliseconds,leaving,reduced,colors);
            if(!UpdateLayeredWindow(window,screen,&position,&size,memory,&source,0,&blend,ULW_ALPHA))break;
            if(!shown){SetWindowPos(window,HWND_TOPMOST,position.x,position.y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);shown=true;}
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)stop=true;else{TranslateMessage(&msg);DispatchMessageW(&msg);}}
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    if(old)SelectObject(memory,old);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,instance);
}
StartupSplash::StartupSplash(bool enabled,bool reduced,std::string palette):impl_(std::make_unique<Impl>(enabled,reduced,std::move(palette))){}
StartupSplash::~StartupSplash()=default;
void StartupSplash::ready(){impl_->finished=true;}
}
