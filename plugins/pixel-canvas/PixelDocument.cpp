#include "PixelDocument.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <queue>
namespace hibernation::pixel {
namespace {
bool integer(const PluginJson& value,int low,int high){return value.is_number_integer()&&value.get<std::int64_t>()>=low&&value.get<std::int64_t>()<=high;}
bool size(int value){return value==16||value==32||value==64;}
bool text(const PluginJson& value,std::size_t max){return value.is_string()&&!value.get_ref<const std::string&>().empty()&&value.get_ref<const std::string&>().size()<=max&&value.get_ref<const std::string&>().find('\0')==std::string::npos;}
bool color(const PluginJson& value){if(!value.is_string())return false;const auto& s=value.get_ref<const std::string&>();return s.size()==9&&s[0]=='#'&&std::all_of(s.begin()+1,s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
bool palette(const PluginJson& value){if(!value.is_array()||value.size()<2||value.size()>32)return false;for(const auto& c:value)if(!color(c))return false;return value[0].get<std::string>().substr(7)=="00";}
bool allowed(const PluginJson& value,std::initializer_list<const char*> keys){if(!value.is_object())return false;for(auto it=value.begin();it!=value.end();++it)if(std::none_of(keys.begin(),keys.end(),[&](const char* key){return it.key()==key;}))return false;return true;}
int get(const PluginJson& value,const char* name,int low,int high,std::string& issue){if(!value.contains(name)||!integer(value.at(name),low,high)){issue="pixel_invalid_command";return -1;}return value.at(name).get<int>();}
}
bool validId(const std::string& id){return !id.empty()&&id.size()<=48&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';});}
PluginJson defaultPalette(){return PluginJson::array({"#00000000","#293438FF","#F7F5ECFF","#8D9895FF","#C8D2C4FF","#5D7F67FF","#89AC72FF","#B8D391FF","#E0CB83FF","#A98453FF","#705849FF","#D68D82FF","#EFBBB0FF","#9E5C73FF","#BC8DAAFF","#D6BBD6FF","#5C7894FF","#8BAAC4FF","#BBD6DAFF","#EEE1AEFF","#E8A65DFF","#C86B56FF","#665D7DFF","#ADA5BEFF"});}
bool validateDocument(const PluginJson& d,std::string& issue){
    const auto fail=[&]{issue="pixel_document_invalid";return false;};
    try{
    if(!d.is_object()||!d.contains("format_version")||!integer(d.at("format_version"),1,1)||!d.contains("id")||!text(d.at("id"),48)||!validId(d.at("id"))||!d.contains("title")||!text(d.at("title"),240))return fail();
    if(!d.contains("width")||!integer(d.at("width"),16,64)||!size(d.at("width"))||!d.contains("height")||!integer(d.at("height"),16,64)||!size(d.at("height")))return fail();
    if(!d.contains("revision")||!integer(d.at("revision"),1,1000000000)||!d.contains("palette")||!palette(d.at("palette")))return fail();
    if(!d.contains("frames")||!d.at("frames").is_array()||d.at("frames").empty()||d.at("frames").size()>8)return fail();
    const auto count=d.at("width").get<int>()*d.at("height").get<int>();
    for(const auto& frame:d.at("frames")){if(!frame.is_object()||!frame.contains("duration_ms")||!integer(frame.at("duration_ms"),50,5000)||!frame.contains("pixels")||!frame.at("pixels").is_array()||frame.at("pixels").size()!=count)return fail();for(const auto& p:frame.at("pixels"))if(!integer(p,0,static_cast<int>(d.at("palette").size())-1))return fail();}
    for(const auto* field:{"created_ms","updated_ms"})if(!d.contains(field)||!d.at(field).is_number_integer()||d.at(field).get<std::int64_t>()<0)return fail();
    issue.clear();return true;
    }catch(...){return fail();}
}
PluginJson createDocument(const PluginJson& args,std::int64_t now,std::string& issue){
    if(!args.is_object()||!args.contains("id")||!text(args.at("id"),48)||!validId(args.at("id"))){issue="pixel_identity";return nullptr;}
    if(!args.contains("title")||!text(args.at("title"),240)||args.at("title").get_ref<const std::string&>().find_first_not_of(" \t\r\n")==std::string::npos){issue="pixel_title";return nullptr;}
    if(!args.contains("width")||!integer(args.at("width"),16,64)||!size(args.at("width"))||!args.contains("height")||!integer(args.at("height"),16,64)||!size(args.at("height"))){issue="pixel_canvas_size";return nullptr;}
    auto colors=args.value("palette",defaultPalette());if(!palette(colors)){issue="pixel_palette";return nullptr;}
    for(auto& c:colors){auto s=c.get<std::string>();std::transform(s.begin(),s.end(),s.begin(),[](unsigned char x){return static_cast<char>(std::toupper(x));});c=std::move(s);}colors[0]="#00000000";
    const auto w=args.at("width").get<int>(),h=args.at("height").get<int>();
    PluginJson d={{"format_version",1},{"id",args.at("id")},{"title",args.at("title")},{"width",w},{"height",h},{"palette",colors},{"revision",1},{"created_ms",now},{"updated_ms",now},{"frames",PluginJson::array({{{"duration_ms",200},{"pixels",std::vector<int>(static_cast<std::size_t>(w)*h,0)}}})}};
    issue.clear();return d;
}
bool editDocument(PluginJson& d,int frame,const PluginJson& commands,std::string& issue){
    if(frame<0||frame>=static_cast<int>(d.at("frames").size())){issue="pixel_frame_index";return false;}
    if(!commands.is_array()||commands.empty()||commands.size()>256){issue="pixel_command_limit";return false;}
    const int w=d.at("width"),h=d.at("height"),maxColor=static_cast<int>(d.at("palette").size())-1;
    auto changed=d.at("frames").at(frame).at("pixels").get<std::vector<int>>();
    const auto put=[&](int x,int y,int c){changed[static_cast<std::size_t>(y)*w+x]=c;};
    for(const auto& command:commands){
        issue.clear();if(!command.is_object()||!command.contains("op")||!command.at("op").is_string()){issue="pixel_invalid_command";return false;}
        const auto op=command.at("op").get<std::string>();
        if(op=="clear"){
            if(!allowed(command,{"op","color"})||(command.contains("color")&&!integer(command.at("color"),0,maxColor))){issue="pixel_invalid_command";return false;}
            std::fill(changed.begin(),changed.end(),command.value("color",0));continue;
        }
        if(op!="pixel"&&op!="line"&&op!="rect"&&op!="fill"){issue="pixel_invalid_command";return false;}
        if(!allowed(command,op=="rect"?std::initializer_list<const char*>{"op","x","y","x2","y2","color","filled"}:op=="line"?std::initializer_list<const char*>{"op","x","y","x2","y2","color"}:std::initializer_list<const char*>{"op","x","y","color"})){issue="pixel_invalid_command";return false;}
        const int x=get(command,"x",0,w-1,issue),y=get(command,"y",0,h-1,issue),c=get(command,"color",0,maxColor,issue);if(!issue.empty())return false;
        if(op=="pixel"){put(x,y,c);continue;}
        if(op=="fill"){
            const int old=changed[static_cast<std::size_t>(y)*w+x];if(old==c)continue;
            std::queue<std::pair<int,int>> pending;pending.push({x,y});put(x,y,c);
            while(!pending.empty()){const auto [px,py]=pending.front();pending.pop();for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){const int nx=px+dx,ny=py+dy;if(nx<0||nx>=w||ny<0||ny>=h||changed[static_cast<std::size_t>(ny)*w+nx]!=old)continue;put(nx,ny,c);pending.push({nx,ny});}}
            continue;
        }
        const int x2=get(command,"x2",0,w-1,issue),y2=get(command,"y2",0,h-1,issue);if(!issue.empty())return false;
        if(op=="rect"){
            if(command.contains("filled")&&!command.at("filled").is_boolean()){issue="pixel_invalid_command";return false;}
            const bool fill=command.value("filled",false);for(int py=std::min(y,y2);py<=std::max(y,y2);++py)for(int px=std::min(x,x2);px<=std::max(x,x2);++px)if(fill||px==std::min(x,x2)||px==std::max(x,x2)||py==std::min(y,y2)||py==std::max(y,y2))put(px,py,c);
        }else{
            int px=x,py=y,dx=std::abs(x2-x),sx=x<x2?1:-1,dy=-std::abs(y2-y),sy=y<y2?1:-1,error=dx+dy;
            for(;;){put(px,py,c);if(px==x2&&py==y2)break;const int twice=2*error;if(twice>=dy){error+=dy;px+=sx;}if(twice<=dx){error+=dx;py+=sy;}}
        }
    }
    d["frames"][frame]["pixels"]=std::move(changed);issue.clear();return true;
}
bool changeFrame(PluginJson& d,const PluginJson& args,std::string& issue){
    const auto action=args.value("action",std::string{});auto& frames=d.at("frames");const int index=args.value("index",static_cast<int>(frames.size())-1);
    if(index<0||index>=static_cast<int>(frames.size())){issue="pixel_frame_index";return false;}
    if(args.contains("duration_ms")&&!integer(args.at("duration_ms"),50,5000)){issue="pixel_frame_duration";return false;}
    if(action=="remove"){if(frames.size()==1){issue="pixel_last_frame";return false;}frames.erase(frames.begin()+index);}
    else if(action=="timing")frames[index]["duration_ms"]=args.value("duration_ms",200);
    else if(action=="add"||action=="copy"){
        if(frames.size()>=8){issue="pixel_frame_limit";return false;}auto added=frames[index];if(action=="add")added["pixels"]=std::vector<int>(static_cast<std::size_t>(d.at("width").get<int>())*d.at("height").get<int>(),0);added["duration_ms"]=args.value("duration_ms",action=="add"?200:added.at("duration_ms").get<int>());frames.insert(frames.begin()+index+1,added);
    }else{issue="pixel_frame_action";return false;}
    issue.clear();return true;
}
bool changePalette(PluginJson& d,int index,const std::string& rgba,std::string& issue){
    if(!color(PluginJson(rgba))||(index==0&&rgba.substr(7)!="00")){issue="pixel_palette";return false;}
    auto& colors=d.at("palette");if(index<0||index>static_cast<int>(colors.size())){issue="pixel_palette_index";return false;}
    if(index==static_cast<int>(colors.size())&&colors.size()>=32){issue="pixel_palette_limit";return false;}
    std::string normalized=rgba;std::transform(normalized.begin(),normalized.end(),normalized.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});if(index==0)normalized="#00000000";
    if(index==static_cast<int>(colors.size()))colors.push_back(normalized);else colors[index]=normalized;
    issue.clear();return true;
}
PluginJson metadata(const PluginJson& d){PluginJson value;for(const auto* key:{"id","title","width","height","revision","created_ms","updated_ms"})value[key]=d.at(key);value["frames"]=d.at("frames").size();return value;}
std::vector<std::uint8_t> rgbaPixels(const PluginJson& d,bool sheet,int frame,int& width,int& height){
    const int w=d.at("width"),h=d.at("height"),count=sheet?static_cast<int>(d.at("frames").size()):1;width=w*count;height=h;
    if(frame<0||frame>=static_cast<int>(d.at("frames").size()))return {};
    std::vector<std::array<std::uint8_t,4>> colors;for(const auto& c:d.at("palette")){const auto s=c.get<std::string>();std::array<std::uint8_t,4> rgba{};for(int i=0;i<4;++i)rgba[i]=static_cast<std::uint8_t>(std::stoul(s.substr(1+i*2,2),nullptr,16));colors.push_back(rgba);}
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width)*height*4);
    for(int f=0;f<count;++f){const auto& pixels=d.at("frames")[sheet?f:frame].at("pixels");for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto c=colors.at(pixels[y*w+x].get<int>());const auto offset=(static_cast<std::size_t>(y)*width+f*w+x)*4;std::copy(c.begin(),c.end(),bytes.begin()+offset);}}
    return bytes;
}
}
