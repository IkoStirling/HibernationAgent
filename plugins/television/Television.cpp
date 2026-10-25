#include "../PluginCommon.h"
#include <wincodec.h>
#include <AYVideo/VideoPlayer.h>
#include <AYVideo/IVideoFrameTexture.h>
#include <AYAudio/AudioEngine.h>
#include <windowsx.h>
#include <commctrl.h>
#include <Hibernation/TelevisionLayout.h>
#include <mmsystem.h>
#include <wincrypt.h>
#include <deque>
#include <future>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <fstream>
#include <sstream>
#include <set>
using namespace hibernation;
using namespace hibernation::plugins;
using ayt::video::VideoResult;
namespace {
template<class T>struct ComPtr{T* p=nullptr;~ComPtr(){if(p)p->Release();}T** put(){return &p;}T* operator->()const{return p;}};
std::string jpeg(const std::vector<uint8_t>& pixels,int width,int height){if(pixels.empty()||width<=0||height<=0)return {};const auto co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> bitmap;ComPtr<IWICBitmapScaler> scale;ComPtr<IWICFormatConverter> conversion;ComPtr<IStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;std::string encoded;
    do{if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put()))))break;if(FAILED(factory->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppBGRA,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data()),bitmap.put())))break;if(FAILED(factory->CreateBitmapScaler(scale.put())))break;const int outputWidth=std::min(width,480),outputHeight=std::max(1,height*outputWidth/width);if(FAILED(scale->Initialize(bitmap.p,outputWidth,outputHeight,WICBitmapInterpolationModeFant)))break;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,stream.put())))break;if(FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,encoder.put()))||FAILED(encoder->Initialize(stream.p,WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(frame.put(),nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(outputWidth,outputHeight)))break;WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;if(FAILED(factory->CreateFormatConverter(conversion.put()))||FAILED(conversion->Initialize(scale.p,GUID_WICPixelFormat24bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))break;if(FAILED(frame->SetPixelFormat(&format))||FAILED(frame->WriteSource(conversion.p,nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit()))break;STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart>256*1024)break;HGLOBAL global=nullptr;if(FAILED(GetHGlobalFromStream(stream.p,&global)))break;auto* bytes=static_cast<BYTE*>(GlobalLock(global));if(!bytes)break;DWORD length=0;CryptBinaryToStringA(bytes,stat.cbSize.LowPart,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&length);encoded.resize(length);if(!CryptBinaryToStringA(bytes,stat.cbSize.LowPart,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,encoded.data(),&length))encoded.clear();else if(!encoded.empty()&&encoded.back()=='\0')encoded.pop_back();GlobalUnlock(global);
    }while(false);
    // COM objects are released before uninitializing the apartment below.
    if(frame.p){frame.p->Release();frame.p=nullptr;}if(encoder.p){encoder.p->Release();encoder.p=nullptr;}if(stream.p){stream.p->Release();stream.p=nullptr;}if(conversion.p){conversion.p->Release();conversion.p=nullptr;}if(scale.p){scale.p->Release();scale.p=nullptr;}if(bitmap.p){bitmap.p->Release();bitmap.p=nullptr;}if(factory.p){factory.p->Release();factory.p=nullptr;}if(SUCCEEDED(co))CoUninitialize();return encoded;
}
class Television final:public BasePlugin {
    bool webMode_=false;std::string pageUrl_,pagePlatform_;
    struct Command{PluginJson value;std::shared_ptr<std::promise<PluginJson>> answer;std::shared_ptr<std::atomic<bool>> cancelled;};
    PluginHostContext host_;PluginJson cfg_;mutable std::recursive_mutex mutex_;std::mutex commandMutex_;std::condition_variable cv_;std::deque<Command> commands_;std::thread worker_;std::atomic<bool> stopped_=false;
    ayt::video::IAYVideoDemuxer* session_=nullptr;bool dirty_=true,previewPending_=false;HBITMAP buffer_=nullptr;int bufferWidth_=0,bufferHeight_=0;std::string syncSource_="EngineClock";HWND window_=nullptr;HINSTANCE instance_{};HFONT font_=nullptr;bool visible_=false,playing_=false,audioAvailable_=false;double position_=0,duration_=0;std::string source_,title_,state_="idle",lastError_;std::vector<uint8_t> pixels_;int frameWidth_=0,frameHeight_=0;double framePts_=0;int volume_=65;
    void emit(PluginJson event){std::function<void(PluginJson)> callback;{std::lock_guard lock(mutex_);callback=host_.emit;}event["plugin"]="television";if(callback)callback(std::move(event));}

