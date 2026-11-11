#include <Hibernation/ChatBubbles.h>
#include <Windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks=0,maximumHeight=0;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);++checks;}
void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
struct Row{HWND window;int height,width,lines;};
std::vector<Row> inspect(HWND viewport){
 std::vector<Row> rows;
 EnumChildWindows(viewport,[](HWND h,LPARAM p)->BOOL{
  if(GetDlgCtrlID(h)<2000)return TRUE;wchar_t cls[80]{};GetClassNameW(h,cls,80);
  if(std::wstring(cls).find(L"RICHEDIT")==std::wstring::npos)return TRUE;
  RECT r{};GetClientRect(h,&r);auto lines=static_cast<int>(SendMessageW(h,EM_GETLINECOUNT,0,0));
  static_cast<std::vector<Row>*>(reinterpret_cast<void*>(p))->push_back({h,r.bottom,r.right,lines});return TRUE;
 },reinterpret_cast<LPARAM>(&rows));
 return rows;
}
void sane(HWND viewport,const char* phase){
 for(const auto& r:inspect(viewport)){
  maximumHeight=std::max(maximumHeight,r.height);
  if(r.height>std::max(1,r.lines)*48+40){
   std::cerr<<"Excess geometry at "<<phase<<": lines="<<r.lines<<" height="<<r.height<<" width="<<r.width<<'\n';
   throw std::runtime_error("bubble height must track laid-out text, not previous scroll coordinates");
  }
  POINTL first{},last{};const auto length=GetWindowTextLengthW(r.window);
  SendMessageW(r.window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
  SendMessageW(r.window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&last),std::max(0,length-1));
  if(first.y<0||last.y+16>r.height){std::cerr<<"Clipped geometry at "<<phase<<": first="<<first.y<<" last="<<last.y<<" height="<<r.height<<" width="<<r.width<<" lines="<<r.lines<<" length="<<length<<'\n';}
  check(first.y>=0&&last.y+16<=r.height,"text including final glyph fits the actual content area");
 }
}
}
int main(int argc,char** argv){
 try{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const auto original=GetThreadDesktop(GetCurrentThreadId());
  auto name=L"XiaoAiBubbleRegression-"+std::to_wstring(GetCurrentProcessId());
  auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
  check(desktop&&SetThreadDesktop(desktop),"isolated inactive desktop is available");
  auto rich=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);check(rich,"system RichEdit loads");
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  auto body=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
  auto label=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
  auto parent=CreateWindowExW(0,L"STATIC",L"synthetic bubble fixture",WS_OVERLAPPEDWINDOW,0,0,700,820,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  check(parent,"test parent exists");ShowWindow(parent,SW_SHOWNOACTIVATE);
  {
   hibernation::ChatBubbles bubbles;
   auto viewport=reinterpret_cast<HWND>(bubbles.create(reinterpret_cast<std::uintptr_t>(parent),reinterpret_cast<std::uintptr_t>(body),reinterpret_cast<std::uintptr_t>(label)));
   MoveWindow(viewport,20,20,620,500,TRUE);
   std::vector<hibernation::ChatBubble> values;
   for(int i=0;i<22;++i)values.push_back({"seed-"+std::to_string(i),i%2==0,i%2?L"这是合成的前文，包含中文、English 和一些常见标点。暂时把这句话放在这里。\n第二段仍然保留。":L"合成短消息。"});
   bubbles.messages(values);pump();sane(viewport,"history");
   values.push_back({"latest-user",true,L"这是一条刚收到的合成消息，只有一行也不应被撑成很高。"});
   values.push_back({"stream",false,L""});
   std::wstring answer=L"先说眼前这件事。这里的文字会逐段出现，气泡高度应该随实际行数变化。\n\n下面混合中文、English、emoji 🦕🌙，以及一条不带空格的长路径：";
   answer+=std::wstring(240,L'x');answer+=L"\r\n最后一行要完整显示。";
   int step=0;
   for(std::size_t n=1;n<=answer.size();++n){
    if(n<answer.size()&&answer[n-1]>=0xd800&&answer[n-1]<=0xdbff)continue;
    values.back().text=answer.substr(0,n);bubbles.messages(values);
    if(step%11==0)SendMessageW(viewport,WM_VSCROLL,step%22?SB_TOP:SB_BOTTOM,0);
    if(step%17==0)MoveWindow(viewport,20,20,step%34?420:620,500,TRUE);
    pump();sane(viewport,"stream + scroll + resize");++step;
   }
   auto all=inspect(viewport);check(all.size()==values.size(),"each message retains an independent selectable control");
   auto selected=all.front().window;CHARRANGE range{1,4};SendMessageW(selected,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
   for(int width:{310,620,900,420,620}){MoveWindow(viewport,20,20,width,500,TRUE);pump();sane(viewport,"selected resize");CHARRANGE actual{};SendMessageW(selected,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&actual));check(actual.cpMin==range.cpMin&&actual.cpMax==range.cpMax,"selection survives resizing");}
   MoveWindow(viewport,20,20,0,0,TRUE);values.back().text+=L"\n缩小窗口期间追加的文字。";bubbles.messages(values);pump();
   MoveWindow(viewport,20,20,620,500,TRUE);pump();sane(viewport,"zero-size restore");
   ShowWindow(parent,SW_HIDE);
   for(int i=0;i<80;++i){values.back().text=answer.substr(0,40+i);bubbles.messages(values);if(i%9==0)SendMessageW(viewport,WM_VSCROLL,i%18?SB_TOP:SB_BOTTOM,0);}
   ShowWindow(parent,SW_SHOWNOACTIVATE);pump();sane(viewport,"hidden updates and restore");
   auto zoomed=inspect(viewport).back().window;
   SendMessageW(zoomed,EM_SETZOOM,5,4);pump();sane(viewport,"native text zoom changes at constant width");
   SendMessageW(zoomed,EM_SETZOOM,1,1);pump();
   auto larger=CreateFontW(-25,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
   SendMessageW(zoomed,WM_SETFONT,reinterpret_cast<WPARAM>(larger),TRUE);pump();sane(viewport,"native font changes at constant width");
   SendMessageW(zoomed,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);pump();DeleteObject(larger);
   // An edited/replaced answer may shrink. A previous larger natural size
   // must not become a permanent minimum for the same message identifier.
   values.back().text=L"短答。";bubbles.messages(values);pump();sane(viewport,"long-to-short replacement");
   auto shrunk=inspect(viewport);check(shrunk.back().lines==1&&shrunk.back().height<65,"replacing a long answer collapses unused height");
   // History navigation reuses some identities, destroys others, then returns.
   auto page=values;page.erase(page.begin()+8,page.end());bubbles.messages(page);pump();sane(viewport,"older page");
   bubbles.messages(values);pump();sane(viewport,"recent page restored");
   check(inspect(viewport).size()==values.size(),"history controls are restored without stale layout");
  }
  DestroyWindow(parent);DeleteObject(body);DeleteObject(label);FreeLibrary(rich);SetThreadDesktop(original);CloseDesktop(desktop);
  if(argc>1){std::ofstream report{std::filesystem::path(argv[1])};report<<"checks="<<checks<<"\nmaximum_height="<<maximumHeight<<"\npaid_calls=0\n";}
  std::cout<<"Native bubble regression passed "<<checks<<" geometry and selection checks; max height "<<maximumHeight<<". Paid calls: 0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
