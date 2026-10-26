#include "../PluginCommon.h"
#include <Hibernation/SmallWorld.h>
#include "DrawDeskObject.h"
#include <windowsx.h>
#include <random>
#include <array>
#include <optional>

using namespace hibernation;
using namespace hibernation::plugins;
namespace {
constexpr COLORREF transparent=RGB(255,0,255);
class Habitat final:public BasePlugin {
    PluginHostContext host_;PluginJson cfg_;mutable std::recursive_mutex mutex_;SmallWorld world_;
    HWND window_=nullptr;HBITMAP buffer_=nullptr;HINSTANCE instance_=nullptr;
    int width_=1920,height_=1080,actorSize_=112;bool visible_=true,paused_=false,busy_=false,input_=false,actor_=false,reducedMotion_=false,actorResting_=false;
    double actorX_=.1,actorY_=.5,nextSpawn_=12,nextAutonomy_=30,nextPaint_=0;std::int64_t actorSeen_=0;
    std::uint64_t sequence_=0,attentionEpoch_=0;std::mt19937 random_{std::random_device{}()};RECT previous_{};
    struct PaintItem {int x,y;deskAppearance::Frame frame;bool operator==(const PaintItem&)const=default;};
    std::optional<std::vector<PaintItem>> paintKey_;std::uint64_t paintCount_=0;
    std::string nextId(const char* prefix){return std::string(prefix)+"-"+std::to_string(nowMs())+"-"+std::to_string(sequence_++);}
    RECT objectRect(const PluginJson& item)const{const int x=static_cast<int>(item.at("x").get<double>()*(width_-actorSize_)+actorSize_*.65),y=static_cast<int>(item.at("y").get<double>()*(height_-actorSize_)+actorSize_*.9);return {x-26,y-(item.at("kind")=="sprout"?44:24),x+26,y+14};}
    void invalidate(){
        if(!window_)return;RECT dirty=previous_,current{};std::vector<PaintItem> key;const double time=world_.clock();
        for(const auto& item:world_.objects()){auto r=objectRect(item);key.push_back({(r.left+r.right)/2,r.bottom-3,deskAppearance::make(item.at("kind").get<std::string>(),item.at("growth"),time+item.at("x").get<double>()*2,reducedMotion_,time-item.at("born").get<double>())});InflateRect(&r,5,8);UnionRect(&current,&current,&r);}
        if(paintKey_&&*paintKey_==key)return;paintKey_=std::move(key);UnionRect(&dirty,&dirty,&current);if(!IsRectEmpty(&dirty))InvalidateRect(window_,&dirty,FALSE);previous_=current;
    }
    void create(){
        if(window_||!host_.desktopEnabled)return;width_=std::max(640,GetSystemMetrics(SM_CXVIRTUALSCREEN));height_=std::max(480,GetSystemMetrics(SM_CYVIRTUALSCREEN));
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance_;wc.hCursor=LoadCursor(nullptr,IDC_HAND);wc.lpszClassName=L"HibernationDeskObjects";RegisterClassW(&wc);
        window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_LAYERED,wc.lpszClassName,L"小爱 · 桌面小物",WS_POPUP,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),width_,height_,nullptr,nullptr,instance_,this);
        if(window_){SetLayeredWindowAttributes(window_,transparent,255,LWA_COLORKEY);ShowWindow(window_,SW_SHOWNOACTIVATE);}
    }
    std::pair<double,double> nearbyPoint(){
        double x=std::clamp(actorX_+(static_cast<int>(random_()%401)-200)/static_cast<double>(std::max(1,width_-actorSize_)),.02,.96),y=std::clamp(actorY_+(static_cast<int>(random_()%121)-60)/static_cast<double>(std::max(1,height_-actorSize_)),.03,.92);
        POINT p{GetSystemMetrics(SM_XVIRTUALSCREEN)+static_cast<LONG>(x*(width_-actorSize_)+actorSize_/2),GetSystemMetrics(SM_YVIRTUALSCREEN)+static_cast<LONG>(y*(height_-actorSize_)+actorSize_)};MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST),&monitor)){p.x=std::clamp(p.x,monitor.rcWork.left+actorSize_/2,monitor.rcWork.right-actorSize_/2);p.y=std::clamp(p.y,monitor.rcWork.top+actorSize_,monitor.rcWork.bottom-24);x=std::clamp((p.x-GetSystemMetrics(SM_XVIRTUALSCREEN)-actorSize_/2)/static_cast<double>(std::max(1,width_-actorSize_)),0.0,1.0);y=std::clamp((p.y-GetSystemMetrics(SM_YVIRTUALSCREEN)-actorSize_)/static_cast<double>(std::max(1,height_-actorSize_)),0.0,1.0);}return {x,y};
    }
    bool place(const std::string& kind){auto [x,y]=nearbyPoint();const bool ok=world_.place(nextId("object"),kind,x,y);if(ok)invalidate();return ok;}
    std::string request(const std::string& id,const std::string& action){if(paused_||!visible_)return "world_paused";if(actorResting_)return "world_character_resting";if(!actor_||!host_.desktopEnabled)return "desktop_character_required";return world_.request(id,action,nextId("world-action"));}
    void drain(){for(auto event:world_.takeEvents()){event["plugin"]="habitat";event["occurred_ms"]=nowMs();event["wake_model"]=(event.at("kind")=="habitat.consumed"||event.at("kind")=="habitat.planted")&&cfg_.value("wakeOnAction",false);host_.emit(std::move(event));}}
    void paint(){
        ++paintCount_;PAINTSTRUCT ps;auto target=BeginPaint(window_,&ps),dc=CreateCompatibleDC(target);if(!buffer_)buffer_=CreateCompatibleBitmap(target,width_,height_);auto old=SelectObject(dc,buffer_);auto paper=CreateSolidBrush(transparent);FillRect(dc,&ps.rcPaint,paper);DeleteObject(paper);
        const double time=world_.clock();for(const auto& item:world_.objects()){
            const auto r=objectRect(item);RECT overlap{};if(!IntersectRect(&overlap,&r,&ps.rcPaint))continue;
            const auto frame=deskAppearance::make(item.at("kind").get<std::string>(),item.at("growth"),time+item.at("x").get<double>()*2,reducedMotion_,time-item.at("born").get<double>());
            deskAppearance::draw(dc,frame,(r.left+r.right)/2,r.bottom-3);
        }
        BitBlt(target,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);SelectObject(dc,old);DeleteDC(dc);EndPaint(window_,&ps);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* a=reinterpret_cast<Habitat*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){a=static_cast<Habitat*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}if(!a)return DefWindowProcW(hwnd,message,w,l);std::lock_guard lock(a->mutex_);
        switch(message){case WM_MOUSEACTIVATE:return MA_NOACTIVATE;case WM_ERASEBKGND:return 1;case WM_PAINT:a->paint();return 0;
        case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&p);for(const auto& item:a->world_.objects()){auto r=a->objectRect(item);if(PtInRect(&r,p))return HTCLIENT;}return HTTRANSPARENT;}
        case WM_LBUTTONUP:{if(a->paused_||a->busy_||a->input_)return 0;POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};for(const auto& item:a->world_.objects()){auto r=a->objectRect(item);if(PtInRect(&r,p)){a->request(item.at("id"),SmallWorld::edible(item.at("kind"))?"eat":"look");break;}}a->drain();return 0;}
        case WM_SETTINGCHANGE:{BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);a->reducedMotion_=a->cfg_.value("reducedMotion",false)||!animations;a->invalidate();return 0;}
        case WM_DISPLAYCHANGE:a->world_.cancel("display_changed");a->width_=std::max(640,GetSystemMetrics(SM_CXVIRTUALSCREEN));a->height_=std::max(480,GetSystemMetrics(SM_CYVIRTUALSCREEN));if(a->buffer_){DeleteObject(a->buffer_);a->buffer_=nullptr;}SetWindowPos(hwnd,HWND_TOPMOST,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),a->width_,a->height_,SWP_NOACTIVATE);InvalidateRect(hwnd,nullptr,FALSE);a->drain();return 0;
        case WM_RBUTTONUP:{auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"放一颗浆果");AppendMenuW(menu,MF_STRING,2,L"放一颗种子");AppendMenuW(menu,MF_STRING,3,L"放一颗纸星");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4,L"暂时收起小物");POINT p;GetCursorPos(&p);SetForegroundWindow(hwnd);const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);if(choice>=1&&choice<=3)a->place(choice==1?"berry":choice==2?"seed":"paper_star");if(choice==4){a->visible_=false;a->world_.cancel("objects_hidden");ShowWindow(hwnd,SW_HIDE);}a->drain();return 0;}
        case WM_CLOSE:a->visible_=false;a->world_.cancel("objects_hidden");ShowWindow(hwnd,SW_HIDE);a->drain();return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
