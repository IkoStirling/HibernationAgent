#include "../plugins/widget/DrawAppearance.h"
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok)return 3;
    constexpr int width=1400,height=510;
    auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,width,height);auto old=SelectObject(dc,bitmap);
    auto paper=CreateSolidBrush(RGB(247,248,243));RECT all{0,0,width,height};FillRect(dc,&all,paper);DeleteObject(paper);
    const auto font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(48,85,67));
    const char* states[]={"idle","walk","walk","curious","resting","dragged","landing","speaking","eating"};
    const wchar_t* labels[]={L"停留 / 眨眼",L"行走 A",L"行走 B",L"好奇",L"休息",L"被挪动",L"落地缓冲",L"说话",L"咀嚼"};
    for(int col=0;col<9;++col){RECT label{125+col*140,18,265+col*140,46};DrawTextW(dc,labels[col],-1,&label,DT_CENTER|DT_SINGLELINE);}
    const char* skins[]={"dinosaur","moonbird","lumen"};const char* palettes[]={"forest","mist","amber"};const wchar_t* names[]={L"小恐龙\n苔绿",L"月羽鸟\n雾蓝",L"游光\n暖砂"};
    for(int row=0;row<3;++row){RECT label{12,95+row*145,118,170+row*145};DrawTextW(dc,names[row],-1,&label,DT_CENTER);for(int col=0;col<9;++col){const double time=col==0?6.6:col==1?.1:col==2?.5:1;auto f=hibernation::appearance::make(skins[row],states[col],time,false,.2,true);hibernation::appearance::draw(dc,f,hibernation::appearance::palette(palettes[row]),140+col*140,52+row*145,112,true);}}
    SelectObject(dc,old);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));DeleteObject(font);DeleteDC(dc);ReleaseDC(nullptr,screen);
    int result=0;{Gdiplus::Bitmap image(bitmap,nullptr);const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};if(image.Save(argv[1],&png,nullptr)!=Gdiplus::Ok)result=4;}
    DeleteObject(bitmap);Gdiplus::GdiplusShutdown(token);return result;
}
