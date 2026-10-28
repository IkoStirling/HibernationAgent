#include "../plugins/pixel-canvas/PixelDocument.h"
#include "../plugins/pixel-canvas/PixelStore.h"
#include "../plugins/pixel-canvas/PixelRestore.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace hibernation;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<'\n';}
PluginJson arguments(const std::string& id="flower"){return {{"id",id},{"title","A small flower"},{"width",16},{"height",16}};}
std::string bytes(const fs::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
std::vector<std::uint8_t> decode(const fs::path& path,int& width,int& height){
    using Microsoft::WRL::ComPtr;const auto started=CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::vector<std::uint8_t> out;
    {ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))&&SUCCEEDED(decoder->GetFrame(0,&frame))){UINT w=0,h=0;frame->GetSize(&w,&h);width=w;height=h;if(SUCCEEDED(factory->CreateFormatConverter(&converter))&&SUCCEEDED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){out.resize(static_cast<std::size_t>(w)*h*4);if(FAILED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(out.size()),out.data())))out.clear();}}}
    if(SUCCEEDED(started))CoUninitialize();return out;
}
}
int main(int argc,char** argv){
 try{
    const auto base=argc>1?fs::u8path(argv[1]):fs::temp_directory_path()/"hibernation-pixel-unit";const auto root=base/("pixel-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directories(root);
    std::string issue;auto d=pixel::createDocument(arguments(),1,issue);check(d.is_object()&&d.at("revision")==1&&d.at("palette")[0]=="#00000000","valid transparent canvas starts revision1");
    auto invalid=arguments();invalid["width"]=128;check(pixel::createDocument(invalid,1,issue).is_null()&&issue=="pixel_canvas_size","unsupported canvas rejected");
    invalid=arguments("../outside");check(pixel::createDocument(invalid,1,issue).is_null(),"relative traversal identity rejected");
    invalid=arguments();invalid["palette"]=PluginJson::array({"#FFFFFFFF","#000000FF"});check(pixel::createDocument(invalid,1,issue).is_null()&&issue=="pixel_palette","palette zero must remain transparent");
    const auto batch=PluginJson::array({{{"op","rect"},{"x",2},{"y",2},{"x2",5},{"y2",5},{"color",1}},{{"op","fill"},{"x",3},{"y",3},{"color",5}},{{"op","line"},{"x",0},{"y",0},{"x2",1},{"y2",1},{"color",6}}});
    check(pixel::editDocument(d,0,batch,issue),"line rectangle and flood fill batch succeeds");
    const auto& p=d.at("frames")[0].at("pixels");check(p[3*16+3]==5&&p[2*16+2]==1&&p[0]==6&&p[15*16+15]==0,"drawing geometry and untouched transparent pixels match");
    const auto before=d;auto invalidBatch=PluginJson::array({{{"op","pixel"},{"x",7},{"y",7},{"color",8}},{{"op","pixel"},{"x",99},{"y",0},{"color",1}}});
    check(!pixel::editDocument(d,0,invalidBatch,issue)&&d==before,"later invalid command rolls back entire batch");
    check(!pixel::editDocument(d,4,batch,issue)&&issue=="pixel_frame_index","invalid frame rejected");
    auto over=PluginJson::array();for(int i=0;i<257;++i)over.push_back({{"op","clear"}});check(!pixel::editDocument(d,0,over,issue)&&issue=="pixel_command_limit","command batch has hard size limit");
    check(!pixel::changeFrame(d,{{"action","remove"},{"index",0}},issue)&&issue=="pixel_last_frame"&&d.at("frames").size()==1,"last remaining frame cannot be removed");
    check(pixel::changeFrame(d,{{"action","copy"},{"index",0},{"duration_ms",350}},issue)&&d.at("frames").size()==2&&d.at("frames")[1].at("pixels")==d.at("frames")[0].at("pixels"),"copy frame inserts after source");
    check(pixel::changeFrame(d,{{"action","add"},{"index",1}},issue)&&d.at("frames")[2].at("pixels")[0]==0&&d.at("frames")[2].at("duration_ms")==200,"add frame inserts transparent canvas with default duration");
    check(pixel::changeFrame(d,{{"action","copy"},{"index",1}},issue)&&d.at("frames")[2].at("duration_ms")==350,"copy frame without duration preserves source timing");
    check(!pixel::changeFrame(d,{{"action","timing"},{"index",0},{"duration_ms",49}},issue)&&issue=="pixel_frame_duration","invalid animation duration rejected");
    while(d.at("frames").size()<8)check(pixel::changeFrame(d,{{"action","copy"}},issue),"frame growth within bound");
    check(!pixel::changeFrame(d,{{"action","add"}},issue)&&issue=="pixel_frame_limit","ninth frame refused");
    pixel::PixelStore store;check(store.configure(root,3,issue),"store binds explicit workspace");
    auto saved=store.create(arguments(),100,issue);check(saved.is_object(),"canvas persisted");
    check(store.create(arguments(),101,issue).is_null()&&issue=="pixel_document_exists","duplicate identity never replaces existing canvas");
    auto changed=store.revise("flower",1,200,[&](auto& document,std::string& error){return pixel::editDocument(document,0,batch,error);},issue);
    check(changed.is_object()&&changed.at("revision")==2,"atomic write produces next revision");
    check(store.revise("flower",1,201,[&](auto&,auto&){return true;},issue).is_null()&&issue=="pixel_revision_conflict","stale revision rejected");
    check(store.revise("flower",2,202,[&](auto& document,std::string& error){return pixel::editDocument(document,0,invalidBatch,error);},issue).is_null()&&store.read("flower",issue).at("revision")==2,"invalid persisted batch does not advance revision");
    pixel::PixelStore restarted;check(restarted.configure(root,3,issue)&&restarted.read("flower",issue)==changed,"restart recovers exact document");
    auto undone=restarted.undo("flower",2,300,issue);check(undone.is_object()&&undone.at("revision")==3&&undone.at("frames")[0].at("pixels")[0]==0,"persisted undo restores pixels with new monotonic revision");
    check(restarted.undo("flower",3,301,issue).is_null()&&issue=="pixel_undo_empty","empty undo returns explicit failure");
    changed=restarted.revise("flower",3,400,[&](auto& document,std::string& error){return pixel::editDocument(document,0,batch,error);},issue);
    changed=restarted.revise("flower",4,401,[&](auto& document,std::string& error){return pixel::changeFrame(document,{{"action","copy"},{"index",0},{"duration_ms",300}},error);},issue);
    auto png=restarted.exportDocument("flower",5,false,0,{},issue);check(png.is_object()&&png.at("written")==true,"actual PNG export succeeds");
    int width=0,height=0;const auto pixels=decode(fs::u8path(png.at("path").get<std::string>()),width,height);check(width==16&&height==16&&pixels.size()==1024&&pixels[3]==255&&pixels[(15*16+15)*4+3]==0,"export decodes to original RGBA dimensions and alpha");
    check(pixels[0]==0x89&&pixels[1]==0xAC&&pixels[2]==0x72,"PNG keeps exact RGB palette values");
    auto sheet=restarted.exportDocument("flower",5,true,0,{},issue);check(sheet.is_object()&&sheet.at("frame_count")==2,"animation sheet and manifest export");
    const auto sheetPixels=decode(fs::u8path(sheet.at("path").get<std::string>()),width,height);const auto manifest=PluginJson::parse(bytes(fs::u8path(sheet.at("manifest_path").get<std::string>())));
    check(width==32&&height==16&&manifest.at("frames")[1].at("x")==16&&manifest.at("frames")[1].at("duration_ms")==300,"sheet geometry and frame timings match draft");
    check(restarted.exportDocument("flower",5,true,0,{},issue)==sheet,"same-version identical export is idempotent");
    {std::ofstream modified(fs::u8path(png.at("path").get<std::string>()),std::ios::binary);modified<<"user edited";}
    check(restarted.exportDocument("flower",5,false,0,{},issue).is_null()&&issue=="pixel_export_exists","export does not overwrite externally modified artifact");
    check(restarted.exportDocument("flower",4,false,0,{},issue).is_null()&&issue=="pixel_revision_conflict","export requires current revision");
    check(restarted.exportDocument("flower",5,false,0,[]{return true;},issue).is_null()&&issue=="cancelled","cancelled export writes no new artifact");
    const auto preview=restarted.preview("flower",0,issue);check(preview.is_object()&&preview.at("image").at("mime_type")=="image/png"&&preview.at("width")==256&&preview.at("original_width")==16,"preview carries actual nearest-neighbor PNG and source dimensions");
    {std::ofstream corrupt(root/"pixel-canvas/documents/corrupt.json");corrupt<<"not valid json";}
    check(restarted.read("corrupt",issue).is_null()&&issue=="pixel_existing_document_invalid","corrupt draft is unavailable");
    check(restarted.create(arguments("corrupt"),500,issue).is_null()&&issue=="pixel_document_exists"&&bytes(root/"pixel-canvas/documents/corrupt.json")=="not valid json","corrupt occupied identity never overwritten");
    check(restarted.create(arguments("third"),501,issue).is_object()&&restarted.create(arguments("fourth"),502,issue).is_null()&&issue=="pixel_document_limit","invalid occupied files count toward document capacity");
    const auto listed=restarted.list(issue);check(listed.at("documents").size()==2&&listed.at("invalid_slots")==1&&listed.at("occupied_slots")==3,"listing distinguishes valid drafts and occupied invalid slots");
    auto malformed=changed;malformed["format_version"]="wrong type";check(!pixel::validateDocument(malformed,issue),"wrong typed stored metadata is rejected without throwing");
    malformed["format_version"]=1.5;check(!pixel::validateDocument(malformed,issue),"fractional schema version is not truncated into supported version");
    changed=restarted.revise("flower",5,600,[&](auto& document,std::string& error){return pixel::changeFrame(document,{{"action","timing"},{"index",0},{"duration_ms",400}},error);},issue);
    const auto exportRoot=root/"pixel-canvas/exports";{std::ofstream occupied(exportRoot/"flower-r6-sheet.json");occupied<<"user owned";}
    check(restarted.exportDocument("flower",6,true,0,{},issue).is_null()&&issue=="pixel_export_exists"&&!fs::exists(exportRoot/"flower-r6-sheet.png"),"manifest collision is checked before publishing sheet image");
    changed=restarted.revise("flower",6,500,[&](auto& document,std::string& error){return pixel::changeFrame(document,{{"action","timing"},{"index",0},{"duration_ms",450}},error);},issue);
    check(changed.at("updated_ms")==600,"system clock rollback does not move modification ordering backwards");
    std::size_t files=0;for(const auto& entry:fs::directory_iterator(exportRoot))++files;for(int i=0;files<255;++i,++files){std::ofstream item(exportRoot/("occupied-"+std::to_string(i)+".bin"));item<<'x';}
    check(restarted.exportDocument("flower",7,true,0,{},issue).is_null()&&issue=="pixel_export_limit"&&!fs::exists(exportRoot/"flower-r7-sheet.png"),"combined export capacity is checked before either new file");
    std::int64_t revision=7;for(int i=0;i<10;++i){changed=restarted.revise("flower",revision,700+i,[&](auto& document,std::string& error){return pixel::changeFrame(document,{{"action","timing"},{"duration_ms",500+i}},error);},issue);check(changed.is_object(),"bounded history accepts later edit");++revision;}
    for(int i=0;i<8;++i){changed=restarted.undo("flower",revision,800+i,issue);check(changed.is_object()&&changed.at("revision")==revision+1,"bounded history undo advances identity");++revision;}
    check(restarted.undo("flower",revision,900,issue).is_null()&&issue=="pixel_undo_empty","only eight actual undo snapshots are retained");
    const auto colorRoot=root/"color-and-capacity";fs::create_directories(colorRoot);pixel::PixelStore colorStore;check(colorStore.configure(colorRoot,4,issue),"separate palette fixture has explicit workspace");
    auto translucent=arguments("translucent");translucent["palette"]=PluginJson::array({"#00000000","#A1417380"});check(colorStore.create(translucent,1000,issue).is_object(),"custom alpha palette accepted");
    check(colorStore.revise("translucent",1,1001,[&](auto& document,std::string& error){return pixel::editDocument(document,0,PluginJson::array({{{"op","pixel"},{"x",0},{"y",0},{"color",1}}}),error);},issue).is_object(),"custom alpha pixel persisted");
    const auto translucentPng=colorStore.exportDocument("translucent",2,false,0,{},issue);check(translucentPng.is_object(),"custom alpha PNG exported");const auto translucentBytes=decode(fs::u8path(translucentPng.at("path").get<std::string>()),width,height);
    check(translucentBytes.size()==1024&&translucentBytes[0]==0xA1&&translucentBytes[1]==0x41&&translucentBytes[2]==0x73&&translucentBytes[3]==0x80,"PNG keeps partial alpha with unpremultiplied source RGB");
    {std::ofstream occupied(colorRoot/"pixel-canvas/documents/UPPER.JSON");occupied<<"corrupt uppercase occupied file";}
    check(colorStore.create(arguments("upper"),1002,issue).is_null()&&issue=="pixel_document_exists"&&bytes(colorRoot/"pixel-canvas/documents/UPPER.JSON")=="corrupt uppercase occupied file","Windows case aliases cannot replace corrupt occupied document");
    check(colorStore.read("upper",issue).is_null()&&issue=="pixel_existing_document_invalid","case alias of corrupt document remains invalid instead of missing");
    {std::ofstream occupied(colorRoot/"pixel-canvas/exports/TRANSLUCENT-R2-SHEET.PNG");occupied<<"uppercase user artifact";}
    check(colorStore.exportDocument("translucent",2,true,0,{},issue).is_null()&&issue=="pixel_export_exists"&&bytes(colorRoot/"pixel-canvas/exports/TRANSLUCENT-R2-SHEET.PNG")=="uppercase user artifact","Windows case aliases cannot overwrite existing export");
    auto large=arguments("large");large["width"]=64;large["height"]=64;check(colorStore.create(large,1100,issue).is_object(),"largest supported canvas created");
    const auto enlarge=[&](PluginJson& document,std::string&){while(document.at("frames").size()<8)document["frames"].push_back(document.at("frames")[0]);for(std::size_t f=0;f<8;++f)for(std::size_t i=0;i<4096;++i)document["frames"][f]["pixels"][i]=(i+f)%document.at("palette").size();return true;};
    auto largest=colorStore.revise("large",1,1101,enlarge,issue);check(largest.is_object()&&largest.at("frames").size()==8,"largest eight-frame artwork fits bounded store");
    std::int64_t largeRevision=2;for(int i=0;i<8;++i){largest=colorStore.revise("large",largeRevision,1200+i,[&](auto& document,std::string& error){return pixel::changeFrame(document,{{"action","timing"},{"index",0},{"duration_ms",200+i}},error);},issue);check(largest.is_object(),"largest artwork keeps bounded persisted undo");++largeRevision;}
    check(fs::file_size(colorRoot/"pixel-canvas/documents/large.json")<2*1024*1024,"maximum canvas frame and undo combination remains within publication budget");
    const auto largestSheet=colorStore.exportDocument("large",largeRevision,true,0,{},issue);check(largestSheet.is_object()&&largestSheet.at("width")==512&&largestSheet.at("height")==64,"largest animation exports all eight frame rectangles");
    const auto largestBytes=decode(fs::u8path(largestSheet.at("path").get<std::string>()),width,height);check(width==512&&height==64&&largestBytes[3]==0&&largestBytes[64*4+3]==255,"largest sheet decodes and keeps distinct adjacent frame source pixels");
    auto recolored=colorStore.revise("translucent",2,1400,[&](auto& document,std::string& error){return pixel::changePalette(document,1,"#2579ab40",error);},issue);check(recolored.is_object()&&recolored.at("palette")[1]=="#2579AB40"&&recolored.at("frames")[0].at("pixels")[0]==1,"palette edit normalizes RGBA without reindexing pixels");
    const auto recoloredPng=colorStore.exportDocument("translucent",3,false,0,{},issue);check(recoloredPng.is_object(),"recolored version exports independently");const auto recoloredBytes=decode(fs::u8path(recoloredPng.at("path").get<std::string>()),width,height);
    check(recoloredBytes.size()==1024&&recoloredBytes[0]==0x25&&recoloredBytes[1]==0x79&&recoloredBytes[2]==0xAB&&recoloredBytes[3]==0x40,"actual PNG reflects edited palette RGB and alpha");
    check(colorStore.revise("translucent",3,1401,[&](auto& document,std::string& error){return pixel::changePalette(document,0,"#111111FF",error);},issue).is_null()&&issue=="pixel_palette"&&colorStore.read("translucent",issue).at("revision")==3,"opaque transparent index is refused without advancing revision");
    check(colorStore.revise("translucent",3,1402,[&](auto& document,std::string& error){return pixel::changePalette(document,1,"#ZZFFFFFF",error);},issue).is_null()&&issue=="pixel_palette","invalid hex color is refused");
    auto appended=colorStore.revise("translucent",3,1403,[&](auto& document,std::string& error){return pixel::changePalette(document,2,"#FFF000FF",error);},issue);check(appended.is_object()&&appended.at("palette").size()==3&&appended.at("frames")[0].at("pixels")[0]==1,"append at exact palette boundary keeps prior indices");
    check(colorStore.undo("translucent",4,1404,issue).at("palette").size()==2,"palette append participates in persisted undo");
    auto paletteLimit=appended;while(paletteLimit.at("palette").size()<32){const auto i=static_cast<int>(paletteLimit.at("palette").size());if(!pixel::changePalette(paletteLimit,i,"#AABBCCDD",issue))throw std::runtime_error("palette append unexpectedly failed");}
    check(!pixel::changePalette(paletteLimit,32,"#FFEEDDFF",issue)&&issue=="pixel_palette_limit"&&paletteLimit.at("palette").size()==32,"palette cannot exceed32 colors");
    check(!pixel::changePalette(appended,8,"#FFEEDDFF",issue)&&issue=="pixel_palette_index","palette append cannot leave sparse indices");
    check(pixel::changePalette(appended,0,"#FFFFFF00",issue)&&appended.at("palette")[0]=="#00000000","transparent zero stays normalized after palette operation");
    PluginJson presentation={{"workspace_root","D:/safe/drafts"},{"surface",{{"visible",true},{"document_id","flower"},{"revision",5},{"scale",3},{"left",100},{"top",200}}},{"document",{{"pixels","untrusted saved grid is ignored"}}}};
    const auto reopening=pixel::desktopRestoreRequest(presentation,"D:/safe/drafts",true,true,issue);check(reopening.is_object()&&reopening.at("id")=="flower"&&reopening.at("revision")==5&&reopening.at("left")==100&&!reopening.contains("document"),"restore request keeps only matching local presentation metadata");
    check(pixel::desktopRestoreRequest(presentation,"D:/safe/drafts",false,true,issue).is_null()&&issue=="pixel_restore_disabled","desktop restore can be explicitly disabled");
    check(pixel::desktopRestoreRequest(presentation,"D:/safe/drafts",true,false,issue).is_null()&&issue=="pixel_restore_headless","headless runtime does not queue saved presentation");
    check(pixel::desktopRestoreRequest(presentation,"D:/other/drafts",true,true,issue).is_null()&&issue=="pixel_restore_workspace_mismatch","saved presentation never crosses workspace roots");
    auto queuedOnly=presentation;queuedOnly["surface"]["visible"]=false;queuedOnly["surface"]["pending"]=true;check(pixel::desktopRestoreRequest(queuedOnly,"D:/safe/drafts",true,true,issue).is_null()&&issue=="pixel_restore_not_visible","unconfirmed pending display is not replayed after restart");
    auto badPresentation=presentation;badPresentation["surface"]["scale"]=1.5;check(pixel::desktopRestoreRequest(badPresentation,"D:/safe/drafts",true,true,issue).is_null()&&issue=="pixel_restore_metadata_invalid","fractional saved scale is not trusted");
    badPresentation=presentation;badPresentation["surface"]["revision"]="5";check(pixel::desktopRestoreRequest(badPresentation,"D:/safe/drafts",true,true,issue).is_null(),"saved revision must retain integer type");
    badPresentation=presentation;badPresentation["surface"]["left"]=9223372036854775807LL;const auto bounded=pixel::desktopRestoreRequest(badPresentation,"D:/safe/drafts",true,true,issue);check(bounded.is_object()&&!bounded.contains("left"),"untrusted extreme saved position is ignored instead of narrowed");
    badPresentation["surface"]["left"]=18446744073709551615ULL;check(!pixel::desktopRestoreRequest(badPresentation,"D:/safe/drafts",true,true,issue).contains("left"),"unsigned saved position cannot wrap into a valid negative coordinate");
    const auto redoRoot=root/"redo-fixture";fs::create_directories(redoRoot);pixel::PixelStore redoStore;check(redoStore.configure(redoRoot,4,issue)&&redoStore.create(arguments("state"),1500,issue).is_object(),"redo fixture starts as a normal persisted artwork");
    const auto point=PluginJson::array({{{"op","pixel"},{"x",0},{"y",0},{"color",5}}});
    check(redoStore.revise("state",1,1501,[&](auto& document,std::string& error){return pixel::editDocument(document,0,point,error);},issue).is_object(),"redo fixture records actual drawing change");
    check(redoStore.revise("state",2,1502,[&](auto& document,std::string& error){return pixel::changeFrame(document,{{"action","copy"},{"duration_ms",350}},error);},issue).is_object(),"redo fixture records actual frame change");
    check(redoStore.revise("state",3,1503,[&](auto& document,std::string& error){return pixel::changePalette(document,5,"#DDBB6680",error);},issue).is_object(),"redo fixture records actual palette change");
    auto history=redoStore.undo("state",4,1504,issue);check(history.is_object()&&history.at("revision")==5&&history.at("palette")[5]=="#5D7F67FF","undo restores preceding actual palette");
    history=redoStore.redo("state",5,1505,issue);check(history.is_object()&&history.at("revision")==6&&history.at("palette")[5]=="#DDBB6680","redo restores palette with a new revision");
    check(redoStore.undo("state",6,1506,issue).is_object(),"redo can be undone through the same history");
    history=redoStore.undo("state",7,1507,issue);check(history.is_object()&&history.at("frames").size()==1,"second undo removes the copied frame");
    history=redoStore.redo("state",8,1508,issue);check(history.is_object()&&history.at("frames").size()==2&&history.at("frames")[1].at("duration_ms")==350,"redo restores copied frame and its timing");
    check(redoStore.undo("state",9,1509,issue).is_object(),"redo stack retained before restart");
    pixel::PixelStore restartedRedo;check(restartedRedo.configure(redoRoot,4,issue),"restart binds persisted redo fixture");history=restartedRedo.redo("state",10,1510,issue);check(history.is_object()&&history.at("revision")==11&&history.at("frames").size()==2,"redo stack survives independent store reconstruction");
    check(restartedRedo.undo("state",11,1511,issue).is_object(),"frame undo still works after restart");history=restartedRedo.undo("state",12,1512,issue);check(history.is_object()&&history.at("frames")[0].at("pixels")[0]==0,"drawing undo restores original transparent pixel");
    check(restartedRedo.revise("state",13,1513,[&](auto& document,std::string& error){return pixel::changePalette(document,0,"#FFFFFFFF",error);},issue).is_null()&&issue=="pixel_palette","failed new edit does not consume pending redo");
    history=restartedRedo.redo("state",13,1514,issue);check(history.is_object()&&history.at("frames")[0].at("pixels")[0]==5,"redo restores drawing pixels after a rejected edit");
    const auto redonePng=restartedRedo.exportDocument("state",14,false,0,{},issue);check(redonePng.is_object(),"redone pixels can be exported as a real artifact");const auto redoneBytes=decode(fs::u8path(redonePng.at("path").get<std::string>()),width,height);check(redoneBytes[0]==0x5D&&redoneBytes[1]==0x7F&&redoneBytes[2]==0x67&&redoneBytes[3]==255,"actual PNG reflects the redone drawing state");
    check(restartedRedo.revise("state",14,1515,[&](auto& document,std::string& error){return pixel::changePalette(document,5,"#223344FF",error);},issue).is_object(),"new edit creates a new history branch");
    check(restartedRedo.redo("state",15,1516,issue).is_null()&&issue=="pixel_redo_empty","new edit invalidates previous redo branch");
    check(restartedRedo.redo("state",14,1517,issue).is_null()&&issue=="pixel_revision_conflict","stale redo revision is refused before any history change");
    const auto historyPath=redoRoot/"pixel-canvas/documents/state.json";auto legacy=PluginJson::parse(bytes(historyPath));check(legacy.at("undo").size()+legacy.at("redo").size()<=8,"undo and redo share one eight-snapshot capacity");legacy.erase("redo");{std::ofstream file(historyPath,std::ios::binary);file<<legacy.dump();}
    check(restartedRedo.read("state",issue).is_object(),"legacy format1 records without redo remain readable");check(restartedRedo.revise("state",15,1518,[&](auto& document,std::string& error){return pixel::editDocument(document,0,PluginJson::array({{{"op","clear"}}}),error);},issue).is_object()&&PluginJson::parse(bytes(historyPath)).at("redo").is_array(),"legacy redo metadata is normalized only on an actual write");
    auto corruptHistory=PluginJson::parse(bytes(historyPath));const auto oldState=corruptHistory.at("undo")[0];corruptHistory["undo"]=PluginJson::array();for(int i=0;i<8;++i)corruptHistory["undo"].push_back(oldState);corruptHistory["redo"]=PluginJson::array({oldState});{std::ofstream file(historyPath,std::ios::binary);file<<corruptHistory.dump();}const auto invalidHistoryBytes=bytes(historyPath);
    check(restartedRedo.redo("state",16,1519,issue).is_null()&&issue=="pixel_existing_document_invalid"&&bytes(historyPath)==invalidHistoryBytes,"corrupt combined history overcapacity is never rewritten or normalized");
    std::cout<<"Pixel canvas passed "<<checks<<" checks\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAILED "<<error.what()<<'\n';return 1;}
}
