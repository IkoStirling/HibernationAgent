#include <Hibernation/RolePlugin.h>
#include <AYPlugin/DynamicPluginLoader.h>
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace hibernation;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<'\n';}
std::string utf8(const fs::path& path){const auto bytes=path.generic_u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
std::string readFile(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
struct Loaded {
    ayt::plugin::DynamicPluginLoaderImpl loader;IRolePlugin* role=nullptr;
    explicit Loaded(const std::string& dll):loader(dll){if(!loader.load(dll))throw std::runtime_error("isolated native plugin failed to load");role=dynamic_cast<IRolePlugin*>(loader.getPlugin());if(!role||role->roleApiVersion()!=2)throw std::runtime_error("native role API mismatch");}
    void bind(const PluginHostContext& host,const PluginJson& settings){std::string issue;if(!role->bind(host,settings,issue))throw std::runtime_error("isolated native plugin bind failed");}
    PluginJson tool(const std::string& name,const PluginJson& args=PluginJson::object()){return role->invoke(name,args,[]{return false;});}
};
std::uint32_t firstPixel(HWND window){
    BITMAPINFO information{};information.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);information.bmiHeader.biWidth=48;information.bmiHeader.biHeight=-48;information.bmiHeader.biPlanes=1;information.bmiHeader.biBitCount=32;information.bmiHeader.biCompression=BI_RGB;
    std::uint32_t* pixels=nullptr;auto bitmap=CreateDIBSection(nullptr,&information,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);if(!bitmap)throw std::runtime_error("capture failed");auto dc=CreateCompatibleDC(nullptr);auto previous=SelectObject(dc,bitmap);SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);const auto value=pixels[0];SelectObject(dc,previous);DeleteDC(dc);DeleteObject(bitmap);return value;
}
}
int main(int argc,char** argv){
 if(argc<3){std::cerr<<"expected DLL and owned scratch root\n";return 2;}
 const auto original=GetThreadDesktop(GetCurrentThreadId());const auto desktopName=L"HibernationPixelRestore-"+std::to_wstring(GetCurrentProcessId());auto desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
 if(!desktop||!SetThreadDesktop(desktop)){if(desktop)CloseDesktop(desktop);std::cerr<<"private desktop unavailable\n";return 2;}
 int result=0;
 try{
    const auto root=fs::u8path(argv[2])/("restore-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directories(root/"drafts");const auto ownedDll=root/"Hibernation_PixelCanvas.dll";fs::copy_file(fs::u8path(argv[1]),ownedDll);
    PluginHostContext host;host.dataRoot=root/"data";host.projectRoot=root;host.draftsRoot=root/"drafts";host.desktopEnabled=true;std::vector<PluginJson> events;host.emit=[&](PluginJson event){events.push_back(std::move(event));};
    const PluginJson settings={{"maximumDocuments",4},{"defaultScale",3},{"restoreDesktopArtwork",true}};PluginJson saved;const auto dll=utf8(ownedDll);
    {
        Loaded module(dll);module.bind(host,settings);
        check(module.tool("pixel.create",{{"id","lamp"},{"title","synthetic restored lamp"},{"width",16},{"height",16}}).at("ok"),"fixture draft created through native plugin");
        check(module.tool("pixel.edit",{{"id","lamp"},{"expected_revision",1},{"frame",0},{"commands",PluginJson::array({{{"op","pixel"},{"x",0},{"y",0},{"color",5}}})}}).at("ok"),"fixture source pixel saved through actual drawing tool");
        check(module.tool("pixel.show",{{"id","lamp"},{"visible",true},{"scale",3}}).at("ok"),"fixture display requested");module.role->tick(0);saved=module.role->snapshot();
        check(saved.at("surface").at("visible")==true&&saved.at("surface").at("revision")==2,"saved snapshot reflects actual visible version");
    }
    check(!FindWindowW(L"HibernationPixelArtworkV1",nullptr),"module unload removes its private display");
    const auto path=host.draftsRoot/"pixel-canvas/documents/lamp.json";const auto beforeRestore=readFile(path);
    auto forged=saved;forged["document"]={{"palette",PluginJson::array({"#FF0000FF"})},{"frames",PluginJson::array({{{"pixels",std::vector<int>(256,0)}}})}};forged["surface"]["pixels"]="untrusted pixels must not become a restored artwork";
    {
        Loaded module(dll);module.bind(host,settings);events.clear();check(module.role->restore(forged),"saved extra pixel grids do not reject optional presentation restore");
        check(module.role->snapshot().at("surface").at("pending")==true&&module.role->snapshot().at("surface").at("visible")==false,"matching restore queues on owner before claiming visible");
        module.role->tick(0);const auto state=module.role->snapshot();auto window=FindWindowW(L"HibernationPixelArtworkV1",nullptr);
        check(window&&state.at("surface").at("visible")==true&&firstPixel(window)==0xFF5D7F67u,"restored actual window uses file pixels instead of forged saved grid");
        check(readFile(path)==beforeRestore,"presentation restore never rewrites source artwork or undo");
        check(std::none_of(events.begin(),events.end(),[](const auto& event){return event.value("kind",std::string{})=="pixel.created"||event.value("kind",std::string{})=="pixel.revised";}),"reopening old artwork does not emit a new creation or edit");
        const auto changed=module.tool("pixel.edit",{{"id","lamp"},{"expected_revision",2},{"frame",0},{"commands",PluginJson::array({{{"op","pixel"},{"x",0},{"y",0},{"color",6}}})}});check(changed.at("ok")&&changed.at("result").at("document").at("revision")==3,"source may advance beyond last actually displayed saved version");
    }
    {
        Loaded module(dll);module.bind(host,settings);check(module.role->restore(saved),"changed revision does not reject entire plugin startup");module.role->tick(0);const auto state=module.role->snapshot();
        check(state.at("surface").at("visible")==false&&state.at("surface").at("pending")==false&&state.at("restore_issue")=="pixel_restore_revision_changed","older visible snapshot cannot restore changed file revision");
        check(module.tool("pixel.read",{{"id","lamp"}}).at("result").at("document").at("revision")==3,"refused stale restore preserves current draft");
    }
    auto missing=saved;missing["surface"]["document_id"]="missing";
    {
        Loaded module(dll);module.bind(host,settings);check(module.role->restore(missing),"missing optional artwork does not fail plugin startup");module.role->tick(0);
        check(module.role->snapshot().at("surface").at("visible")==false&&module.role->snapshot().at("restore_issue")=="pixel_document_missing"&&!fs::exists(host.draftsRoot/"pixel-canvas/documents/missing.json"),"missing saved artwork is neither recreated nor displayed");
    }
    auto traversal=saved;traversal["surface"]["document_id"]="../outside";
    {
        Loaded module(dll);module.bind(host,settings);check(module.role->restore(traversal),"malformed saved identity is handled as optional restore refusal");module.role->tick(0);
        check(module.role->snapshot().at("surface").at("visible")==false&&module.role->snapshot().at("restore_issue")=="pixel_restore_metadata_invalid","saved identity cannot introduce a path outside the store");
    }
    auto wrongType=saved;wrongType["surface"]["visible"]=1;
    {
        Loaded module(dll);module.bind(host,settings);check(module.role->restore(wrongType),"wrong typed snapshot visibility does not abort plugin startup");module.role->tick(0);check(module.role->snapshot().at("surface").at("visible")==false,"truthy numeric visibility cannot reopen a window");
    }
    {std::ofstream corrupt(path,std::ios::binary);corrupt<<"synthetic corrupt source";}
    {
        Loaded module(dll);module.bind(host,settings);check(module.role->restore(saved),"corrupt optional draft does not abort plugin startup");module.role->tick(0);const auto state=module.role->snapshot();
        check(state.at("surface").at("visible")==false&&state.at("restore_issue")=="pixel_existing_document_invalid"&&readFile(path)=="synthetic corrupt source","corrupt draft is preserved instead of rebuilt from saved pixels");
    }
    std::cout<<"Pixel restore passed "<<checks<<" checks on a private desktop\n";
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';result=1;}
 SetThreadDesktop(original);CloseDesktop(desktop);return result;
}
