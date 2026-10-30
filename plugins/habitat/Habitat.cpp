#include "../PluginCommon.h"
#include <Hibernation/SmallWorld.h>
#include <Hibernation/DesktopSupplies.h>
#include <Hibernation/ForagingBridge.h>
#include <Hibernation/DropPixels.h>
#include <Hibernation/DesktopActivityArea.h>
#include <windowsx.h>
#include <random>
#include <array>
#include <optional>

using namespace hibernation;
using namespace hibernation::plugins;
namespace {

class Habitat final:public BasePlugin,public IForagingSource {
    PluginHostContext host_;PluginJson cfg_;mutable std::recursive_mutex mutex_;SmallWorld world_;
    HWND window_=nullptr;HBITMAP buffer_=nullptr;std::uint32_t* pixels_=nullptr;int surfaceLeft_=0,surfaceTop_=0,surfaceWidth_=0,surfaceHeight_=0;HINSTANCE instance_=nullptr;
    int width_=1920,height_=1080,actorSize_=112;PluginJson canteenDeliveries_=PluginJson::object();bool canteenSupplies_=false;bool economyFallback_=false;bool autonomous_=false,visible_=true,paused_=false,busy_=false,input_=false,actor_=false,reducedMotion_=false,actorResting_=false;
    double actorX_=.1,actorY_=.5,nextSpawn_=12,nextAutonomy_=30,nextPaint_=0;std::int64_t actorSeen_=0;
    std::uint64_t sequence_=0,attentionEpoch_=0;std::mt19937 random_{std::random_device{}()};double lifetimeMin_=300,lifetimeMax_=600;std::vector<double> creations_;
    struct Retiring {PluginJson item;double born;double opacity;};std::vector<Retiring> retiring_;
    struct PaintItem {int x,y;deskAppearance::Frame frame;unsigned alpha;bool operator==(const PaintItem&)const=default;};
    std::optional<std::vector<PaintItem>> paintKey_;std::uint64_t paintCount_=0;
    std::string nextId(const char* prefix){return std::string(prefix)+"-"+std::to_string(nowMs())+"-"+std::to_string(sequence_++);}
    #include "HabitatForaging.inc"
    RECT objectRect(const PluginJson& item)const{const int x=static_cast<int>(item.at("x").get<double>()*(width_-actorSize_)+actorSize_*.65),y=static_cast<int>(item.at("y").get<double>()*(height_-actorSize_)+actorSize_*.9);return {x-26,y-(item.at("kind")=="sprout"?44:24),x+26,y+14};}
    void clearBuffer(){if(buffer_)DeleteObject(buffer_);buffer_=nullptr;pixels_=nullptr;surfaceWidth_=surfaceHeight_=0;}
    void invalidate(){
        if(!window_)return;RECT bounds{};std::vector<PaintItem> key;const double time=world_.clock();
        auto add=[&](const PluginJson& item,double opacity){auto r=objectRect(item);const auto definition=item.at("definition");const auto frame=deskAppearance::make(item.at("kind").get<std::string>(),item.at("growth"),time+item.at("x").get<double>()*2,reducedMotion_,time-item.at("born").get<double>(),definition.at("tint"),item.at("rules").at("quality")==2);const unsigned alpha=static_cast<unsigned>(std::round(std::clamp(opacity,0.0,1.0)*255));if(!alpha)return;key.push_back({(r.left+r.right)/2,r.bottom-3,frame,alpha});r.top-=32;InflateRect(&r,8,12);UnionRect(&bounds,&bounds,&r);};
        for(const auto& item:world_.objects())add(item,drops::opacity(item,time));
        for(const auto& ghost:retiring_)add(ghost.item,ghost.opacity*std::clamp(1-(time-ghost.born)/.55,0.0,1.0));
        if(paintKey_&&*paintKey_==key)return;paintKey_=key;++paintCount_;
        const int width=std::max(1,static_cast<int>(bounds.right-bounds.left)),height=std::max(1,static_cast<int>(bounds.bottom-bounds.top));
        if(width!=surfaceWidth_||height!=surfaceHeight_){clearBuffer();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;buffer_=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels_),nullptr,0);if(!buffer_){paintKey_.reset();return;}surfaceWidth_=width;surfaceHeight_=height;}
        surfaceLeft_=bounds.left;surfaceTop_=bounds.top;std::fill_n(pixels_,static_cast<std::size_t>(width)*height,0u);
        for(const auto& item:key)deskAppearance::raster(pixels_,width,height,item.frame,item.x-surfaceLeft_,item.y-surfaceTop_,item.alpha);
        auto dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,buffer_);POINT destination{GetSystemMetrics(SM_XVIRTUALSCREEN)+surfaceLeft_,GetSystemMetrics(SM_YVIRTUALSCREEN)+surfaceTop_},source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(window_,nullptr,&destination,&size,dc,&source,0,&blend,ULW_ALPHA);SelectObject(dc,old);DeleteDC(dc);
    }
    void create(){
        if(window_||!host_.desktopEnabled)return;width_=std::max(640,GetSystemMetrics(SM_CXVIRTUALSCREEN));height_=std::max(480,GetSystemMetrics(SM_CYVIRTUALSCREEN));
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance_;wc.hCursor=LoadCursor(nullptr,IDC_HAND);wc.lpszClassName=L"HibernationDeskObjects";RegisterClassW(&wc);
        window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_LAYERED,wc.lpszClassName,L"小爱 · 桌面小物",WS_POPUP,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),width_,height_,nullptr,nullptr,instance_,this);
        if(window_)ShowWindow(window_,SW_SHOWNOACTIVATE);
    }
    std::pair<double,double> placementPoint(const std::string& placement){
        const auto areas=desktopActivityAreas(width_,height_,actorSize_);std::pair<double,double> point;
        if(placement=="desktop")point=sampleActivityPoint(areas,random_);
        else{double x=actorX_*(width_-actorSize_),y=actorY_*(height_-actorSize_);
            x+=placement=="left"?-140:placement=="right"?140:std::uniform_real_distribution<double>(-200,200)(random_);
            y+=std::uniform_real_distribution<double>(-60,60)(random_);point=constrainActivityPoint(areas,x,y);}
        return {point.first/std::max(1,width_-actorSize_),point.second/std::max(1,height_-actorSize_)};
    }
    PluginJson policy()const{return {{"max_objects",cfg_.value("maxObjects",6)},{"lifetime_min_seconds",lifetimeMin_},{"lifetime_max_seconds",lifetimeMax_},{"creations_per_minute",cfg_.value("creationsPerMinute",12)},{"templates",drops::kinds()},{"custom_attributes","definition is descriptive only; rules, rarity and actual lifetime are program-owned"}};}
    PluginJson observation()const{auto result=world_.observe();affordances(result["objects"]);result["guide"]=result.value("guide",std::string{})+" 实际可用动作见available_actions，能否直接食用见can_eat；kind和rules保留外观模板的基础属性。";result["creation_policy"]=policy();result["regions"]=regionConfig_;result["gather"]=gather_;result["last_gather"]=lastGather_;result["inventory_available"]=inventoryReady_;return result;}
    PluginJson createDrop(const PluginJson& args,const std::string& creator){
        if(paused_)return toolFailure("world_paused");const std::string kind=args.at("kind");if(!drops::known(kind)||kind=="sprout")return toolFailure("world_template_invalid");
        const auto authored=drops::definition(kind,args.value("definition",PluginJson::object()));if(!drops::validDefinition(authored))return toolFailure("world_definition_invalid");
        const double lo=args.value("lifetime_min_seconds",lifetimeMin_),hi=args.value("lifetime_max_seconds",lifetimeMax_);if(!std::isfinite(lo)||!std::isfinite(hi)||lo<lifetimeMin_||hi>lifetimeMax_||lo>hi)return toolFailure("world_lifetime_policy");
        if(world_.size()>=static_cast<std::size_t>(cfg_.value("maxObjects",6)))return toolFailure("world_object_limit");std::erase_if(creations_,[&](double at){return world_.clock()-at>=60;});if(creations_.size()>=static_cast<std::size_t>(cfg_.value("creationsPerMinute",12)))return toolFailure("world_creation_rate");
        const auto placement=args.value("placement",std::string("desktop"));if(placement!="desktop"&&placement!="nearby"&&placement!="left"&&placement!="right")return toolFailure("world_placement_invalid");auto [x,y]=placementPoint(placement);
        const int roll=static_cast<int>(random_()%100),quality=roll<5?2:roll<25?1:0;const auto id=nextId("object");if(!world_.placeDetailed(id,kind,x,y,authored,std::uniform_real_distribution<double>(lo,hi)(random_),quality,creator))return toolFailure("world_definition_invalid");creations_.push_back(world_.clock());invalidate();return toolSuccess({{"object",world_.object(id)},{"creation_policy",policy()}});
    }
    void deliverMeal(const PluginJson& request){
        const auto ticket=request.value("ticket",std::string{});if(ticket.empty()||ticket.size()>80)return;
        const auto respond=[&](bool ok,const std::string& detail){PluginJson receipt={{"kind","habitat.canteen_delivery"},{"plugin","habitat"},{"ticket",ticket},{"ok",ok},{"occurred_ms",nowMs()}};receipt[ok?"object_id":"error"]=detail;host_.emit(receipt);};
        try{const auto& batch=request.at("batch");const auto batchId=batch.at("id").get<std::string>();
            if(canteenDeliveries_.contains(ticket)){const auto& prior=canteenDeliveries_.at(ticket);respond(prior.at("batch_id")==batchId,prior.at("batch_id")==batchId?prior.at("object_id").get<std::string>():"delivery_identity_conflict");return;}
            if(!host_.desktopEnabled||!visible_||paused_||!canteenSupplies_){respond(false,"desktop_unavailable");return;}if(world_.size()>=static_cast<std::size_t>(cfg_.value("maxObjects",6))){respond(false,"world_object_limit");return;}
            const std::string recipe=batch.at("recipe_id");const auto* dish=bsimmer::cuisine::catalog().dish(recipe);const std::string kind=dish?dish->presentation:std::string{};if(!dish||dish->kind!="dish"||kind.empty()||batch.at("template")!=kind||batch.at("name")!=dish->name){respond(false,"meal_recipe_invalid");return;}
            const double life=std::min(lifetimeMax_,request.at("remaining_seconds").get<double>());if(!std::isfinite(life)||life<5||life>43200){respond(false,"meal_expired");return;}
            const auto id="food-"+ticket;const auto [x,y]=placementPoint("nearby");const PluginJson definition={{"name",batch.at("name")},{"description",batch.at("sensory").at("texture")},{"tint","original"}};
            const PluginJson provenance={{"type","canteen_meal"},{"batch_id",batchId},{"recipe_id",recipe},{"origin",batch.at("origin")},{"sensory",batch.at("sensory")}};
            if(!world_.placeDetailed(id,kind,x,y,definition,life,0,"canteen",provenance)){respond(false,"meal_publication_rejected");return;}
            canteenDeliveries_[ticket]={{"batch_id",batchId},{"object_id",id}};while(canteenDeliveries_.size()>256)canteenDeliveries_.erase(canteenDeliveries_.begin());invalidate();drain();respond(true,id);
        }catch(...){respond(false,"meal_publication_invalid");}
    }
    bool place(const std::string& kind){return createDrop({{"kind",kind},{"placement","desktop"}},"program").value("ok",false);}
    std::string request(const std::string& id,const std::string& action){if(action=="collect"&&(!inventoryReady_||!canteenSupplies_))return "resource_inventory_unavailable";if(action=="collect"&&supplies::resource(world_.object(id)).empty())return "resource_not_collectible";if(action=="eat"){const auto item=world_.object(id);if(item.is_object()){const auto resource=supplies::resource(item);if(!resource.empty())for(const auto& r:supplies::catalog())if(r.id==resource&&!r.edible)return "world_object_not_food";}}if(paused_||!visible_)return "world_paused";if(actorResting_)return "world_character_resting";if(!actor_||!host_.desktopEnabled)return "desktop_character_required";return world_.request(id,action,nextId("world-action"));}
    void drain(){for(auto event:world_.takeEvents()){const auto kind=event.at("kind");if(kind=="habitat.collected"&&!gather_.is_null())event["gather_id"]=gather_.at("id");if(kind=="habitat.action_cancelled"&&!gather_.is_null())finishGather("interrupted");if(kind=="habitat.consumed"||kind=="habitat.collected"||kind=="habitat.planted"){auto item=kind=="habitat.planted"?event.at("previous_item"):event.at("item");retiring_.push_back({item,world_.clock(),drops::opacity(item,world_.clock())});if(retiring_.size()>8)retiring_.erase(retiring_.begin());}event["plugin"]="habitat";event["occurred_ms"]=nowMs();event["wake_model"]=(kind=="habitat.consumed"||kind=="habitat.planted")&&cfg_.value("wakeOnAction",false);host_.emit(std::move(event));}}
    void paint(HDC target){if(!buffer_||!target)return;auto dc=CreateCompatibleDC(target);auto old=SelectObject(dc,buffer_);BitBlt(target,0,0,surfaceWidth_,surfaceHeight_,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteDC(dc);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* a=reinterpret_cast<Habitat*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){a=static_cast<Habitat*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}if(!a)return DefWindowProcW(hwnd,message,w,l);std::lock_guard lock(a->mutex_);
        switch(message){case WM_MOUSEACTIVATE:return MA_NOACTIVATE;case WM_ERASEBKGND:return 1;case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);a->paint(dc);EndPaint(hwnd,&ps);return 0;}case WM_PRINTCLIENT:a->paint(reinterpret_cast<HDC>(w));return 0;
        case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&p);p.x+=a->surfaceLeft_;p.y+=a->surfaceTop_;for(const auto& item:a->world_.objects()){auto r=a->objectRect(item);if(PtInRect(&r,p))return HTCLIENT;}return HTTRANSPARENT;}
        case WM_LBUTTONUP:{if(a->paused_||a->busy_||a->input_)return 0;POINT p{GET_X_LPARAM(l)+a->surfaceLeft_,GET_Y_LPARAM(l)+a->surfaceTop_};for(const auto& item:a->world_.objects()){auto r=a->objectRect(item);if(PtInRect(&r,p)){a->request(item.at("id"),a->inventoryReady_&&!supplies::resource(item).empty()?"collect":SmallWorld::edible(item.at("kind"))?"eat":"look");break;}}a->drain();return 0;}
        case WM_SETTINGCHANGE:{BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);a->reducedMotion_=a->cfg_.value("reducedMotion",false)||!animations;a->invalidate();return 0;}
        case WM_DISPLAYCHANGE:a->world_.cancel("display_changed");a->width_=std::max(640,GetSystemMetrics(SM_CXVIRTUALSCREEN));a->height_=std::max(480,GetSystemMetrics(SM_CYVIRTUALSCREEN));a->clearBuffer();a->paintKey_.reset();a->invalidate();a->drain();return 0;
        case WM_RBUTTONUP:{auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"放一颗浆果");AppendMenuW(menu,MF_STRING,2,L"放一颗种子");AppendMenuW(menu,MF_STRING,3,L"放一颗纸星");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4,L"暂时收起小物");POINT p;GetCursorPos(&p);SetForegroundWindow(hwnd);const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);if(choice>=1&&choice<=3)a->place(choice==1?"berry":choice==2?"seed":"paper_star");if(choice==4){a->visible_=false;a->world_.cancel("objects_hidden");ShowWindow(hwnd,SW_HIDE);}a->drain();return 0;}
        case WM_CLOSE:a->visible_=false;a->world_.cancel("objects_hidden");ShowWindow(hwnd,SW_HIDE);a->drain();return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