public:
    Habitat(){GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&anchor),&instance_);}
    static void anchor(){}
    const char* getName()const override{return "habitat";}
    const char* getDescription()const override{return "A small persistent world of desk objects and bounded autonomous actions";}
    std::vector<const char*> getDependencies()const override{return {"widget"};}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        try{configurationTypes(cfg,{},{"autoSpawn","autoInteract","wakeOnAction","reducedMotion"});const auto maximum=number(cfg,"maxObjects",3,1,8),lifetime=number(cfg,"lifetimeSeconds",600,30,43200);number(cfg,"spawnSeconds",120,30,3600);number(cfg,"interactionProbability",.35,0,1);std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);if(std::floor(maximum)!=maximum)throw std::runtime_error("object_count");world_.configure(static_cast<std::size_t>(maximum),lifetime);BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);reducedMotion_=cfg_.value("reducedMotion",false)||!animations;create();invalidate();issue.clear();return true;}catch(...){issue="habitat_configuration";return false;}
    }
    PluginJson tools()const override{return PluginJson::array({
        tool("habitat.observe","观察桌面虚拟物件、正在进行的动作与近期真实结果。",object()),
        tool("habitat.show","显示或收起桌面小物，不删除保存的虚拟物件。",object({{"visible",{{"type","boolean"}}}},PluginJson::array({"visible"}))),
        tool("habitat.place","在角色附近放一个受数量限制的虚拟小物件，不是现实文件或食物。",object({{"kind",{{"enum",PluginJson::array({"berry","leaf","seed","paper_star"})}}}},PluginJson::array({"kind"}))),
        tool("habitat.act","选择一件实际存在的小物，走过去看、吃或种下。返回accepted只是开始；完成另有真实回执。允许自行忽略或拒绝。",object({{"id",{{"type","string"},{"minLength",1},{"maxLength",96}}},{"action",{{"enum",PluginJson::array({"look","eat","plant"})}}}},PluginJson::array({"id","action"})))
    });}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::lock_guard lock(mutex_);if(name=="habitat.observe")return toolSuccess(world_.observe());if(name=="habitat.show"){if(!host_.desktopEnabled)return toolFailure("desktop_required");visible_=args.at("visible");if(!visible_)world_.cancel("objects_hidden");drain();return toolSuccess({{"visible",visible_}});}if(!host_.desktopEnabled)return toolFailure("desktop_required");if(!visible_)return toolFailure("desk_objects_hidden");
        if(name=="habitat.place"){if(!place(args.at("kind")))return toolFailure("world_object_limit");drain();return toolSuccess(world_.observe());}
        if(name=="habitat.act"){const auto error=request(args.at("id"),args.at("action"));if(!error.empty())return toolFailure(error.c_str());return toolSuccess({{"accepted",true},{"completed",false},{"pending",world_.action()}});}return toolFailure("tool_not_available");
    }
    void tick(double dt)override{
        std::lock_guard lock(mutex_);if(window_&&(IsWindowVisible(window_)!=FALSE)!=visible_)ShowWindow(window_,visible_?SW_SHOWNOACTIVATE:SW_HIDE);const bool actorAvailable=actor_&&nowMs()-actorSeen_<2500;
        world_.tick(dt,!paused_,actorX_,actorY_,actorAvailable);
        if(!paused_&&visible_&&!busy_&&!input_&&actorAvailable){
            if(cfg_.value("autoSpawn",true)&&world_.clock()>=nextSpawn_){if(world_.size()<static_cast<std::size_t>(cfg_.value("maxObjects",3)))place(std::array<const char*,4>{"berry","leaf","seed","paper_star"}[random_()%4]);nextSpawn_=world_.clock()+cfg_.value("spawnSeconds",120.0)*(.8+(random_()%401)/1000.0);}
            if(world_.queued()&&!actorResting_)world_.beginWalk();
            if(cfg_.value("autoInteract",true)&&!actorResting_&&!world_.pending()&&!world_.objects().empty()&&world_.clock()>=nextAutonomy_){nextAutonomy_=world_.clock()+30+random_()%31;if(std::bernoulli_distribution(cfg_.value("interactionProbability",.35))(random_)){std::vector<PluginJson> candidates;for(const auto& item:world_.objects())if(SmallWorld::newObservation(item)||(world_.wantsFood()&&SmallWorld::edible(item.at("kind"))))candidates.push_back(item);if(!candidates.empty()){const auto& item=candidates.at(random_()%candidates.size());request(item.at("id"),world_.wantsFood()&&SmallWorld::edible(item.at("kind"))?"eat":"look");}}}
        }
        drain();if(window_&&visible_&&world_.clock()>=nextPaint_){nextPaint_=world_.clock()+1.0/15;invalidate();}
    }
    void event(const PluginJson& event)override{
        std::lock_guard lock(mutex_);const auto kind=event.value("kind",std::string{});
        if(kind=="runtime.paused"){paused_=event.value("paused",false);return;}
        if(kind=="runtime.attention"){const bool busy=event.value("busy",false),input=event.value("input_active",false);const auto epoch=event.value("epoch",std::uint64_t(0));if((busy&&!busy_)||(input&&!input_)||epoch!=attentionEpoch_)world_.cancel("attention_changed");attentionEpoch_=epoch;busy_=busy;input_=input;drain();return;}
        if(kind=="widget.position"){actor_=event.value("active",false);actorX_=event.value("x",actorX_);actorY_=event.value("y",actorY_);actorSize_=event.value("size",actorSize_);actorResting_=event.value("state",std::string{})=="resting";actorSeen_=nowMs();return;}
        if(kind=="widget.arrived")world_.arrive(event.value("target_token",std::string{}),event.value("x",-1.0),event.value("y",-1.0));
        if(kind=="widget.interacted")world_.cancel("character_interrupted");if((kind=="widget.intent_cancelled"||kind=="widget.route_rejected")&&world_.pending()&&world_.action().at("token")==event.value("target_token",std::string{}))world_.cancel("character_interrupted");
        if(kind=="surface.show"&&event.value("surface",std::string{})=="habitat"){visible_=true;if(window_)ShowWindow(window_,SW_SHOWNOACTIVATE);invalidate();}
        drain();
    }
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto result=world_.snapshot();result["visible"]=visible_;result["desktop"]=host_.desktopEnabled;result["paint_count"]=paintCount_;return result;}
    bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);if(!world_.restore(value))return false;visible_=value.value("visible",true);nextSpawn_=world_.clock()+12;nextAutonomy_=world_.clock()+30;if(window_)ShowWindow(window_,visible_?SW_SHOWNOACTIVATE:SW_HIDE);invalidate();return true;}
    void shutdown()override{std::lock_guard lock(mutex_);if(window_){DestroyWindow(window_);window_=nullptr;}if(buffer_){DeleteObject(buffer_);buffer_=nullptr;}UnregisterClassW(L"HibernationDeskObjects",instance_);}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Habitat;}
