#include <Hibernation/DesktopServices.h>
#include <Windows.h>
#include <httplib.h>
#include <future>
#include <atomic>
#include <fstream>
#include <thread>
#include <stdexcept>
using namespace hibernation;
namespace {
void check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
template<class F> bool wait(F probe,DesktopServices& browser){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<end){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}browser.tick();if(probe())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;
}
Json call(DesktopServices& browser,const std::string& op,Json args=Json::object()){
    auto result=std::async(std::launch::async,[&]{return browser.browser(op,args,{});});
    check(wait([&]{return result.wait_for(std::chrono::seconds(0))==std::future_status::ready;},browser),"browser command timeout");return result.get();
}
bool ok(const Json& result){return result.value("ok",false);}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;const auto root=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(root);std::ofstream report(root/"result.txt");
    try{
        httplib::Server server;server.Get("/fixture",[](const auto&,auto& response){response.set_header("Set-Cookie","login_fixture=kept; Max-Age=86400; Path=/");response.set_content("<html><title>Login fixture</title><body><script>localStorage.setItem('login_fixture','kept');document.body.append('Persistent '+document.cookie+' '+localStorage.getItem('login_fixture'));</script></body></html>","text/html");});
        std::atomic<bool> signedIn=false;
        server.Get("/state",[&](const auto&,auto& response){response.set_content(signedIn?"yes":"no","text/plain");});
        server.Get("/auto",[](const auto&,auto& response){response.set_content(R"(<html><title>Automatic login fixture</title><body><div id="auth" class="pending">checking</div><script>async function update(){const state=await(await fetch('/state',{cache:'no-store'})).text();document.querySelector('#auth').className=state;document.querySelector('#auth').textContent=state;}update();setInterval(update,100);</script></body></html>)","text/html");});
        const int port=server.bind_to_any_port("127.0.0.1");check(port>0,"loopback fixture");std::thread network([&]{server.listen_after_bind();});
        struct Network{httplib::Server& s;std::thread& t;~Network(){s.stop();t.join();}}networkOwner{server,network};
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"HibernationFloatingTelevision";RegisterClassW(&cls);
        const auto window=CreateWindowExW(0,cls.lpszClassName,L"Private TV fixture",WS_OVERLAPPEDWINDOW,100,100,480,320,nullptr,nullptr,cls.hInstance,nullptr);check(window!=nullptr,"fixture television");ShowWindow(window,SW_SHOWNOACTIVATE);
        struct Window{HWND h;~Window(){DestroyWindow(h);}}owned{window};
        Config cfg;cfg.desktopEnabled=true;cfg.dataRoot=root/"data";
        DesktopServices browser(cfg);const std::string url="http://127.0.0.1:"+std::to_string(port)+"/fixture";
        const Json open={{"parent",reinterpret_cast<std::uintptr_t>(window)},{"platform","fixture"},{"hosts",Json::array({"127.0.0.1"})},{"url",url},{"width",440},{"height",240}};
        check(ok(call(browser,"open",open)),"open real fixture webview");
        const auto before=call(browser,"observe",{{"include_frame",false}});check(ok(before)&&before.at("result").at("title")=="Login fixture","real fixture ready");
        check(ok(call(browser,"login",{{"return_url",url},{"name","fixture"}})),"open login window");
        const auto popup=FindWindowW(L"HibernationTelevisionLogin",nullptr);check(popup!=nullptr,"native independent login window exists");
        check((GetWindowLongPtrW(popup,GWL_STYLE)&WS_THICKFRAME)!=0&&(GetWindowLongPtrW(popup,GWL_STYLE)&WS_MAXIMIZEBOX)!=0,"resizable maximizable login window");
        RECT large{};GetClientRect(popup,&large);check(large.right>600&&large.bottom>400,"large readable client area");
        check(ok(call(browser,"login",{{"return_url",url}}))&&FindWindowW(L"HibernationTelevisionLogin",nullptr)==popup,"repeat login reuses same window");
        for(const auto& op:{"observe","page","video_frame","control","open"}){const auto blocked=call(browser,op,op==std::string("open")?open:Json{{"action","play"}});check(!ok(blocked)&&blocked.at("error").at("code")=="television_login_in_progress","role cannot inspect or replace login");}
        check(ok(call(browser,"resize",{{"width",300},{"height",180}})),"TV resize accepted during login");check(ok(call(browser,"visible",{{"visible",false}})),"TV hide accepted during login");
        RECT retained{};GetClientRect(popup,&retained);check(retained.right==large.right&&retained.bottom==large.bottom&&IsWindowVisible(popup),"TV resize and hide leave login window independent");
        SetWindowPos(popup,nullptr,0,0,800,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);GetClientRect(popup,&retained);check(retained.right>700,"login window resizes");
        SendMessageW(popup,WM_CLOSE,0,0);check(!FindWindowW(L"HibernationTelevisionLogin",nullptr),"close login returns to TV");
        const auto after=call(browser,"observe",{{"include_frame",false}});check(ok(after)&&after.at("result").at("url")==url&&after.at("result").at("text").get<std::string>().find("login_fixture=kept")!=std::string::npos,"document and cookie survive reparenting");
        check(ok(call(browser,"close")),"close browser controller");check(ok(call(browser,"open",open)),"reopen same platform profile");
        const auto restored=call(browser,"observe",{{"include_frame",false}});check(ok(restored)&&restored.at("result").at("text").get<std::string>().find("login_fixture=kept")!=std::string::npos,"profile works after reopening");
        check(std::filesystem::is_directory(cfg.dataRoot/"runtime/browser/fixture"),"profile kept outside program build");
        check(ok(call(browser,"close")),"close unknown-auth fixture");
        auto automatic=open;automatic["url"]="http://127.0.0.1:"+std::to_string(port)+"/auto";automatic["login_detection"]={{"signed_in_selector","#auth.yes"},{"signed_out_selector","#auth.no"}};
        check(ok(call(browser,"open",automatic)),"open automatic login fixture");
        check(wait([]{return FindWindowW(L"HibernationTelevisionLogin",nullptr)!=nullptr;},browser),"signed out automatically opens login window");
        check(call(browser,"login_status").at("result").at("login_state")=="signed_out","signed-out boolean recorded");
        signedIn=true;
        check(wait([]{return FindWindowW(L"HibernationTelevisionLogin",nullptr)==nullptr;},browser),"verified sign in automatically returns to TV");
        check(call(browser,"login_status").at("result").at("login_state")=="signed_in","verified state retained");
        check(ok(call(browser,"observe",{{"include_frame",false}})),"role observation resumes after confirmed login");
        signedIn=false;
        check(wait([]{return FindWindowW(L"HibernationTelevisionLogin",nullptr)!=nullptr;},browser),"expired login prompts again");
        SendMessageW(FindWindowW(L"HibernationTelevisionLogin",nullptr),WM_CLOSE,0,0);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(4);
        check(wait([&]{return std::chrono::steady_clock::now()>=until;},browser),"pump dismissed login observation");
        check(!FindWindowW(L"HibernationTelevisionLogin",nullptr)&&call(browser,"login_status").at("result").at("auto_prompt_dismissed")==true,"manual cancellation does not repeatedly reopen");
        signedIn=true;check(ok(call(browser,"open",automatic)),"navigate fresh signed-in page");
        check(wait([&]{return call(browser,"login_status").at("result").at("login_state")=="signed_in";},browser),"already signed in recognized without prompting");
        check(!FindWindowW(L"HibernationTelevisionLogin",nullptr),"already signed in has no popup");
        check(ok(call(browser,"close"))&&ok(call(browser,"open",open)),"return to unknown-auth page for manual fallback");
        check(ok(call(browser,"login",{{"return_url",url}})),"reopen login before shutdown");browser.stop();check(!FindWindowW(L"HibernationTelevisionLogin",nullptr),"shutdown closes owned login window");
        if(GetEnvironmentVariableW(L"HA_TEST_BILIBILI_AUTH",nullptr,0)>0){
            DesktopServices publicBrowser(cfg);auto publicPage=open;publicPage["platform"]="public-bilibili-fixture";publicPage["url"]="https://www.bilibili.com/";publicPage["hosts"]=Json::array({"bilibili.com","b23.tv"});
            check(ok(call(publicBrowser,"open",publicPage)),"open isolated anonymous public Bilibili");
            const bool prompted=wait([]{return FindWindowW(L"HibernationTelevisionLogin",nullptr)!=nullptr;},publicBrowser);
            const auto status=call(publicBrowser,"login_status");report<<"Public anonymous login state: "<<status.dump()<<'\n';
            if(!prompted){const auto observed=call(publicBrowser,"page",{{"limit",256}});report<<"Public page diagnostic: "<<observed.dump()<<'\n';}
            check(prompted&&status.at("result").at("login_state")=="signed_out","official Bilibili boolean works in real WebView");
            publicBrowser.stop();check(!FindWindowW(L"HibernationTelevisionLogin",nullptr),"anonymous window closes on stop");
        }
        report<<"PASS independent login, resize, reuse, blocked observations, return to TV, persistent profile, shutdown; automatic auth transitions and dismissal\n";return 0;
    }catch(const std::exception& error){report<<"FAIL "<<error.what()<<'\n';return 1;}
}