    static bool within(const std::filesystem::path& root,const std::filesystem::path& candidate){
        auto r=root.begin(),p=candidate.begin();
        for(;r!=root.end();++r,++p)if(p==candidate.end()||CompareStringOrdinal(r->c_str(),-1,p->c_str(),-1,TRUE)!=CSTR_EQUAL)return false;
        return true;
    }
    std::vector<std::pair<std::string,std::filesystem::path>> mediaRoots(){std::lock_guard lock(mutex_);return {{"materials",host_.materialsRoot.empty()?host_.dataRoot/"materials":host_.materialsRoot},{"drafts",host_.draftsRoot.empty()?host_.dataRoot/"drafts":host_.draftsRoot}};}
    static bool videoFile(const std::filesystem::path& path){auto ext=pathUtf8(path.extension());std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return ext==".mp4"||ext==".mkv"||ext==".webm"||ext==".avi"||ext==".mov"||ext==".mpeg"||ext==".mpg"||ext==".ts"||ext==".ogv"||ext==".flv"||ext==".wmv";}
    std::string mediaSource(std::string source){
        if(source.empty()||source.size()>2048||source.find_first_of("\r\n")!=std::string::npos||source.find('\0')!=std::string::npos)return {};
        if(source.starts_with("https://")||source.starts_with("http://")){const auto start=source.find("://")+3,slash=source.find_first_of("/?#",start);const auto origin=source.substr(0,slash);if(!ayt::net::http::validOrigin(origin)||origin.find('@')!=std::string::npos)return {};return source;}
        std::replace(source.begin(),source.end(),'\\','/');auto requested=std::filesystem::u8path(source);
        std::string found;
        for(const auto& [mount,rawRoot]:mediaRoots()){
            std::error_code ec;const auto root=std::filesystem::canonical(rawRoot,ec);if(ec)continue;
            auto path=requested;
            if(!requested.is_absolute()){
                if(source.starts_with("materials/")||source.starts_with("drafts/")){if(!source.starts_with(mount+"/"))continue;path=std::filesystem::u8path(source.substr(mount.size()+1));}
                path=root/path;
            }
            const auto candidate=std::filesystem::canonical(path,ec);
            if(ec||!within(root,candidate)||!std::filesystem::is_regular_file(candidate,ec)||ec||!videoFile(candidate))continue;
            const auto value=pathUtf8(candidate);if(!found.empty()&&found!=value)return {};found=value;
        }
        return found;
    }
    PluginJson sources(){PluginJson files=PluginJson::array();unsigned visited=0;
        for(const auto& [mount,rawRoot]:mediaRoots()){
            std::error_code ec;const auto root=std::filesystem::canonical(rawRoot,ec);if(ec)continue;
            std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;
            for(;it!=end&&!ec;it.increment(ec)){
                if(++visited>512||files.size()>=64)break;
                if(it.depth()>=3)it.disable_recursion_pending();
                if(!it->is_regular_file(ec)||ec||!videoFile(it->path()))continue;
                const auto path=std::filesystem::canonical(it->path(),ec);if(ec||!within(root,path))continue;
                auto relative=pathUtf8(path.lexically_relative(root));std::replace(relative.begin(),relative.end(),'\\','/');files.push_back(mount+"/"+relative);
            }
        }
        return toolSuccess({{"sources",files},{"limit",64},{"note","仅列出 materials/ 和 drafts/ 内的视频。来源被拒时先核对该列表，不要反复猜路径。"}});
    }
    void enqueue(Command command){std::lock_guard lock(commandMutex_);if(commands_.size()>=16){if(command.answer)command.answer->set_value(toolFailure("television_queue_full"));return;}commands_.push_back(std::move(command));cv_.notify_one();}
    bool hasCommands(){std::lock_guard lock(commandMutex_);return !commands_.empty();}