public:
    Habitat(){GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&anchor),&instance_);}
    static void anchor(){}
    const char* getName()const override{return "habitat";}
    const char* getVersion()const override{return "0.4.0";}
    const char* getDescription()const override{return "A small persistent world of desk objects and bounded autonomous actions";}
    std::vector<const char*> getDependencies()const override{return {"widget"};}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        try{configurationTypes(cfg,{},{"autoSpawn","autoInteract","wakeOnAction","reducedMotion","resourceSpawn"});const auto maximum=number(cfg,"maxObjects",6,1,8),lifetime=number(cfg,"lifetimeSeconds",600,30,43200);lifetimeMin_=number(cfg,"lifetimeMinSeconds",std::min(300.0,lifetime),5,43200);lifetimeMax_=number(cfg,"lifetimeMaxSeconds",lifetime,5,43200);if(lifetimeMin_>lifetimeMax_)throw std::runtime_error("lifetime_range");const auto regionConfig=cfg.value("regions",supplies::defaultRegions());const auto regionRules=supplies::regions(regionConfig);const auto rate=number(cfg,"creationsPerMinute",12,1,60);if(std::floor(rate)!=rate)throw std::runtime_error("creation_rate");number(cfg,"spawnSeconds",120,30,3600);number(cfg,"interactionProbability",.35,0,1);std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);if(regionConfig_!=regionConfig){nextResources_.clear();world_.cancel("regions_changed");finishGather("regions_changed");}regions_=regionRules;regionConfig_=regionConfig;if(std::floor(maximum)!=maximum)throw std::runtime_error("object_count");world_.configure(static_cast<std::size_t>(maximum),lifetime);BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);reducedMotion_=cfg_.value("reducedMotion",false)||!animations;create();invalidate();issue.clear();return true;}catch(...){issue="habitat_configuration";return false;}
    }
    PluginJson tools()const override{return PluginJson::array({
        tool("habitat.gather","自主收集桌面食材。可用region或ingredient_id筛选，count为1至8；recipe_id按食堂实际缺料收集，不可与ingredient_id同用。程序选择路线并逐件拾取；无目标会保留部分成果后结束。destination可为backpack或pantry；accepted只是开始，gather_finished才是结果。",object({{"region",{{"type","string"},{"maxLength",40}}},{"ingredient_id",{{"type","string"},{"maxLength",32}}},{"recipe_id",{{"type","string"},{"minLength",1},{"maxLength",64}}},{"count",{{"type","integer"},{"minimum",1},{"maximum",8}}},{"destination",{{"enum",PluginJson::array({"backpack","pantry"})}}}})),
        tool("habitat.observe","观察桌面物件、食材和真实动作结果。available_actions是当前支持的动作，can_eat表示实际可直接食用性。",object()),
        tool("habitat.show","显示或收起桌面小物，不删除保存的虚拟物件。",object({{"visible",{{"type","boolean"}}}},PluginJson::array({"visible"}))),
        tool("habitat.place","在可活动的屏幕工作区内放一个受数量限制的虚拟小物件，不是现实文件或食物。",object({{"kind",{{"enum",drops::kinds()}}}},PluginJson::array({"kind"}))),
        tool("habitat.arrange","走近一件实际存在的小物，把它缓慢挪到桌面相对坐标x/y（0至1）。可排列、留空或布置小场景；不会延长寿命或改变属性。accepted只是开始，完成另有回执。",object({{"id",{{"type","string"},{"minLength",1},{"maxLength",96}}},{"x",{{"type","number"},{"minimum",0},{"maximum",1}}},{"y",{{"type","number"},{"minimum",0},{"maximum",1}}}},PluginJson::array({"id","x","y"}))),
        tool("habitat.create","创造桌面上的虚拟食物或掉落物，可命名、描述、设定颜色和描述性特征。先observe了解模板和寿命策略；实际稀有度、可食用性和数值由程序决定。成功回执才表示实际创建。",object({{"kind",{{"type","string"},{"enum",drops::kinds()}}},{"definition",drops::definitionSchema()},{"placement",{{"type","string"},{"enum",PluginJson::array({"desktop","nearby","left","right"})}}},{"lifetime_min_seconds",{{"type","number"},{"minimum",5},{"maximum",43200}}},{"lifetime_max_seconds",{{"type","number"},{"minimum",5},{"maximum",43200}}}},PluginJson::array({"kind"}))),
        tool("habitat.act","选择实际小物，走近后看、吃、种下或收进背包(collect)。优先用gather表达收集目的，让程序选目标。accepted只是开始，完成另有真实回执。",object({{"id",{{"type","string"},{"minLength",1},{"maxLength",96}}},{"action",{{"enum",PluginJson::array({"look","eat","plant","collect"})}}}},PluginJson::array({"id","action"})))
    });}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::lock_guard lock(mutex_);if(name=="habitat.observe")return toolSuccess(observation());if(name=="habitat.gather")return startGather(args);if(name=="habitat.show"){if(!host_.desktopEnabled)return toolFailure("desktop_required");visible_=args.at("visible");if(!visible_)world_.cancel("objects_hidden");drain();return toolSuccess({{"visible",visible_}});}if(!host_.desktopEnabled)return toolFailure("desktop_required");if(!visible_)return toolFailure("desk_objects_hidden");
        if(name=="habitat.create"||name=="habitat.place"){auto result=createDrop(args,"model");drain();if(name=="habitat.place"&&result.value("ok",false))return toolSuccess(observation());return result;}
        if(name=="habitat.arrange"){
            if(paused_||actorResting_)return toolFailure("world_character_unavailable");if(!actor_)return toolFailure("desktop_character_required");
            const auto point=constrainActivityPoint(desktopActivityAreas(width_,height_,actorSize_),args.at("x").get<double>()*(width_-actorSize_),args.at("y").get<double>()*(height_-actorSize_));const auto error=world_.requestArrangement(args.at("id"),point.first/std::max(1,width_-actorSize_),point.second/std::max(1,height_-actorSize_),nextId("world-action"));if(!error.empty())return toolFailure(error.c_str());return toolSuccess({{"accepted",true},{"completed",false},{"pending",world_.action()}});
        }
        if(name=="habitat.act"){const auto error=request(args.at("id"),args.at("action"));if(!error.empty())return toolFailure(error.c_str());return toolSuccess({{"accepted",true},{"completed",false},{"pending",world_.action()}});}return toolFailure("tool_not_available");
    }
    void tick(double dt)override{
        std::lock_guard lock(mutex_);if(window_&&(IsWindowVisible(window_)!=FALSE)!=visible_)ShowWindow(window_,visible_?SW_SHOWNOACTIVATE:SW_HIDE);const bool actorAvailable=actor_&&nowMs()-actorSeen_<2500;
        world_.tick(dt,!paused_,actorX_,actorY_,actorAvailable);updateRegions();std::erase_if(retiring_,[&](const Retiring& ghost){return world_.clock()-ghost.born>=.55;});
        if(!paused_&&visible_)spawnResources();
        if(!paused_&&visible_&&!busy_&&!input_&&actorAvailable){
            advanceGather();
            if(cfg_.value("autoSpawn",true)&&world_.clock()>=nextSpawn_){if(world_.size()<static_cast<std::size_t>(cfg_.value("maxObjects",6))){const std::string selected=drops::kinds().at(random_()%drops::kinds().size());if(!canteenSupplies_||!SmallWorld::edible(selected))place(selected);}nextSpawn_=world_.clock()+cfg_.value("spawnSeconds",120.0)*(.8+(random_()%401)/1000.0);}
            if(world_.queued()&&!actorResting_)world_.beginWalk();
            if((!autonomous_||economyFallback_)&&cfg_.value("autoInteract",true)&&!actorResting_&&!world_.pending()&&gather_.is_null()&&!world_.objects().empty()&&world_.clock()>=nextAutonomy_){nextAutonomy_=world_.clock()+30+random_()%31;if(std::bernoulli_distribution(cfg_.value("interactionProbability",.35))(random_)){std::vector<PluginJson> candidates;for(const auto& item:world_.objects())if(SmallWorld::newObservation(item)||(world_.wantsFood()&&SmallWorld::edible(item.at("kind"))))candidates.push_back(item);if(!candidates.empty()){const auto& item=candidates.at(random_()%candidates.size());request(item.at("id"),world_.wantsFood()&&SmallWorld::edible(item.at("kind"))?"eat":"look");}}}
        }
        drain();if(window_&&visible_&&world_.clock()>=nextPaint_){nextPaint_=world_.clock()+1.0/15;invalidate();}
    }
    void event(const PluginJson& event)override{
        std::lock_guard lock(mutex_);const auto kind=event.value("kind",std::string{});
        if(kind=="runtime.canteen.available"){canteenSupplies_=event.value("enabled",false);if(!canteenSupplies_){inventoryReady_=false;world_.cancel("inventory_disabled");finishGather("inventory_disabled");}return;}
        if(kind=="runtime.supplies.status"){inventoryReady_=event.value("available",false);menu_=event.value("menu",PluginJson::array());pantry_=event.value("pantry",PluginJson::array());return;}
        if(kind=="runtime.supplies.consumed"){world_.consumeInventoryNutrition(event.value("nutrition",0.0));return;}
        if(kind=="runtime.canteen.deliver"){deliverMeal(event.at("request"));return;}
        if(kind=="runtime.autonomy_fallback"){economyFallback_=event.value("enabled",false);return;}
        if(kind=="runtime.autonomy"){autonomous_=event.value("enabled",false);return;}
        if(kind=="runtime.activity_cancel"){world_.cancel("activity_cancelled");finishGather("interrupted");drain();return;}
        if(kind=="runtime.paused"){paused_=event.value("paused",false);if(paused_){world_.cancel("runtime_paused");finishGather("interrupted");}return;}
        if(kind=="runtime.attention"){const bool busy=event.value("busy",false),input=event.value("input_active",false);const auto epoch=event.value("epoch",std::uint64_t(0));if((busy&&!busy_)||(input&&!input_)||epoch!=attentionEpoch_){world_.cancel("attention_changed");finishGather("interrupted");}attentionEpoch_=epoch;busy_=busy;input_=input;drain();return;}
        if(kind=="widget.position"){actor_=event.value("active",false);actorX_=event.value("x",actorX_);actorY_=event.value("y",actorY_);actorSize_=event.value("size",actorSize_);actorResting_=event.value("state",std::string{})=="resting";actorSeen_=nowMs();return;}
        if(kind=="widget.arrived")world_.arrive(event.value("target_token",std::string{}),event.value("x",-1.0),event.value("y",-1.0));
        if(kind=="widget.interacted"){world_.cancel("character_interrupted");finishGather("interrupted");}if((kind=="widget.intent_cancelled"||kind=="widget.route_rejected")&&world_.pending()&&world_.action().at("token")==event.value("target_token",std::string{}))world_.cancel("character_interrupted");
        if(kind=="surface.show"&&event.value("surface",std::string{})=="habitat"){visible_=true;if(window_)ShowWindow(window_,SW_SHOWNOACTIVATE);invalidate();}
        drain();
    }
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto result=world_.snapshot();affordances(result["objects"]);result["resource_schedule"]=nextResources_;result["regions"]=regionConfig_;result["gather"]=gather_;result["last_gather"]=lastGather_;result["inventory_available"]=inventoryReady_;result["canteen_deliveries"]=canteenDeliveries_;result["canteen_supplies"]=canteenSupplies_;result["visible"]=visible_;result["desktop"]=host_.desktopEnabled;result["paint_count"]=paintCount_;result["alpha_rendering"]=true;result["surface_left"]=surfaceLeft_;result["surface_top"]=surfaceTop_;result["surface_width"]=surfaceWidth_;result["surface_height"]=surfaceHeight_;result["retiring_count"]=retiring_.size();result["creation_policy"]=policy();return result;}
    bool restore(const PluginJson& value)override{std::lock_guard lock(mutex_);const auto receipts=value.value("canteen_deliveries",PluginJson::object());if(!receipts.is_object()||receipts.size()>256)return false;for(auto it=receipts.begin();it!=receipts.end();++it)if(it.key().empty()||it.key().size()>80||!it.value().is_object()||!it.value().contains("batch_id")||!it.value().contains("object_id")||!it.value().at("batch_id").is_string()||!it.value().at("object_id").is_string())return false;const auto schedule=value.value("resource_schedule",PluginJson::object());if(!schedule.is_object()||schedule.size()>8)return false;for(const auto& time:schedule)if(!time.is_number()||!std::isfinite(time.get<double>())||time.get<double>()<0||time.get<double>()>1e10)return false;if(!world_.restore(value))return false;canteenDeliveries_=receipts;gather_=nullptr;lastGather_=nullptr;nextResources_.clear();for(const auto& r:regions_)if(schedule.contains(r.id))nextResources_[r.id]=schedule.at(r.id);updateRegions();retiring_.clear();paintKey_.reset();visible_=value.value("visible",true);nextSpawn_=world_.clock()+12;nextAutonomy_=world_.clock()+30;if(window_)ShowWindow(window_,visible_?SW_SHOWNOACTIVATE:SW_HIDE);invalidate();return true;}
    void shutdown()override{std::lock_guard lock(mutex_);if(window_){DestroyWindow(window_);window_=nullptr;}clearBuffer();UnregisterClassW(L"HibernationDeskObjects",instance_);}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Habitat;}
