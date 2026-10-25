    struct Fade {HWND window;ULONGLONG started;DWORD duration;};std::vector<Fade> fades;
    ULONGLONG nextThinkingPaint=0;
    bool reducedMotion()const{BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);return !enabled||app.config().frontend.value("reducedMotion",false);}
    void fadeIn(HWND target,DWORD duration){
        if(reducedMotion()||IsWindowVisible(target))return;
        SetWindowLongPtrW(target,GWL_EXSTYLE,GetWindowLongPtrW(target,GWL_EXSTYLE)|WS_EX_LAYERED);SetLayeredWindowAttributes(target,0,180,LWA_ALPHA);
        std::erase_if(fades,[&](const auto& value){return value.window==target;});fades.push_back({target,GetTickCount64(),duration});
    }
    void tickTransitions(){
        const auto now=GetTickCount64();if(!fades.empty()){const bool reduce=reducedMotion();for(auto it=fades.begin();it!=fades.end();){
            const double progress=reduce?1:std::clamp((now-it->started)/static_cast<double>(it->duration),0.0,1.0);const auto alpha=static_cast<BYTE>(180+75*(1-(1-progress)*(1-progress)));
            if(IsWindow(it->window))SetLayeredWindowAttributes(it->window,0,alpha,LWA_ALPHA);
            if(progress>=1||!IsWindow(it->window))it=fades.erase(it);else ++it;
        }}
        if(busy&&!paused&&IsWindowVisible(window)&&now>=nextThinkingPaint&&!reducedMotion()){nextThinkingPaint=now+180;RECT r{};GetClientRect(window,&r);r.left=20;r.right-=20;r.top=r.bottom-151;r.bottom-=126;InvalidateRect(window,&r,FALSE);}
    }
