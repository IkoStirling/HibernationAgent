#include "PixelStore.h"
#include <AYIO/PathSafety.h>
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
namespace hibernation::pixel {
namespace {
std::string utf8(const std::filesystem::path& path){const auto bytes=path.generic_u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
std::string documentPath(const std::string& id){return "pixel-canvas/documents/"+id+".json";}
bool sameName(const std::string& a,const std::string& b){const auto left=std::filesystem::u8path(a).native(),right=std::filesystem::u8path(b).native();return CompareStringOrdinal(left.data(),static_cast<int>(left.size()),right.data(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;}
bool containsName(const std::vector<std::string>& names,const std::string& name){return std::any_of(names.begin(),names.end(),[&](const auto& candidate){return sameName(candidate,name);});}
std::string lowerAscii(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return text;}
template<class T>using ComPtr=Microsoft::WRL::ComPtr<T>;
std::string png(const std::vector<std::uint8_t>& bytes,int width,int height){
    if(bytes.empty()||width<=0||height<=0||bytes.size()!=static_cast<std::size_t>(width)*height*4)return {};
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::string result;
    {
        ComPtr<IWICImagingFactory> factory;ComPtr<IStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
        do{
            if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))break;
            if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))break;
            if(FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)))break;
            if(FAILED(encoder->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(width,height)))break;
            // Windows' PNG encoder negotiates its native BGRA layout. Convert
            // the owned RGBA buffer explicitly instead of silently accepting a
            // changed channel layout and exporting red/blue-swapped pixels.
            WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;if(FAILED(frame->SetPixelFormat(&format))||format!=GUID_WICPixelFormat32bppBGRA)break;
            auto native=bytes;for(std::size_t i=0;i<native.size();i+=4)std::swap(native[i],native[i+2]);
            if(FAILED(frame->WritePixels(height,width*4,static_cast<UINT>(native.size()),native.data()))||FAILED(frame->Commit())||FAILED(encoder->Commit()))break;
            STATSTG information{};if(FAILED(stream->Stat(&information,STATFLAG_NONAME))||information.cbSize.QuadPart>2*1024*1024)break;
            HGLOBAL handle=nullptr;if(FAILED(GetHGlobalFromStream(stream.Get(),&handle)))break;const auto* data=static_cast<const char*>(GlobalLock(handle));if(!data)break;result.assign(data,information.cbSize.LowPart);GlobalUnlock(handle);
        }while(false);
    }
    if(SUCCEEDED(initialized))CoUninitialize();return result;
}
std::string base64(const std::string& bytes){
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;out.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3){const auto a=static_cast<unsigned char>(bytes[i]);const auto b=i+1<bytes.size()?static_cast<unsigned char>(bytes[i+1]):0;const auto c=i+2<bytes.size()?static_cast<unsigned char>(bytes[i+2]):0;out+=alphabet[a>>2];out+=alphabet[((a&3)<<4)|(b>>4)];out+=i+1<bytes.size()?alphabet[((b&15)<<2)|(c>>6)]:'=';out+=i+2<bytes.size()?alphabet[c&63]:'=';}
    return out;
}
bool recordValid(const PluginJson& record,const std::string& id){
    try{
        if(!record.is_object()||!record.contains("format_version")||!record.at("format_version").is_number_integer()||record.at("format_version")!=1||!record.contains("document")||!record.contains("undo")||!record.at("undo").is_array()||record.at("undo").size()>8)return false;
        if(record.contains("redo")&&(!record.at("redo").is_array()||record.at("undo").size()+record.at("redo").size()>8))return false;
        std::string issue;if(!validateDocument(record.at("document"),issue)||record.at("document").at("id")!=id)return false;
        for(const auto& previous:record.at("undo"))if(!validateDocument(previous,issue)||previous.at("id")!=id||previous.at("revision")>=record.at("document").at("revision"))return false;
        if(record.contains("redo"))for(const auto& next:record.at("redo"))if(!validateDocument(next,issue)||next.at("id")!=id||next.at("revision")>=record.at("document").at("revision"))return false;
        return true;
    }catch(...){return false;}
}
}
bool PixelStore::configure(const std::filesystem::path& draftsRoot,std::size_t maximum,std::string& issue){
    if(maximum<1||maximum>64){issue="pixel_document_limit";return false;}
    std::error_code ec;auto root=std::filesystem::canonical(draftsRoot,ec);if(ec){issue="pixel_workspace_unavailable";return false;}
    ayt::io::Workspace prepared;
    if(!prepared.configure({{"drafts",root,true,true,2*1024*1024,2*1024*1024,{".system"}}},[](const auto& a){return a.principal=="pixel-canvas"&&a.mount=="drafts"&&(a.relativePath=="pixel-canvas"||a.relativePath.starts_with("pixel-canvas/"));},issue)){issue="pixel_workspace_denied";return false;}
    for(const auto* relative:{"pixel-canvas/documents","pixel-canvas/exports"}){std::filesystem::path resolved;if(!ayt::io::path::resolveWithinRoot(root,std::filesystem::path(relative),resolved,issue)){issue="pixel_workspace_denied";return false;}std::filesystem::create_directories(resolved,ec);if(ec||!ayt::io::path::resolveWithinRoot(root,std::filesystem::path(relative),resolved,issue)){issue="pixel_workspace_denied";return false;}}
    files_=std::move(prepared);root_=std::move(root);maximum_=maximum;issue.clear();return true;
}
PluginJson PixelStore::load(const std::string& id,std::string& issue)const{
    if(!validId(id)){issue="pixel_identity";return nullptr;}
    std::vector<std::string> names;std::string contents;
    if(!files_.list("pixel-canvas","drafts","pixel-canvas/documents",names,issue,256)){issue="pixel_workspace_denied";return nullptr;}
    if(!containsName(names,id+".json")){issue="pixel_document_missing";return nullptr;}
    if(!files_.read("pixel-canvas","drafts",documentPath(id),contents,issue)){issue="pixel_read_denied";return nullptr;}
    auto record=PluginJson::parse(contents,nullptr,false);if(!recordValid(record,id)){issue="pixel_existing_document_invalid";return nullptr;}
    if(!record.contains("redo"))record["redo"]=PluginJson::array();issue.clear();return record;
}
bool PixelStore::save(const std::string& id,const PluginJson& record,std::string& issue)const{
    if(!recordValid(record,id)){issue="pixel_document_invalid";return false;}
    if(!files_.write("pixel-canvas","drafts",documentPath(id),record.dump(),issue)){issue="pixel_write_denied";return false;}
    issue.clear();return true;
}
PluginJson PixelStore::list(std::string& issue)const{
    std::vector<std::string> names;if(!files_.list("pixel-canvas","drafts","pixel-canvas/documents",names,issue,256)){issue="pixel_workspace_denied";return nullptr;}
    PluginJson documents=PluginJson::array();std::size_t invalid=0;
    for(const auto& name:names){if(name.size()<=5||!sameName(name.substr(name.size()-5),".json")){++invalid;continue;}std::string ignored;const auto record=load(lowerAscii(name.substr(0,name.size()-5)),ignored);if(!record.is_object()){++invalid;continue;}documents.push_back(metadata(record.at("document")));}
    std::sort(documents.begin(),documents.end(),[](const auto& a,const auto& b){if(a.at("updated_ms")!=b.at("updated_ms"))return a.at("updated_ms")>b.at("updated_ms");return a.at("id")<b.at("id");});
    issue.clear();return {{"documents",documents},{"maximum",maximum_},{"occupied_slots",names.size()},{"invalid_slots",invalid},{"content_kind","authored_pixel_art"}};
}
PluginJson PixelStore::read(const std::string& id,std::string& issue)const{const auto record=load(id,issue);return record.is_object()?record.at("document"):PluginJson(nullptr);}
PluginJson PixelStore::create(const PluginJson& args,std::int64_t now,std::string& issue){
    auto document=createDocument(args,now,issue);if(document.is_null())return nullptr;const auto id=document.at("id").get<std::string>();
    std::vector<std::string> names;if(!files_.list("pixel-canvas","drafts","pixel-canvas/documents",names,issue,256)){issue="pixel_workspace_denied";return nullptr;}
    if(containsName(names,id+".json")){issue="pixel_document_exists";return nullptr;}
    if(names.size()>=maximum_){issue="pixel_document_limit";return nullptr;}
    if(!save(id,{{"format_version",1},{"document",document},{"undo",PluginJson::array()},{"redo",PluginJson::array()}},issue))return nullptr;
    return document;
}
PluginJson PixelStore::revise(const std::string& id,std::int64_t expected,std::int64_t now,const std::function<bool(PluginJson&,std::string&)>& change,std::string& issue){
    auto record=load(id,issue);if(record.is_null())return nullptr;const auto before=record.at("document");const auto revision=before.at("revision").get<std::int64_t>();
    if(expected!=revision){issue="pixel_revision_conflict";return nullptr;}if(revision>=1000000000){issue="pixel_revision_limit";return nullptr;}
    auto changed=before;if(!change(changed,issue))return nullptr;changed["revision"]=revision+1;changed["updated_ms"]=std::max(now,before.at("updated_ms").get<std::int64_t>());
    if(changed.at("id")!=id||!validateDocument(changed,issue))return nullptr;
    auto& undo=record.at("undo");undo.push_back(before);while(undo.size()>8)undo.erase(undo.begin());record["redo"]=PluginJson::array();record["document"]=changed;
    if(!save(id,record,issue))return nullptr;return changed;
}
PluginJson PixelStore::undo(const std::string& id,std::int64_t expected,std::int64_t now,std::string& issue){
    auto record=load(id,issue);if(record.is_null())return nullptr;const auto revision=record.at("document").at("revision").get<std::int64_t>();
    if(expected!=revision){issue="pixel_revision_conflict";return nullptr;}if(revision>=1000000000){issue="pixel_revision_limit";return nullptr;}
    auto& history=record.at("undo");if(history.empty()){issue="pixel_undo_empty";return nullptr;}auto changed=history.back();history.erase(history.end()-1);record["redo"].push_back(record.at("document"));changed["revision"]=revision+1;changed["updated_ms"]=std::max(now,record.at("document").at("updated_ms").get<std::int64_t>());record["document"]=changed;
    if(!save(id,record,issue))return nullptr;return changed;
}
PluginJson PixelStore::redo(const std::string& id,std::int64_t expected,std::int64_t now,std::string& issue){
    auto record=load(id,issue);if(record.is_null())return nullptr;const auto revision=record.at("document").at("revision").get<std::int64_t>();
    if(expected!=revision){issue="pixel_revision_conflict";return nullptr;}if(revision>=1000000000){issue="pixel_revision_limit";return nullptr;}
    auto& history=record.at("redo");if(history.empty()){issue="pixel_redo_empty";return nullptr;}auto changed=history.back();history.erase(history.end()-1);record["undo"].push_back(record.at("document"));changed["revision"]=revision+1;changed["updated_ms"]=std::max(now,record.at("document").at("updated_ms").get<std::int64_t>());record["document"]=changed;
    if(!save(id,record,issue))return nullptr;return changed;
}
PluginJson PixelStore::exportDocument(const std::string& id,std::int64_t expected,bool sheet,int frame,const std::function<bool()>& cancelled,std::string& issue){
    const auto document=read(id,issue);if(document.is_null())return nullptr;if(document.at("revision")!=expected){issue="pixel_revision_conflict";return nullptr;}
    if(frame<0||frame>=static_cast<int>(document.at("frames").size())){issue="pixel_frame_index";return nullptr;}
    if(cancelled&&cancelled()){issue="cancelled";return nullptr;}
    int width=0,height=0;const auto pixels=rgbaPixels(document,sheet,frame,width,height);const auto encoded=png(pixels,width,height);if(encoded.empty()){issue="pixel_png_encoding";return nullptr;}
    const auto stem=id+"-r"+std::to_string(expected)+(sheet?"-sheet":"-f"+std::to_string(frame));const auto relative="pixel-canvas/exports/"+stem+".png";
    std::vector<std::string> names;if(!files_.list("pixel-canvas","drafts","pixel-canvas/exports",names,issue,1024)){issue="pixel_workspace_denied";return nullptr;}
    std::vector<std::pair<std::string,std::string>> outputs{{relative,encoded}};
    PluginJson result={{"path",utf8(root_/relative)},{"relative_path",relative},{"source_mount","drafts"},{"source_path",relative},{"id",id},{"revision",expected},{"width",width},{"height",height},{"frame_count",sheet?document.at("frames").size():1},{"written",true},{"completed",true},{"content_kind","authored_pixel_art"}};
    if(sheet){
        PluginJson frames=PluginJson::array();for(std::size_t i=0;i<document.at("frames").size();++i)frames.push_back({{"x",i*document.at("width").get<int>()},{"y",0},{"width",document.at("width")},{"height",document.at("height")},{"duration_ms",document.at("frames")[i].at("duration_ms")}});
        const auto manifestRelative="pixel-canvas/exports/"+stem+".json";const PluginJson manifest={{"format_version",1},{"kind","pixel_sequence"},{"image",stem+".png"},{"title",document.at("title")},{"document_id",id},{"revision",expected},{"frame_width",document.at("width")},{"frame_height",document.at("height")},{"frames",frames}};
        outputs.emplace_back(manifestRelative,manifest.dump(2));result["manifest_path"]=utf8(root_/manifestRelative);result["manifest_relative_path"]=manifestRelative;
    }
    // Preflight all known collisions and the combined file capacity before
    // publishing either member of a sheet/manifest pair. Each file publication
    // is atomic; an actual I/O failure during the pair can still leave its PNG.
    std::vector<const std::pair<std::string,std::string>*> pending;
    for(const auto& output:outputs){const auto filename=std::filesystem::path(output.first).filename().string();if(containsName(names,filename)){std::string existing;if(!files_.read("pixel-canvas","drafts",output.first,existing,issue)||existing!=output.second){issue="pixel_export_exists";return nullptr;}}else pending.push_back(&output);}
    if(!pending.empty()&&names.size()+pending.size()>256){issue="pixel_export_limit";return nullptr;}
    for(const auto* output:pending){if(cancelled&&cancelled()){issue="cancelled";return nullptr;}if(!files_.write("pixel-canvas","drafts",output->first,output->second,issue)){issue="pixel_export_denied";return nullptr;}}
    issue.clear();return result;
}
PluginJson PixelStore::preview(const std::string& id,int frame,std::string& issue)const{
    const auto document=read(id,issue);if(document.is_null())return nullptr;if(frame<0||frame>=static_cast<int>(document.at("frames").size())){issue="pixel_frame_index";return nullptr;}
    int width=0,height=0;const auto original=rgbaPixels(document,false,frame,width,height);const int scale=256/std::max(width,height),targetWidth=width*scale,targetHeight=height*scale;
    std::vector<std::uint8_t> enlarged(static_cast<std::size_t>(targetWidth)*targetHeight*4);for(int y=0;y<targetHeight;++y)for(int x=0;x<targetWidth;++x){const auto source=(static_cast<std::size_t>(y/scale)*width+x/scale)*4,target=(static_cast<std::size_t>(y)*targetWidth+x)*4;std::copy_n(original.begin()+source,4,enlarged.begin()+target);}
    const auto encoded=png(enlarged,targetWidth,targetHeight);if(encoded.empty()){issue="pixel_png_encoding";return nullptr;}
    issue.clear();return {{"image",{{"mime_type","image/png"},{"base64",base64(encoded)}}},{"id",id},{"revision",document.at("revision")},{"frame",frame},{"width",targetWidth},{"height",targetHeight},{"original_width",width},{"original_height",height},{"source_mount","drafts"},{"source_path",documentPath(id)},{"content_kind","authored_pixel_art"},{"perception_note","自己实际绘制的一帧像素作品，使用最近邻放大；是创作画面，不是外部经历。"}};
}
}
