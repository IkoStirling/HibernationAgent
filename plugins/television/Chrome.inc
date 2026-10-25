    HWND webSurface_=nullptr,powerButton_=nullptr,playButton_=nullptr,stopButton_=nullptr,tooltip_=nullptr;
    int regionWidth_=0,regionHeight_=0;bool regionFramed_=false,lastButtonPlaying_=false,runtimePaused_=false;
    bool framed()const{return cfg_.value("frameStyle",std::string("television"))=="television";}
    COLORREF shellColor()const{const auto color=cfg_.value("framePalette",std::string("sage"));return color=="walnut"?RGB(171,137,105):color=="ivory"?RGB(239,234,218):RGB(215,226,203);}
    static RECT nativeBox(const TelevisionBox& box){return {box.left,box.top,box.right,box.bottom};}
    TelevisionLayout chromeLayout()const{RECT r{};if(window_)GetClientRect(window_,&r);return televisionLayout(r.right,r.bottom,framed());}
    PluginJson chromeScreenSnapshot()const{if(!window_)return nullptr;const auto pane=chromeLayout().screen;return {{"x",pane.left},{"y",pane.top},{"width",pane.width()},{"height",pane.height()}};}
    static void fill(HDC dc,const RECT& rect,COLORREF color){const auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
    void createChrome(){
        WNDCLASSW surface{};surface.lpfnWndProc=DefWindowProcW;surface.hInstance=instance_;surface.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));surface.lpszClassName=L"HibernationTelevisionSurface";RegisterClassW(&surface);
        webSurface_=CreateWindowExW(0,surface.lpszClassName,L"影像",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,window_,reinterpret_cast<HMENU>(3500),instance_,nullptr);
        auto button=[&](const wchar_t* label,int id){return CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,30,30,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);};
        powerButton_=button(L"收起电视",3501);playButton_=button(L"播放或暂停",3502);stopButton_=button(L"停止播放",3503);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES};InitCommonControlsEx(&common);tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,window_,nullptr,instance_,nullptr);
        if(tooltip_)for(const auto& entry:std::initializer_list<std::pair<HWND,const wchar_t*>>{{powerButton_,L"收起电视；稍后可从托盘重新打开"},{playButton_,L"播放 / 暂停（空格）"},{stopButton_,L"停止这段影像"}}){TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window_;info.uId=reinterpret_cast<UINT_PTR>(entry.first);info.lpszText=const_cast<wchar_t*>(entry.second);SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
        layoutChrome();
    }
    void layoutChrome(){
        if(!window_)return;RECT client{};GetClientRect(window_,&client);if(client.right<=0||client.bottom<=0)return;const auto layout=chromeLayout();
        if(webSurface_){const auto& r=layout.screen;SetWindowPos(webSurface_,nullptr,r.left,r.top,r.width(),r.height(),SWP_NOACTIVATE|SWP_NOZORDER);ShowWindow(webSurface_,webMode_?SW_SHOWNOACTIVATE:SW_HIDE);}
        const auto place=[&](HWND control,const TelevisionBox& r){if(control){SetWindowPos(control,nullptr,r.left,r.top,r.width(),r.height(),SWP_NOACTIVATE|SWP_NOZORDER);ShowWindow(control,framed()?SW_SHOWNOACTIVATE:SW_HIDE);}};
        place(powerButton_,layout.power);place(playButton_,layout.play);place(stopButton_,layout.stop);
        if(regionWidth_!=client.right||regionHeight_!=client.bottom||regionFramed_!=framed()){
            regionWidth_=client.right;regionHeight_=client.bottom;regionFramed_=framed();auto region=framed()?CreateRoundRectRgn(0,0,client.right+1,client.bottom+1,22,22):nullptr;if(!SetWindowRgn(window_,region,TRUE)&&region)DeleteObject(region);
        }
        if(webMode_&&host_.browser)host_.browser("resize",{{"width",layout.screen.width()},{"height",layout.screen.height()}},{});
    }
    void applyChrome(){
        if(!window_)return;const DWORD style=(framed()?WS_POPUP|WS_THICKFRAME|WS_SYSMENU:WS_OVERLAPPEDWINDOW)|WS_CLIPCHILDREN;
        if((static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE))&~WS_VISIBLE)!=style){const bool shown=IsWindowVisible(window_)!=FALSE;SetWindowLongPtrW(window_,GWL_STYLE,style|(shown?WS_VISIBLE:0));SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
        layoutChrome();dirty_=true;InvalidateRect(window_,nullptr,FALSE);for(auto button:{powerButton_,playButton_,stopButton_})if(button)InvalidateRect(button,nullptr,FALSE);
    }
    void togglePlayback(){if(runtimePaused_)return;if(webMode_&&host_.browser)host_.browser("control",{{"action","toggle"}},{});else enqueue({{{"action",playing_?"pause":"play"}}, {}});}
    void stopPlayback(){if(webMode_&&host_.browser)host_.browser("control",{{"action","stop"}},{});else enqueue({{{"action","stop"}}, {}});}
    void hideTelevision(){visible_=false;ShowWindow(window_,SW_HIDE);if(webMode_&&host_.browser)host_.browser("control",{{"action","pause"}},{});else enqueue({{{"action","pause"}}, {}});}
    void drawChrome(HDC dc,const RECT& client){
        const auto layout=chromeLayout();fill(dc,client,framed()?shellColor():RGB(24,29,25));
        if(framed()){
            auto outer=nativeBox(layout.screen);InflateRect(&outer,4,4);const auto pen=CreatePen(PS_SOLID,2,RGB(110,133,105));const auto brush=CreateSolidBrush(RGB(42,54,44));const auto previousPen=SelectObject(dc,pen),previousBrush=SelectObject(dc,brush);RoundRect(dc,outer.left,outer.top,outer.right,outer.bottom,12,12);SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(pen);DeleteObject(brush);
            const int rail=client.right-31;for(int i=0;i<5;++i){RECT line{rail-10,client.bottom-79+i*6,rail+10,client.bottom-77+i*6};fill(dc,line,RGB(130,149,122));}
            const auto led=CreateSolidBrush(state_=="failed"?RGB(174,104,90):playing_||webMode_?RGB(94,133,89):RGB(155,168,143));const auto old=SelectObject(dc,led);const auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,layout.screen.left,client.bottom-34,layout.screen.left+6,client.bottom-28);SelectObject(dc,old);SelectObject(dc,oldPen);DeleteObject(led);
            if(!webMode_&&duration_>0){RECT bar{layout.screen.left+15,client.bottom-33,layout.screen.right,client.bottom-30};fill(dc,bar,RGB(189,204,176));bar.right=bar.left+static_cast<LONG>((bar.right-bar.left)*std::clamp(position_/duration_,0.0,1.0));fill(dc,bar,RGB(107,143,100));}
        }
        fill(dc,nativeBox(layout.screen),RGB(24,29,25));
    }
    void drawChromeButton(const DRAWITEMSTRUCT& item){
        fill(item.hDC,item.rcItem,shellColor());const auto pen=CreatePen(PS_SOLID,1,RGB(113,137,106));const auto brush=CreateSolidBrush((item.itemState&ODS_SELECTED)?RGB(148,172,133):RGB(235,240,226));const auto oldPen=SelectObject(item.hDC,pen),oldBrush=SelectObject(item.hDC,brush);
        const int cx=(item.rcItem.left+item.rcItem.right)/2,cy=(item.rcItem.top+item.rcItem.bottom)/2;Ellipse(item.hDC,item.rcItem.left+1,item.rcItem.top+1,item.rcItem.right-1,item.rcItem.bottom-1);SelectObject(item.hDC,GetStockObject(DC_PEN));SetDCPenColor(item.hDC,RGB(69,101,66));SelectObject(item.hDC,GetStockObject(DC_BRUSH));SetDCBrushColor(item.hDC,RGB(69,101,66));
        if(item.CtlID==3501){Arc(item.hDC,cx-6,cy-5,cx+7,cy+7,cx+4,cy-6,cx-4,cy-6);MoveToEx(item.hDC,cx,cy-8,nullptr);LineTo(item.hDC,cx,cy+1);}
        else if(item.CtlID==3503){RECT square{cx-4,cy-4,cx+5,cy+5};FillRect(item.hDC,&square,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
        else if(playing_&&!webMode_){RECT a{cx-5,cy-5,cx-2,cy+6},b{cx+2,cy-5,cx+5,cy+6};FillRect(item.hDC,&a,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));FillRect(item.hDC,&b,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
        else {POINT points[]={{cx-3,cy-6},{cx+6,cy},{cx-3,cy+6}};Polygon(item.hDC,points,3);}
        if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-5,-5);DrawFocusRect(item.hDC,&focus);}SelectObject(item.hDC,oldPen);SelectObject(item.hDC,oldBrush);DeleteObject(pen);DeleteObject(brush);
    }
