    std::uint64_t loginEpoch=0;HWND loginWindow=nullptr;std::string loginReturnUrl;int televisionWidth=480,televisionHeight=280;bool televisionVisible=true;
    Json loginDetection=Json::object();std::string loginState="unknown",loginMethod;bool loginDismissed=false,authPending=false;
    std::chrono::steady_clock::time_point nextAuth{};
    void finishTelevisionLogin(bool restore=true,bool verified=false){
        const auto window=loginWindow;if(window){++loginEpoch;authPending=false;presentationPending=false;}loginWindow=nullptr;
        if(window&&restore&&!verified)loginDismissed=true;
        if(view&&restore&&IsWindow(reinterpret_cast<HWND>(parent))){
            view->visible(false);if(view->reparent(parent)){view->bounds(televisionWidth,televisionHeight);view->visible(televisionVisible);if(!loginReturnUrl.empty()&&view->source()!=loginReturnUrl)view->navigate(loginReturnUrl);}
        }
        loginReturnUrl.clear();if(window)DestroyWindow(window);
        nextAuth=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    }
    static LRESULT CALLBACK loginProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(message==WM_CLOSE){self->finishTelevisionLogin();return 0;}
            if(message==WM_SIZE&&self->loginWindow&&self->view&&w!=SIZE_MINIMIZED){RECT area{};GetClientRect(window,&area);self->view->bounds(std::max(1L,area.right),std::max(1L,area.bottom));return 0;}
            if(message==WM_GETMINMAXINFO){reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={640,480};return 0;}
            if(message==WM_NCDESTROY)SetWindowLongPtrW(window,GWLP_USERDATA,0);
        }
        return DefWindowProcW(window,message,w,l);
    }
    Json showTelevisionLogin(const Json& args){
        if(!view||!view->ready())return toolFailure("webpage_not_open");
        if(loginWindow){ShowWindow(loginWindow,SW_RESTORE);SetForegroundWindow(loginWindow);return toolSuccess({{"login_window_open",true},{"manual_login",true}});}
        const auto label=args.value("name",std::string("小电视"));if(label.size()>160)return toolFailure("login_label_invalid");
        const auto titleText="网页登录 · "+label+"（登录成功后自动返回小电视）";const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,titleText.data(),static_cast<int>(titleText.size()),nullptr,0);std::wstring title(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,titleText.data(),static_cast<int>(titleText.size()),title.data(),n);
        WNDCLASSW cls{};cls.lpfnWndProc=loginProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"HibernationTelevisionLogin";RegisterClassW(&cls);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(reinterpret_cast<HWND>(parent),MONITOR_DEFAULTTONEAREST),&monitor);const auto area=monitor.rcWork;
        const int width=std::min(1100L,std::max(640L,area.right-area.left-60)),height=std::min(820L,std::max(480L,area.bottom-area.top-60));
        loginWindow=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,nullptr,nullptr,cls.hInstance,this);
        if(!loginWindow)return toolFailure("login_window_unavailable");
        ++loginEpoch;authPending=false;presentationPending=false;nextAuth={};loginDismissed=false;loginReturnUrl=view->source();view->script(std::string(televisionBridgeSource)+".reset()",[](bool,std::string){});view->visible(false);
        if(!view->reparent(reinterpret_cast<std::uintptr_t>(loginWindow))){finishTelevisionLogin();return toolFailure("login_window_reparent_failed");}
        RECT client{};GetClientRect(loginWindow,&client);view->bounds(client.right,client.bottom);view->visible(true);
        view->script("document.querySelectorAll('video').forEach(v=>v.pause());true",[](bool,std::string){});
        ShowWindow(loginWindow,SW_SHOW);SetForegroundWindow(loginWindow);
        return toolSuccess({{"login_window_open",true},{"manual_login",true},{"shared_browser_profile",true},{"login_verified",false},{"note","请在大窗口中手动登录；确认登录成功后自动返回小电视。无法识别的网站仍可手动关闭窗口。"}});
    }
    void updateTelevisionLogin(){
        const auto now=std::chrono::steady_clock::now();
        if(!view||!view->ready()||authPending||now<nextAuth||(!loginWindow&&!televisionVisible))return;
        nextAuth=now+std::chrono::seconds(1);authPending=true;const auto epoch=loginEpoch;
        auto options=loginDetection;options["interval_ms"]=loginWindow?1500:15000;
        view->script(std::string(televisionBridgeSource)+".auth("+options.dump()+")",[this,epoch](bool ok,std::string text){
            if(epoch!=loginEpoch)return;authPending=false;
            const auto result=Json::parse(text,nullptr,false);if(!ok||!result.is_object()){loginState="unknown";return;}
            const auto state=result.value("state",std::string("unknown"));loginState=state=="signed_in"||state=="signed_out"?state:"unknown";loginMethod=result.value("method",std::string{});
            if(loginState=="signed_in"){if(loginWindow)finishTelevisionLogin(true,true);}
            else if(loginState=="signed_out"&&!loginWindow&&!loginDismissed&&televisionVisible)showTelevisionLogin(Json::object());
        });
    }
