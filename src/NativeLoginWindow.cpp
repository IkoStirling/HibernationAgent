#include <Hibernation/NativeLoginWindow.h>
#include <Windows.h>
#ifdef HA_HAS_WEBVIEW
#include <AYPlatform/WebView.h>
#endif
namespace hibernation {
struct NativeLoginWindow::Impl {
 HWND window=nullptr,owner=nullptr;std::string url,origin;std::filesystem::path profile;bool navigated=false;
#ifdef HA_HAS_WEBVIEW
 std::unique_ptr<ayt::platform::WebView> view;
#endif
 Impl(std::uintptr_t o,std::string u,std::filesystem::path p):owner(reinterpret_cast<HWND>(o)),url(std::move(u)),profile(std::move(p)){origin=url.substr(0,url.find('/',url.find("://")+3));}
 static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){
  auto* a=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if(m==WM_NCCREATE){a=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);a->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
  if(!a)return DefWindowProcW(w,m,wp,lp);
  if(m==WM_CLOSE){ShowWindow(w,SW_HIDE);
#ifdef HA_HAS_WEBVIEW
   if(a->view)a->view->visible(false);
#endif
   return 0;}
  if(m==WM_SIZE){
#ifdef HA_HAS_WEBVIEW
   RECT r{};GetClientRect(w,&r);if(a->view)a->view->bounds(r.right,r.bottom);
#endif
   return 0;}
  if(m==WM_PAINT){PAINTSTRUCT ps{};auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);auto b=CreateSolidBrush(RGB(247,248,243));FillRect(dc,&r,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(60,85,67));DrawTextW(dc,L"正在准备 QQ 登录窗口……",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);EndPaint(w,&ps);return 0;}
  return DefWindowProcW(w,m,wp,lp);
 }
};
NativeLoginWindow::NativeLoginWindow(std::uintptr_t owner,std::string url,std::filesystem::path profile):impl_(std::make_unique<Impl>(owner,std::move(url),std::move(profile))){}
NativeLoginWindow::~NativeLoginWindow(){
#ifdef HA_HAS_WEBVIEW
 if(impl_->view)impl_->view->close();
#endif
 if(impl_->window)DestroyWindow(impl_->window);
}
bool NativeLoginWindow::show(){
#ifndef HA_HAS_WEBVIEW
 return false;
#else
 auto& a=*impl_;
 if(!a.window){
  WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=Impl::procedure;c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.lpszClassName=L"HibernationNapcatLogin";RegisterClassW(&c);
  a.window=CreateWindowExW(WS_EX_TOOLWINDOW,c.lpszClassName,L"小爱 · QQ 登录",WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,540,730,a.owner,nullptr,c.hInstance,&a);if(!a.window)return false;
  a.view=std::make_unique<ayt::platform::WebView>();
  if(!a.view->initialize(reinterpret_cast<std::uintptr_t>(a.window),a.profile,[base=a.origin](const std::string& u){return u.starts_with(base+"/");})){a.view.reset();DestroyWindow(a.window);a.window=nullptr;return false;}
 }
 ShowWindow(a.window,SW_SHOWNORMAL);a.view->visible(true);SetForegroundWindow(a.window);update();return true;
#endif
}
void NativeLoginWindow::update(){
#ifdef HA_HAS_WEBVIEW
 auto& a=*impl_;if(!a.view||!a.view->ready()||a.navigated)return;RECT r{};GetClientRect(a.window,&r);a.view->bounds(r.right,r.bottom);a.navigated=a.view->navigate(a.url);
#endif
}
}