    void mediaLoop(){
        using namespace ayt::video;
        ayt::audio::AudioEngine audio;
        ayt::audio::AudioSettings settings;settings.backend=ayt::audio::AudioBackendKind::Miniaudio;settings.reverbReturn=0;
        const bool sound=audio.initialize(settings);int audioVolume=-1;
        AYVideoPlayer player(makeFFmpegDemuxer(),makeFFmpegDecoder());
        if(sound)player.attachAudioEngine(&audio);
        auto texture=makeCpuVideoFrameTexture();bool opened=false;
        {std::lock_guard lock(mutex_);session_=player.demuxer();}
        auto publish=[&](const VideoFrame& frame){
            if(!frame.data||texture->updateFromFrame(frame)!=VideoResult::Ok)return;
            std::vector<uint8_t> bgra(texture->rgba8Data(),texture->rgba8Data()+texture->rgba8ByteSize());
            for(size_t i=0;i+3<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
            std::lock_guard lock(mutex_);pixels_=std::move(bgra);frameWidth_=frame.width;frameHeight_=frame.height;framePts_=frame.pts.toUs()/1000000.0;previewPending_=false;dirty_=true;
        };
        while(!stopped_){
            Command command;bool has=false;
            {std::unique_lock lock(commandMutex_);if(commands_.empty())cv_.wait_for(lock,std::chrono::milliseconds(5));if(!commands_.empty()){command=std::move(commands_.front());commands_.pop_front();has=true;}}
            if(has){
                if(command.cancelled&&command.cancelled->load()){if(command.answer)command.answer->set_value(toolFailure("video_control_cancelled",true));continue;}
                const auto action=command.value.value("action",std::string{});auto status=VideoResult::Ok;PluginJson result;
                if(action=="open"){
                    if(player.state()!=PlayerState::Idle&&player.state()!=PlayerState::Stopped)player.stop();opened=false;
                    DemuxerOpenParams params;params.path=command.value.at("source");params.openTimeoutMs=10000;params.rwTimeoutMs=5000;
                    const bool network=isHttpUrl(params.path);params.reconnectMax=network?1:0;
                    {std::lock_guard lock(mutex_);source_=params.path;title_=command.value.value("title",std::string("一段影像"));state_="opening";playing_=false;position_=duration_=0;pixels_.clear();frameWidth_=frameHeight_=0;lastError_.clear();dirty_=true;audio.setMasterGain(volume_/100.0f);
                        if(network){const auto start=params.path.find("://")+3,slash=params.path.find_first_of("/?#",start);params.httpProxyOrigin=cfg_.value("proxyOrigin",std::string{});if(params.httpProxyOrigin.empty()&&cfg_.value("useSystemProxy",true))params.httpProxyOrigin=ayt::net::http::systemProxyOrigin(params.path.substr(0,slash));}}
                    params.protocolAllowlist=network?"http,https,tcp,tls,crypto,data,httpproxy":"file";
                    params.formatAllowlist=network?"mov,matroska,webm,avi,mpeg,mpegts,ogg,flv,asf,hls":"mov,matroska,webm,avi,mpeg,mpegts,ogg,flv,asf";
                    status=player.openWithOptions(params);
                    if(command.cancelled&&command.cancelled->load()){player.stop();status=VideoResult::Cancelled;}
                    if(status==VideoResult::Ok){MediaInfo info;player.getMediaInfo(info);opened=true;const bool paused=command.value.value("start_paused",false);status=player.play();if(paused&&status==VideoResult::Ok){player.pause();player.seek(ayt::time::Duration{});}
                        {std::lock_guard lock(mutex_);duration_=info.durationSec;visible_=command.value.value("visible",true);audioAvailable_=sound&&info.hasAudio;}
                        result=toolSuccess({{"opened",true},{"duration_seconds",info.durationSec},{"width",info.width},{"height",info.height},{"has_audio",info.hasAudio}});
                        emit({{"kind","television.opened"},{"source",params.path},{"duration_seconds",info.durationSec}});
                    }
                }else if(action=="stop"){
                    if(player.state()!=PlayerState::Idle&&player.state()!=PlayerState::Stopped)status=player.stop();opened=false;
                    std::lock_guard lock(mutex_);state_="stopped";playing_=false;position_=0;pixels_.clear();dirty_=true;
                }else if(!opened){result=toolFailure("no_video_open");}
                else if(action=="pause")status=player.pause();
                else if(action=="play")status=player.play();
                else if(action=="seek"){{std::lock_guard lock(mutex_);previewPending_=true;}status=player.seek(ayt::time::Duration::fromUs(static_cast<int64_t>(command.value.value("seconds",0.0)*1000000)));}
                else result=toolFailure("invalid_video_control");
                if(status!=VideoResult::Ok){std::lock_guard lock(mutex_);state_="failed";playing_=false;lastError_=toString(status);dirty_=true;result=toolFailure(action=="open"?"video_open_failed":"video_control_failed",status==VideoResult::DemuxError);}
                else if(opened){std::lock_guard lock(mutex_);playing_=player.state()==PlayerState::Playing;state_=playing_?"playing":"paused";position_=player.position().toUs()/1000000.0;dirty_=true;}
                if(result.is_null())result=toolSuccess();if(command.answer)command.answer->set_value(std::move(result));
            }
            {std::lock_guard lock(mutex_);if(volume_!=audioVolume){audioVolume=volume_;audio.setMasterGain(audioVolume/100.0f);}}
            if(!opened)continue;
            VideoFrame frame;auto status=VideoResult::Ok;
            if(player.state()==PlayerState::Playing)status=player.pullFrame(frame);
            else if(player.state()==PlayerState::Paused){player.pollSeekPreview();player.currentFrame(frame);}
            if(frame.data){bool changed;{std::lock_guard lock(mutex_);changed=pixels_.empty()||std::abs(framePts_-frame.pts.toUs()/1000000.0)>0.000001;}if(changed)publish(frame);}
            {std::lock_guard lock(mutex_);const auto pos=player.position().toUs()/1000000.0;if(static_cast<int>(pos)!=static_cast<int>(position_))dirty_=true;position_=pos;syncSource_=toString(player.syncSource());}
            if(status==VideoResult::EndOfStream){PluginJson ended;{std::lock_guard lock(mutex_);playing_=false;state_="ended";position_=duration_;dirty_=true;ended={{"kind","television.ended"},{"source",source_},{"title",title_},{"duration_seconds",duration_},{"frame_time_seconds",pixels_.empty()?PluginJson():PluginJson(framePts_)},{"wake_model",cfg_.value("wakeOnEnd",true)}};}emit(std::move(ended));}
            else if(status!=VideoResult::Ok){std::lock_guard lock(mutex_);state_="failed";playing_=false;lastError_=toString(status);dirty_=true;}
        }
        {std::lock_guard lock(mutex_);session_=nullptr;}
        player.stop();audio.shutdown();
        std::lock_guard lock(commandMutex_);for(auto& cmd:commands_)if(cmd.answer)cmd.answer->set_value(toolFailure("television_stopped"));commands_.clear();
    }
#include "Chrome.inc"
    void create(){
        if(window_||!host_.desktopEnabled)return;WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance_;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.lpszClassName=L"HibernationFloatingTelevision";RegisterClassW(&wc);
        const DWORD style=(framed()?WS_POPUP|WS_THICKFRAME|WS_SYSMENU:WS_OVERLAPPEDWINDOW)|WS_CLIPCHILDREN;
        window_=CreateWindowExW(WS_EX_APPWINDOW|(cfg_.value("topmost",true)?WS_EX_TOPMOST:0),wc.lpszClassName,L"小爱 · 浮动电视",style,160,120,static_cast<int>(cfg_.value("width",480)),static_cast<int>(cfg_.value("height",320)),nullptr,nullptr,instance_,this);
    }
    void drawWindow(HDC target){
        RECT r{};GetClientRect(window_,&r);if(!target||r.right<=0||r.bottom<=0)return;
        auto dc=CreateCompatibleDC(target);if(!buffer_||bufferWidth_!=r.right||bufferHeight_!=r.bottom){if(buffer_)DeleteObject(buffer_);buffer_=CreateCompatibleBitmap(target,r.right,r.bottom);bufferWidth_=r.right;bufferHeight_=r.bottom;}auto oldBitmap=SelectObject(dc,buffer_);drawChrome(dc,r);const auto layout=chromeLayout();const auto& pane=layout.screen;
        if(!webMode_&&!pixels_.empty()&&frameWidth_>0&&frameHeight_>0){
            const double scale=std::min(static_cast<double>(pane.width())/frameWidth_,static_cast<double>(pane.height())/frameHeight_);const int width=std::max(1,static_cast<int>(frameWidth_*scale)),height=std::max(1,static_cast<int>(frameHeight_*scale));
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frameWidth_;info.bmiHeader.biHeight=-frameHeight_;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
            StretchDIBits(dc,pane.left+(pane.width()-width)/2,pane.top+(pane.height()-height)/2,width,height,0,0,frameWidth_,frameHeight_,pixels_.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        }
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,framed()?RGB(64,89,60):RGB(195,210,188));auto oldFont=SelectObject(dc,font_);
        const auto label=state_=="playing"?"播放中":state_=="paused"?"暂停":state_=="opening"?"正在打开":state_=="ended"?"播放结束":state_=="failed"?"暂不可播放":state_=="stopped"?"已停止":"等待影片";
        const auto status=wide((webMode_?pagePlatform_:title_)+"  ·  "+(webMode_?std::string("网页"):std::string(label))+(!webMode_&&duration_>0?"  "+std::to_string(static_cast<int>(position_))+" / "+std::to_string(static_cast<int>(duration_))+"s":""));auto footer=nativeBox(layout.footer);DrawTextW(dc,status.c_str(),static_cast<int>(status.size()),&footer,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(pixels_.empty()&&!webMode_){RECT center{pane.left+10,pane.top+pane.height()/2-20,pane.right-10,pane.top+pane.height()/2+30};SetTextColor(dc,RGB(195,210,188));DrawTextW(dc,state_=="opening"?L"正在打开影像……":state_=="failed"?L"这段影像暂时无法播放。":L"这里可以放一段影像。",-1,&center,DT_CENTER|DT_SINGLELINE);}
        SelectObject(dc,oldFont);BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteDC(dc);
    }
    void paint(){PAINTSTRUCT ps;const auto dc=BeginPaint(window_,&ps);drawWindow(dc);dirty_=false;EndPaint(window_,&ps);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* a=reinterpret_cast<Television*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){a=static_cast<Television*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}if(!a)return DefWindowProcW(hwnd,message,w,l);std::lock_guard lock(a->mutex_);
        switch(message){
        case WM_CREATE:a->createChrome();return 0;
        case WM_NCCALCSIZE:if(a->framed())return 0;break;
        case WM_NCPAINT:if(a->framed())return 0;break;
        case WM_NCACTIVATE:if(a->framed())return TRUE;break;
        case WM_NCHITTEST:if(a->framed()){
            POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&point);RECT r{};GetClientRect(hwnd,&r);const bool left=point.x<7,right=point.x>=r.right-7,top=point.y<7,bottom=point.y>=r.bottom-7;
            if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;if(left)return HTLEFT;if(right)return HTRIGHT;
            const auto layout=a->chromeLayout();for(const auto& control:{layout.power,layout.play,layout.stop}){const auto bounds=nativeBox(control);if(PtInRect(&bounds,point))return HTCLIENT;}const auto pane=nativeBox(layout.screen);return PtInRect(&pane,point)?HTCLIENT:HTCAPTION;
        }break;
        case WM_PAINT:a->paint();return 0;
        case WM_PRINTCLIENT:a->drawWindow(reinterpret_cast<HDC>(w));return 0;
        case WM_SIZE:a->layoutChrome();a->dirty_=true;InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_DRAWITEM:{const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item&&item->CtlID>=3501&&item->CtlID<=3503){a->drawChromeButton(*item);return TRUE;}break;}
        case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){if(LOWORD(w)==3501){a->hideTelevision();return 0;}if(LOWORD(w)==3502){a->togglePlayback();return 0;}if(LOWORD(w)==3503){a->stopPlayback();return 0;}}break;
        case WM_ERASEBKGND:return 1;
        case WM_CLOSE:a->hideTelevision();return 0;
        case WM_KEYDOWN:if(w==VK_SPACE){a->togglePlayback();return 0;}break;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={320,220};return 0;
        }return DefWindowProcW(hwnd,message,w,l);
    }
    PluginJson platforms()const{
        std::lock_guard lock(mutex_);return cfg_.value("platforms",PluginJson::array({{{"id","bilibili"},{"name","哔哩哔哩"},{"enabled",true},{"hosts",PluginJson::array({"bilibili.com","b23.tv"})},{"homeUrl","https://www.bilibili.com/"},{"searchUrl","https://search.bilibili.com/all?keyword={query}"}}}));
    }
    static std::string urlHost(const std::string& url){const auto start=url.starts_with("https://")?8:url.starts_with("http://")?7:0;if(!start||url.size()>4096||url.find('@')!=std::string::npos||url.find_first_of("\r\n")!=std::string::npos||url.find('\0')!=std::string::npos)return {};const auto end=url.find_first_of("/?#",start);const auto origin=url.substr(0,end);if(!ayt::net::http::validOrigin(origin))return {};auto host=url.substr(start,end-start);if(const auto colon=host.find(':');colon!=std::string::npos)host.resize(colon);std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});return host;}
    static bool matches(const PluginJson& platform,const std::string& url){const auto host=urlHost(url);if(host.empty())return false;for(const auto& entry:platform.at("hosts")){const auto allowed=entry.get<std::string>();if(host==allowed||host.ends_with("."+allowed))return true;}return false;}
    PluginJson openPage(const std::string& url,const std::string& preferred,const std::function<bool()>& cancelled){
        PluginJson platform;for(const auto& row:platforms())if(row.value("enabled",true)&&(preferred.empty()||row.at("id")==preferred)&&matches(row,url)){platform=row;break;}if(platform.is_null())return toolFailure("video_platform_not_configured");
        std::function<PluginJson(const std::string&,const PluginJson&,const std::function<bool()>&)> bridge;HWND window;{std::lock_guard lock(mutex_);if(!cfg_.value("webBridgeEnabled",true))return toolFailure("webpage_bridge_disabled");if(!host_.desktopEnabled)return toolFailure("desktop_required");bridge=host_.browser;window=webSurface_;}
        if(!bridge)return toolFailure("webview_unavailable");enqueue({{{"action","stop"}}, {}});
        RECT client{};GetClientRect(window,&client);const auto result=bridge("open",{{"parent",reinterpret_cast<std::uintptr_t>(window)},{"url",url},{"platform",platform.at("id")},{"hosts",platform.at("hosts")},{"width",client.right},{"height",std::max(1L,client.bottom)}},cancelled);
        if(result.value("ok",false)){std::lock_guard lock(mutex_);webMode_=true;pageUrl_=url;pagePlatform_=platform.at("name");visible_=true;dirty_=true;}
        return result;
    }
    PluginJson search(const PluginJson& args,const std::function<bool()>& cancelled){
        const auto selected=args.value("platform",std::string("bilibili"));if(selected=="archive")return archiveSearch(args,cancelled);
        for(const auto& row:platforms())if(row.at("id")==selected&&row.value("enabled",true)){
            auto url=row.at("searchUrl").get<std::string>();const auto token=url.find("{query}");if(token==std::string::npos)return toolFailure("platform_search_not_configured");url.replace(token,7,urlEncode(args.at("query")));
            {std::lock_guard lock(mutex_);if(!host_.desktopEnabled)return toolSuccess({{"platform",selected},{"search_page",url},{"opened",false},{"note","这是平台搜索入口，尚未读取搜索结果；网页观察需要桌面模式。"}});}
            auto result=openPage(url,selected,cancelled);if(result.value("ok",false)){result["result"]["search_page"]=url;result["result"]["next_step"]="用 television.observe 读取实际搜索结果中的文字和视频链接，再选择链接打开。";}return result;
        }return toolFailure("video_platform_not_configured");
    }
    PluginJson archiveSearch(const PluginJson& args,const std::function<bool()>& externalCancel){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);auto cancel=[&]{return stopped_.load()||(externalCancel&&externalCancel())||std::chrono::steady_clock::now()>deadline;};try{auto query=args.at("query").get<std::string>();for(char& c:query)if(c=='"'||c=='\\'||c=='('||c==')')c=' ';const auto response=fetchJson("https://archive.org","/advancedsearch.php?q="+urlEncode("mediatype:movies AND title:(\""+query+"\")")+"&fl%5B%5D=identifier&fl%5B%5D=title&rows=3&output=json",cancel);PluginJson found=PluginJson::array();for(const auto& doc:response.at("response").at("docs")){if(cancel())break;const auto identifier=doc.at("identifier").get<std::string>();const auto metadata=fetchJson("https://archive.org","/metadata/"+urlEncode(identifier),cancel);for(const auto& f:metadata.at("files")){const auto name=f.value("name",std::string{});if(name.ends_with(".mp4")||name.ends_with(".webm")){found.push_back({{"title",doc.value("title",identifier)},{"url","https://archive.org/download/"+urlEncode(identifier)+"/"+urlEncode(name)},{"page","https://archive.org/details/"+identifier},{"license",metadata.value("metadata",PluginJson::object()).value("licenseurl",std::string{})}});break;}}}return toolSuccess({{"videos",found},{"provider","Internet Archive"}});}catch(...){return toolFailure(cancel()?"search_cancelled":"video_search_unavailable",true);}}
