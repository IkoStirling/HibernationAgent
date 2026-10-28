#include "PixelSurface.h"
#include "PixelPlayback.h"
#include <Windows.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace hibernation::pixel {
namespace {
constexpr wchar_t windowClass[]=L"HibernationPixelArtworkV1";
void moduleAnchor(){}
std::optional<std::uint32_t> rgba(const PluginJson& value){
    if(!value.is_string())return {};
    const auto text=value.get<std::string>();
    if(text.size()!=9||text[0]!='#')return {};
    std::uint32_t packed=0;
    for(std::size_t i=1;i<text.size();++i){const auto c=text[i];unsigned digit;
        if(c>='0'&&c<='9')digit=c-'0';else if(c>='a'&&c<='f')digit=c-'a'+10;
        else if(c>='A'&&c<='F')digit=c-'A'+10;else return {};packed=(packed<<4)|digit;}
    const auto alpha=packed&255u;
    const auto red=(packed>>24)*alpha/255,green=((packed>>16)&255u)*alpha/255,blue=((packed>>8)&255u)*alpha/255;
    return (alpha<<24)|(red<<16)|(green<<8)|blue;
}
bool validSurfaceDocument(const PluginJson& document){
    try{
        if(!document.is_object()||!document.at("id").is_string()||document.at("id").get_ref<const std::string&>().size()>48||!document.at("revision").is_number_integer())return false;
        const auto width=document.at("width").get<int>(),height=document.at("height").get<int>();
        if((width!=16&&width!=32&&width!=64)||(height!=16&&height!=32&&height!=64))return false;
        const auto& palette=document.at("palette");if(!palette.is_array()||palette.empty()||palette.size()>32)return false;
        for(const auto& color:palette)if(!rgba(color))return false;
        if((*rgba(palette.front())>>24)!=0)return false;
        const auto& frames=document.at("frames");if(!frames.is_array()||frames.empty()||frames.size()>8)return false;
        for(const auto& frame:frames){if(!frame.at("duration_ms").is_number_integer())return false;const int duration=frame.at("duration_ms");if(duration<50||duration>10000)return false;
            const auto& pixels=frame.at("pixels");if(!pixels.is_array()||pixels.size()!=static_cast<std::size_t>(width*height))return false;
            for(const auto& pixel:pixels)if(!pixel.is_number_integer()||pixel.get<std::int64_t>()<0||pixel.get<std::int64_t>()>=static_cast<std::int64_t>(palette.size()))return false;}
        return true;
    }catch(...){return false;}
}
}
struct DesktopSurface::Impl {
    mutable std::recursive_mutex mutex;
    struct Command {PluginJson document;bool visible;int scale;std::uint64_t token;};
    std::optional<Command> pending;
    PluginJson document=PluginJson::object();
    std::function<void(PluginJson)> emit;
    bool desktop=false,visible=false,paused=false,reducedMotion=false,configuredReducedMotion=false,topmost=true,positioned=false;
    bool pressed=false,dragging=false;POINT press{},origin{};
    int scale=3,left=0,top=0,width=0,height=0;std::size_t frame=0;double elapsed=0;
    std::uint64_t sequence=0,paintCount=0;
    std::string issue;
    HINSTANCE instance=nullptr;HWND window=nullptr;HBITMAP bitmap=nullptr;std::uint32_t* buffer=nullptr;
    Impl(){GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&moduleAnchor),&instance);}
    void notify(const char* kind,PluginJson detail=PluginJson::object()){
        if(!emit)return;detail["kind"]=kind;detail["plugin"]="pixel-canvas";
        detail["wake_model"]=false;emit(std::move(detail));
    }
    void cancelPendingShow(const char* reason){
        if(!pending||!pending->visible)return;
        notify("pixel.display_cancelled",{{"id",pending->document.at("id")},{"revision",pending->document.at("revision")},{"request",pending->token},{"completed",false},{"reason",reason}});pending.reset();
    }
    void freeBitmap(){if(bitmap)DeleteObject(bitmap);bitmap=nullptr;buffer=nullptr;width=height=0;}
    void keepOnScreen(){
        RECT bounds{left,top,left+std::max(width,1),top+std::max(height,1)};
        MONITORINFO info{sizeof(info)};if(!GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&info))return;
        left=std::clamp(left,static_cast<int>(info.rcWork.left),std::max(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));
        top=std::clamp(top,static_cast<int>(info.rcWork.top),std::max(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-height));
    }
    bool create(){
        if(window)return true;
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32649));wc.lpszClassName=windowClass;
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){issue="pixel_surface_registration_failed";return false;}
        window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|(topmost?WS_EX_TOPMOST:0),windowClass,L"小爱 · 像素作品",WS_POPUP,left,top,1,1,nullptr,nullptr,instance,this);
        if(!window){issue="pixel_surface_create_failed";return false;}return true;
    }
    bool paint(){
        if(!window||document.empty())return false;
        const int nextWidth=document.at("width").get<int>()*scale,nextHeight=document.at("height").get<int>()*scale;
        if(!bitmap||width!=nextWidth||height!=nextHeight){freeBitmap();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=nextWidth;info.bmiHeader.biHeight=-nextHeight;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&buffer),nullptr,0);
            if(!bitmap){issue="pixel_surface_bitmap_failed";return false;}width=nextWidth;height=nextHeight;}
        std::array<std::uint32_t,32> colors{};const auto& palette=document.at("palette");for(std::size_t i=0;i<palette.size();++i)colors[i]=*rgba(palette[i]);
        const auto& pixels=document.at("frames").at(frame).at("pixels");const int sourceWidth=document.at("width");
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)buffer[y*width+x]=colors[pixels[(y/scale)*sourceWidth+x/scale].get<std::size_t>()];
        keepOnScreen();auto dc=CreateCompatibleDC(nullptr);if(!dc){issue="pixel_surface_dc_failed";return false;}auto previous=SelectObject(dc,bitmap);
        POINT destination{left,top},source{};SIZE extent{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool painted=UpdateLayeredWindow(window,nullptr,&destination,&extent,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
        SelectObject(dc,previous);DeleteDC(dc);if(!painted){issue="pixel_surface_update_failed";return false;}++paintCount;issue.clear();return true;
    }
    void print(HDC dc){if(!dc||!bitmap)return;auto source=CreateCompatibleDC(dc);if(!source)return;auto previous=SelectObject(source,bitmap);BitBlt(dc,0,0,width,height,source,0,0,SRCCOPY);SelectObject(source,previous);DeleteDC(source);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* a=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){a=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
        if(!a)return DefWindowProcW(hwnd,message,w,l);std::lock_guard guard(a->mutex);
        switch(message){
            case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
            case WM_ERASEBKGND:return 1;
            case WM_PRINTCLIENT:a->print(reinterpret_cast<HDC>(w));return 0;
            case WM_PAINT:{PAINTSTRUCT ps{};const auto dc=BeginPaint(hwnd,&ps);a->print(dc);EndPaint(hwnd,&ps);return 0;}
            case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&p);if(!a->buffer||p.x<0||p.y<0||p.x>=a->width||p.y>=a->height||(a->buffer[p.y*a->width+p.x]>>24)<24)return HTTRANSPARENT;return HTCLIENT;}
            case WM_LBUTTONDOWN:a->pressed=true;a->dragging=false;GetCursorPos(&a->press);a->origin={a->left,a->top};SetCapture(hwnd);return 0;
            case WM_MOUSEMOVE:if(a->pressed){POINT point;GetCursorPos(&point);if(!a->dragging&&(std::abs(point.x-a->press.x)>=GetSystemMetrics(SM_CXDRAG)||std::abs(point.y-a->press.y)>=GetSystemMetrics(SM_CYDRAG)))a->dragging=true;
                if(a->dragging){a->left=a->origin.x+point.x-a->press.x;a->top=a->origin.y+point.y-a->press.y;a->keepOnScreen();SetWindowPos(hwnd,nullptr,a->left,a->top,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);}}return 0;
            case WM_LBUTTONUP:if(a->pressed){const bool dragged=a->dragging;a->pressed=a->dragging=false;ReleaseCapture();a->notify(dragged?"pixel.moved":"pixel.touched",{{"id",a->document.value("id",std::string{})},{"revision",a->document.value("revision",std::int64_t(0))},{"completed",true}});}return 0;
            case WM_CAPTURECHANGED:a->pressed=a->dragging=false;return 0;
            case WM_RBUTTONUP:{auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"收起这件作品");POINT point;GetCursorPos(&point);SetForegroundWindow(hwnd);const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);
                if(selected==1){a->visible=false;ShowWindow(hwnd,SW_HIDE);a->notify("pixel.hidden",{{"id",a->document.value("id",std::string{})},{"completed",true},{"visible",false}});}return 0;}
            case WM_CLOSE:a->visible=false;ShowWindow(hwnd,SW_HIDE);a->notify("pixel.hidden",{{"id",a->document.value("id",std::string{})},{"completed",true},{"visible",false}});return 0;
            case WM_DISPLAYCHANGE:a->keepOnScreen();if(a->visible)a->paint();return 0;
            case WM_SETTINGCHANGE:{BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);a->reducedMotion=a->configuredReducedMotion||!enabled;return 0;}
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
};
DesktopSurface::DesktopSurface():impl_(std::make_unique<Impl>()){}
DesktopSurface::~DesktopSurface(){shutdown();}
void DesktopSurface::configure(bool desktopEnabled,const PluginJson& cfg,std::function<void(PluginJson)> emit){
    auto& a=*impl_;std::lock_guard guard(a.mutex);a.desktop=desktopEnabled;a.emit=std::move(emit);BOOL motion=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);a.configuredReducedMotion=cfg.value("reducedMotion",false);a.reducedMotion=a.configuredReducedMotion||!motion;a.topmost=cfg.value("topmost",true);
    if(a.window)SetWindowPos(a.window,a.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    if(!a.desktop){a.pending.reset();a.visible=false;if(a.window)ShowWindow(a.window,SW_HIDE);}
}
PluginJson DesktopSurface::show(const PluginJson& document,bool visible,double scale){
    auto& a=*impl_;std::lock_guard guard(a.mutex);if(!a.desktop)return toolFailure("desktop_required");
    if(visible&&a.paused)return toolFailure("runtime_paused");
    if(!std::isfinite(scale)||scale<1||scale>8||std::floor(scale)!=scale)return toolFailure("pixel_integer_scale_required");
    if(!validSurfaceDocument(document))return toolFailure("pixel_surface_document_invalid");
    if(a.pending)a.notify("pixel.display_cancelled",{{"id",a.pending->document.at("id")},{"revision",a.pending->document.at("revision")},{"request",a.pending->token},{"completed",false},{"reason","superseded"}});
    a.pending=Impl::Command{document,visible,static_cast<int>(scale),++a.sequence};
    return toolSuccess({{"accepted",true},{"completed",false},{"visible_requested",visible},{"id",document.at("id")},{"revision",document.at("revision")},{"request",a.sequence}});
}
PluginJson DesktopSurface::hide(const std::string& id){
    auto& a=*impl_;std::lock_guard guard(a.mutex);if(!a.desktop)return toolFailure("desktop_required");
    if(a.pending&&a.pending->document.value("id",std::string{})==id)return show(a.pending->document,false,a.pending->scale);
    if(a.document.value("id",std::string{})==id)return show(a.document,false,a.scale);
    return toolFailure("pixel_surface_not_current");
}
bool DesktopSurface::placement(int left,int top){
    if(left<-100000||left>100000||top<-100000||top>100000)return false;
    auto& a=*impl_;std::lock_guard guard(a.mutex);a.left=left;a.top=top;a.positioned=true;a.keepOnScreen();return true;
}
void DesktopSurface::tick(double dt){
    auto& a=*impl_;std::lock_guard guard(a.mutex);
    if(a.paused)a.cancelPendingShow("runtime_paused");
    if(a.pending){auto command=std::move(*a.pending);a.pending.reset();a.document=std::move(command.document);a.scale=command.scale;a.frame=0;a.elapsed=0;
        if(!command.visible){a.visible=false;if(a.window)ShowWindow(a.window,SW_HIDE);a.notify("pixel.hidden",{{"id",a.document.at("id")},{"revision",a.document.at("revision")},{"completed",true},{"visible",false},{"request",command.token}});}
        else if(a.desktop){
            if(!a.positioned){RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);a.left=work.right-a.document.at("width").get<int>()*a.scale-48;a.top=work.bottom-a.document.at("height").get<int>()*a.scale-48;a.positioned=true;}
            const bool okay=a.create()&&a.paint();a.visible=okay;if(a.window)ShowWindow(a.window,okay?SW_SHOWNOACTIVATE:SW_HIDE);
            a.notify(okay?"pixel.displayed":"pixel.display_failed",{{"id",a.document.at("id")},{"revision",a.document.at("revision")},{"completed",okay},{"visible",okay},{"request",command.token},{"error",okay?"":a.issue}});
        }
    }
    if(a.document.empty())return;
    std::vector<int> durations;for(const auto& frame:a.document.at("frames"))durations.push_back(frame.at("duration_ms").get<int>());
    const bool changed=advancePixelFrames(durations,dt,a.visible&&!a.paused&&!a.reducedMotion,a.frame,a.elapsed);
    if(changed&&!a.paint()){a.visible=false;if(a.window)ShowWindow(a.window,SW_HIDE);a.notify("pixel.display_failed",{{"id",a.document.at("id")},{"revision",a.document.at("revision")},{"completed",false},{"visible",false},{"error",a.issue}});}
}
void DesktopSurface::event(const PluginJson& event){auto& a=*impl_;std::lock_guard guard(a.mutex);const auto kind=event.value("kind",std::string{});if(kind=="runtime.paused"){a.paused=event.value("paused",false);if(a.paused)a.cancelPendingShow("runtime_paused");}if(kind=="surface.show"&&event.value("surface",std::string{})=="pixel-canvas"&&!a.document.empty()&&!a.paused)a.pending=Impl::Command{a.document,true,a.scale,++a.sequence};}
PluginJson DesktopSurface::snapshot()const{const auto& a=*impl_;std::lock_guard guard(a.mutex);return {{"desktop",a.desktop},{"visible",a.visible},{"pending",a.pending.has_value()},{"document_id",a.document.value("id",std::string{})},{"revision",a.document.value("revision",std::int64_t(0))},{"frame",a.frame},{"scale",a.scale},{"paused",a.paused},{"reduced_motion",a.reducedMotion},{"paint_count",a.paintCount},{"left",a.left},{"top",a.top},{"width",a.width},{"height",a.height},{"issue",a.issue}};}
void DesktopSurface::reset(){auto& a=*impl_;std::lock_guard guard(a.mutex);a.pending.reset();a.visible=false;if(a.window){DestroyWindow(a.window);a.window=nullptr;}a.freeBitmap();a.document=PluginJson::object();a.frame=0;a.elapsed=0;a.positioned=false;a.pressed=a.dragging=false;a.issue.clear();}
void DesktopSurface::shutdown(){reset();auto& a=*impl_;std::lock_guard guard(a.mutex);UnregisterClassW(windowClass,a.instance);}
}