public:
    Television(){GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&anchor),&instance_);}static void anchor(){}
    const char* getName()const override{return "television";}const char* getDescription()const override{return "Floating television using AYVideoPlayer with AYAudio master clock";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{try{configurationTypes(cfg,{"proxyOrigin","frameStyle","framePalette"},{"topmost","useSystemProxy","wakeOnEnd","webBridgeEnabled"});number(cfg,"width",480,320,1280);number(cfg,"height",320,220,900);const auto frame=cfg.value("frameStyle",std::string("television")),palette=cfg.value("framePalette",std::string("sage"));if((frame!="television"&&frame!="window")||(palette!="sage"&&palette!="walnut"&&palette!="ivory"))throw std::runtime_error("television_appearance");
            if(cfg.contains("platforms")){const auto& rows=cfg.at("platforms");if(!rows.is_array()||rows.size()>8)throw std::runtime_error("platforms");std::set<std::string> ids;for(const auto& row:rows){const auto id=row.at("id").get<std::string>();if(id.empty()||id.size()>40||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos||!ids.insert(id).second||row.at("name").get<std::string>().size()>160||!row.value("enabled",true)||!row.at("hosts").is_array()||row.at("hosts").empty()||row.at("hosts").size()>16){if(!row.value("enabled",true))continue;throw std::runtime_error("platform");}for(const auto& value:row.at("hosts")){const auto host=value.get<std::string>();if(host.empty()||host.size()>253||host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=std::string::npos||host.front()=='.')throw std::runtime_error("platform_host");}if(!matches(row,row.at("homeUrl"))||!matches(row,row.at("searchUrl")))throw std::runtime_error("platform_url");}}
const int volume=static_cast<int>(number(cfg,"volume",65,0,100));{std::lock_guard lock(mutex_);host_=std::move(host);cfg_=std::move(cfg);volume_=volume;if(!font_)font_=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");create();applyChrome();if(window_)SetWindowPos(window_,cfg_.value("topmost",true)?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}if(!worker_.joinable()){stopped_=false;worker_=std::thread([this]{mediaLoop();});}issue.clear();return true;}catch(...){issue="television_configuration";return false;}}
    PluginJson tools()const override{return PluginJson::array({tool("television.platforms","读取已配置的视频平台、搜索入口和网页桥接方式。账号由用户在页面登录。",object()),tool("television.open_page","在小电视内打开已配置平台的网页，可保留该平台的本机登录状态。没有视频直链时使用此工具，再观察实际页面；不能绕过登录或版权限制。",object({{"url",{{"type","string"},{"minLength",1},{"maxLength",4096}}},{"platform",{{"type","string"},{"maxLength",40}}}},PluginJson::array({"url"}))),tool("television.sources","列出已授权 materials/ 和 drafts/ 中的视频路径，最多 64 个。",object()),tool("television.open","打开并播放 materials/ 或 drafts/ 工作区中的视频，或直接媒体 HTTP(S) URL。被拒时先用 television.sources 核对路径，不要反复猜测。",object({{"source",{{"type","string"},{"minLength",1},{"maxLength",2048}}},{"title",{{"type","string"},{"maxLength",160}}}},PluginJson::array({"source"}))),tool("television.control","控制电视播放、暂停、停止、跳转、显示或隐藏。",object({{"action",{{"enum",PluginJson::array({"play","pause","stop","seek","show","hide"})}}},{"seconds",{{"type","number"},{"minimum",0},{"maximum",86400}}}},PluginJson::array({"action"}))),tool("television.observe","获取当前真正呈现的画面及时间位置；有视觉模型时可分析返回帧。",object({{"include_frame",{{"type","boolean"}}}})),tool("television.search","按平台搜索视频：默认 bilibili 打开实际搜索页，再 observe 读取结果和链接；archive 搜索公开直链。",object({{"platform",{{"type","string"},{"maxLength",40}}},{"query",{{"type","string"},{"minLength",1},{"maxLength",256}}}},PluginJson::array({"query"})))});}
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{if(name=="television.platforms")return toolSuccess({{"platforms",platforms()},{"direct_search_provider","archive"},{"web_bridge","user_login_in_embedded_page"}});if(name=="television.open_page")return openPage(args.at("url"),args.value("platform",std::string{}),cancelled);if(name=="television.sources")return sources();if(name=="television.search")return search(args,cancelled);if(name=="television.observe"){
            bool web=false;decltype(host_.browser) bridge;{std::lock_guard lock(mutex_);web=webMode_;bridge=host_.browser;}if(web)return bridge?bridge("observe",{{"include_frame",args.value("include_frame",true)}},cancelled):toolFailure("webview_unavailable");
            const bool includeFrame=args.value("include_frame",true);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            std::vector<uint8_t> frame;int width=0,height=0;PluginJson result;
            for(;;){
                if(stopped_||(cancelled&&cancelled()))return toolFailure("video_observation_cancelled",true);
                bool wait=false;
                {std::lock_guard lock(mutex_);result=snapshot();result["frame_ready"]=!pixels_.empty()&&!previewPending_;
                    if(!pixels_.empty()&&!previewPending_){result["frame_time_seconds"]=framePts_;if(includeFrame){frame=pixels_;width=frameWidth_;height=frameHeight_;}}
                    wait=includeFrame&&(pixels_.empty()||previewPending_)&&(state_=="opening"||state_=="playing"||state_=="paused");}
                if(!wait||std::chrono::steady_clock::now()>=deadline)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if(!frame.empty()){const auto encoded=jpeg(frame,width,height);if(!encoded.empty())result["image"]={{"mime_type","image/jpeg"},{"base64",encoded}};}
result["perception_note"]="仅包含实际解码并呈现的帧；没有自动理解未提供的画面或声音。";return toolSuccess(result);}
        auto value=args;if(name=="television.open"){
            const auto requested=args.at("source").get<std::string>();for(const auto& platform:platforms())if(platform.value("enabled",true)&&matches(platform,requested))return openPage(requested,platform.at("id"),cancelled);
            decltype(host_.browser) bridge;{std::lock_guard lock(mutex_);webMode_=false;bridge=host_.browser;}if(bridge)bridge("close",PluginJson::object(),cancelled);const auto source=mediaSource(args.at("source"));if(source.empty())return toolFailure("video_source_outside_workspace_or_invalid_url");value["source"]=source;value["action"]="open";}
        if(name=="television.control"&&(args.at("action")=="show"||args.at("action")=="hide")){std::lock_guard lock(mutex_);if(!host_.desktopEnabled)return toolFailure("desktop_required");visible_=args.at("action")=="show";return toolSuccess({{"visible",visible_}});}
        {bool web;decltype(host_.browser) bridge;{std::lock_guard lock(mutex_);web=webMode_;bridge=host_.browser;}if(name=="television.control"&&web)return bridge?bridge("control",args,cancelled):toolFailure("webview_unavailable");}
        if(value.value("action",std::string{})=="seek"&&!value.contains("seconds"))return toolFailure("seek_position_required");
        auto promise=std::make_shared<std::promise<PluginJson>>();auto cancelledCommand=std::make_shared<std::atomic<bool>>(false);auto future=promise->get_future();{std::lock_guard lock(mutex_);}enqueue({value,promise,cancelledCommand});const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(18);while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready){if((cancelled&&cancelled())||std::chrono::steady_clock::now()>deadline){cancelledCommand->store(true);std::lock_guard lock(mutex_);if(session_)session_->requestAbort();return toolFailure("video_control_cancelled_or_timeout",true);}}return future.get();
    }
    void tick(double)override{std::lock_guard lock(mutex_);if(window_){if(webSurface_&&((GetWindowLongPtrW(webSurface_,GWL_STYLE)&WS_VISIBLE)!=0)!=webMode_)ShowWindow(webSurface_,webMode_?SW_SHOWNOACTIVATE:SW_HIDE);const bool glyph=playing_&&!webMode_;if(glyph!=lastButtonPlaying_){lastButtonPlaying_=glyph;if(playButton_)InvalidateRect(playButton_,nullptr,FALSE);}const auto shown=IsWindowVisible(window_)!=FALSE;if(visible_!=shown){ShowWindow(window_,visible_?SW_SHOWNOACTIVATE:SW_HIDE);if(webMode_&&host_.browser)host_.browser("visible",{{"visible",visible_}},{});}if(visible_&&dirty_)InvalidateRect(window_,nullptr,FALSE);}}
    void event(const PluginJson& value)override{if(value.value("kind",std::string{})=="surface.show"&&value.value("surface",std::string{})=="television"){std::lock_guard lock(mutex_);visible_=true;dirty_=true;return;}if(value.value("kind",std::string{})=="runtime.paused"){std::lock_guard lock(mutex_);runtimePaused_=value.value("paused",false);if(playButton_)EnableWindow(playButton_,!runtimePaused_);if(!runtimePaused_)return;if(webMode_&&host_.browser)host_.browser("control",{{"action","pause"}},{});else enqueue({{{"action","pause"}}, {}});}}
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);if(webMode_)return {{"frame_style",cfg_.value("frameStyle",std::string("television"))},{"frame_palette",cfg_.value("framePalette",std::string("sage"))},{"screen_rect",chromeScreenSnapshot()},{"mode","webpage"},{"page_platform",pagePlatform_},{"page_url",pageUrl_},{"playback_engine","WebView2"},{"sync_source","WebsitePlayer"},{"source",pageUrl_},{"state","webpage"},{"visible",visible_},{"observation","television.observe returns actual current title, URL, media state and optional pixels"}};return {{"frame_style",cfg_.value("frameStyle",std::string("television"))},{"frame_palette",cfg_.value("framePalette",std::string("sage"))},{"screen_rect",chromeScreenSnapshot()},{"mode","direct"},{"page_platform",pagePlatform_},{"page_url",pageUrl_},{"playback_engine","AYVideoPlayer"},{"sync_source",syncSource_},{"source",source_},{"title",title_},{"state",state_},{"position_seconds",position_},{"duration_seconds",duration_},{"visible",visible_},{"audio_available",audioAvailable_},{"frame_width",frameWidth_},{"frame_height",frameHeight_},{"error",lastError_}};}
    bool restore(const PluginJson& value)override{if(!value.is_object())return false;const auto source=value.value("source",std::string{});if(value.value("mode",std::string{})=="webpage"){std::lock_guard lock(mutex_);webMode_=false;pageUrl_=value.value("page_url",std::string{});pagePlatform_=value.value("page_platform",std::string{});visible_=false;return true;}if(source.empty())return true;if(value.value("state",std::string{})=="stopped"||value.value("state",std::string{})=="failed"){std::lock_guard lock(mutex_);source_=source;title_=value.value("title",std::string{});state_="stopped";visible_=value.value("visible",false);return true;}const auto valid=mediaSource(source);if(valid.empty())return true;enqueue({{{"action","open"},{"source",valid},{"title",value.value("title",std::string("一段影像"))},{"start_paused",true},{"visible",value.value("visible",false)}}, {}});const auto position=value.value("position_seconds",0.0);if(std::isfinite(position)&&position>0&&position<86400)enqueue({{{"action","seek"},{"seconds",position}}, {}});{std::lock_guard lock(mutex_);visible_=value.value("visible",false);}return true;}
    void shutdown()override{if(host_.browser)host_.browser("close",PluginJson::object(),{});stopped_=true;{std::lock_guard lock(mutex_);if(session_)session_->requestAbort();}cv_.notify_all();if(worker_.joinable())worker_.join();std::lock_guard lock(mutex_);session_=nullptr;if(window_){DestroyWindow(window_);window_=nullptr;}UnregisterClassW(L"HibernationFloatingTelevision",instance_);UnregisterClassW(L"HibernationTelevisionSurface",instance_);if(buffer_){DeleteObject(buffer_);buffer_=nullptr;}if(font_){DeleteObject(font_);font_=nullptr;}}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Television;}
