#pragma once
#include <AY2D/FrameTimeline2D.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <random>
#include <string>
#include <cmath>
#include <optional>
#include <array>

namespace hibernation::character {
using Json=nlohmann::json;
struct ImageRef {std::string image;int x=0,y=0,w=0,h=0;};
struct Frame:ImageRef {std::uint32_t ms=100;std::optional<ImageRef> front;std::optional<std::array<int,2>> hand;};
struct Clip {std::vector<Frame> frames;ayt::ay2d::FrameTimeline2D timeline;};
struct Variant {std::string id;unsigned weight=1;Clip start,loop,end;};
struct Pack {
    std::string id;std::filesystem::path root;int width=32,height=32,anchorX=16,anchorY=32;std::array<int,4> hitbox{0,0,32,32};bool recolor=false;
    std::map<std::string,std::vector<Variant>> states;std::map<std::string,std::string> fallbacks;std::optional<ImageRef> mouth;
    std::map<std::string,std::filesystem::path> files;
    std::string resolve(std::string state)const {std::set<std::string> seen;while(!states.contains(state)&&seen.insert(state).second){auto next=fallbacks.find(state);if(next==fallbacks.end())return "idle";state=next->second;}return states.contains(state)?state:"idle";}
};
inline void require(bool ok){if(!ok)throw std::runtime_error("character_pack_invalid");}
inline void keys(const Json& j,std::initializer_list<const char*> allowed){require(j.is_object());for(auto it=j.begin();it!=j.end();++it)require(std::find(allowed.begin(),allowed.end(),it.key())!=allowed.end());}
inline int integer(const Json& j,int low,int high){require(j.is_number_integer());const auto n=j.get<std::int64_t>();require(n>=low&&n<=high);return static_cast<int>(n);}
inline std::string identifier(const Json& j){require(j.is_string());const auto text=j.get<std::string>();require(!text.empty()&&text.size()<=48&&std::all_of(text.begin(),text.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';}));return text;}
inline std::shared_ptr<Pack> loadPack(const std::filesystem::path& directory){
    auto pack=std::make_shared<Pack>();pack->root=std::filesystem::canonical(directory);const auto manifest=pack->root/"character.json";require(std::filesystem::file_size(manifest)<=256*1024);std::ifstream in(manifest,std::ios::binary);const auto j=Json::parse(in);keys(j,{"version","id","canvas","anchor","hitbox","palette","states","fallbacks","mouth_open"});require(j.at("version")==1);pack->id=identifier(j.at("id"));
    require(j.at("canvas").is_array()&&j.at("canvas").size()==2);pack->width=integer(j.at("canvas")[0],8,512);pack->height=integer(j.at("canvas")[1],8,512);
    require(j.at("anchor").is_array()&&j.at("anchor").size()==2);pack->anchorX=integer(j.at("anchor")[0],0,pack->width);pack->anchorY=integer(j.at("anchor")[1],0,pack->height);
    pack->hitbox={0,0,pack->width,pack->height};if(j.contains("hitbox")){require(j.at("hitbox").is_array()&&j.at("hitbox").size()==4);for(int i=0;i<4;++i)pack->hitbox[i]=integer(j.at("hitbox")[i],0,i%2?pack->height:pack->width);require(pack->hitbox[2]>pack->hitbox[0]&&pack->hitbox[3]>pack->hitbox[1]);}
    const auto palette=j.value("palette",std::string("original"));require(palette=="original"||palette=="builtin");pack->recolor=palette=="builtin";
    std::uintmax_t sourceBytes=0;auto image=[&](const Json& value){ImageRef result;require(value.at("image").is_string());result.image=value.at("image");require(!result.image.empty()&&result.image.size()<=240&&result.image.find('\0')==std::string::npos&&result.image.find(':')==std::string::npos&&result.image.find('\\')==std::string::npos);const auto relative=std::filesystem::u8path(result.image);require(!relative.is_absolute()&&relative.extension()==".png");for(const auto& part:relative)require(part!="..");const auto full=std::filesystem::canonical(pack->root/relative);const auto check=full.lexically_relative(pack->root);require(!check.empty()&&!check.is_absolute());for(const auto& part:check)require(part!="..");if(!pack->files.contains(result.image)){const auto bytes=std::filesystem::file_size(full);require(bytes>0&&bytes<=8*1024*1024&&pack->files.size()<512);sourceBytes+=bytes;require(sourceBytes<=128*1024*1024);pack->files.emplace(result.image,full);}
        result.w=pack->width;result.h=pack->height;if(value.contains("rect")){const auto& r=value.at("rect");require(r.is_array()&&r.size()==4);result.x=integer(r[0],0,16384);result.y=integer(r[1],0,16384);result.w=integer(r[2],1,512);result.h=integer(r[3],1,512);require(result.w==pack->width&&result.h==pack->height);}return result;};
    std::size_t frameCount=0;auto clip=[&](const Json& value,bool loop){Clip result;require(value.is_array()&&value.size()<=256);std::vector<std::uint32_t> times;for(const auto& entry:value){keys(entry,{"image","rect","ms","front","hand"});Frame frame;static_cast<ImageRef&>(frame)=image(entry);frame.ms=entry.contains("ms")?integer(entry.at("ms"),16,60000):100;if(entry.contains("front")){keys(entry.at("front"),{"image","rect"});frame.front=image(entry.at("front"));}if(entry.contains("hand")){const auto& h=entry.at("hand");require(h.is_array()&&h.size()==2);frame.hand=std::array<int,2>{integer(h[0],0,pack->width),integer(h[1],0,pack->height)};}result.frames.push_back(std::move(frame));times.push_back(result.frames.back().ms);require(++frameCount<=4096);}if(!times.empty())require(result.timeline.configure(times,loop));return result;};
    require(j.at("states").is_object()&&j.at("states").size()<=32);for(auto state=j.at("states").begin();state!=j.at("states").end();++state){identifier(state.key());require(state.value().is_array()&&!state.value().empty()&&state.value().size()<=8);std::set<std::string> names;for(const auto& entry:state.value()){keys(entry,{"id","weight","start","loop","end"});Variant variant;variant.id=identifier(entry.at("id"));require(names.insert(variant.id).second);variant.weight=entry.contains("weight")?integer(entry.at("weight"),1,100):1;variant.start=clip(entry.value("start",Json::array()),false);variant.loop=clip(entry.at("loop"),true);require(!variant.loop.frames.empty());variant.end=clip(entry.value("end",Json::array()),false);pack->states[state.key()].push_back(std::move(variant));}}
    require(pack->states.contains("idle"));if(j.contains("fallbacks")){require(j.at("fallbacks").is_object()&&j.at("fallbacks").size()<=32);for(auto it=j.at("fallbacks").begin();it!=j.at("fallbacks").end();++it)pack->fallbacks[identifier(it.key())]=identifier(it.value());for(const auto& [from,to]:pack->fallbacks){std::set<std::string> seen;auto at=from;while(pack->fallbacks.contains(at)){require(seen.insert(at).second);at=pack->fallbacks.at(at);}}}
    if(j.contains("mouth_open")){keys(j.at("mouth_open"),{"image","rect"});pack->mouth=image(j.at("mouth_open"));}return pack;
}
struct Selection {const Frame* frame=nullptr;std::string requested,resolved,variant,phase;std::uint32_t index=0;};
class Animator {
    std::shared_ptr<const Pack> pack_;std::mt19937 random_{std::random_device{}()};std::string requested_,active_,pending_;const Variant* variant_=nullptr;std::string phase_;double start_=0;
    void enter(const std::string& requested,double now){active_=pack_->resolve(requested);const auto& variants=pack_->states.at(active_);unsigned sum=0;for(const auto& variant:variants)sum+=variant.weight;auto roll=random_()%sum;variant_=&variants.front();for(const auto& candidate:variants){if(roll<candidate.weight){variant_=&candidate;break;}roll-=candidate.weight;}phase_=variant_->start.frames.empty()?"loop":"start";start_=now;}
    const Clip& current()const{return phase_=="start"?variant_->start:phase_=="end"?variant_->end:variant_->loop;}
public:
    void bind(std::shared_ptr<const Pack> pack){pack_=std::move(pack);variant_=nullptr;requested_.clear();pending_.clear();}
    Selection select(const std::string& wanted,double now,bool reduced=false){
        if(!pack_)return {};if(!variant_){requested_=wanted;enter(wanted,now);}else if(requested_!=wanted){requested_=wanted;pending_=wanted;const bool urgent=wanted=="dragged"||wanted=="landing"||wanted=="receiving"||wanted=="preparing";if(urgent||variant_->end.frames.empty())enter(wanted,now);else if(phase_!="end"){phase_="end";start_=now;}}
        for(int i=0;i<3&&phase_!="loop";++i){const double duration=current().timeline.durationUs()/1000000.0;if(now-start_<duration)break;start_+=duration;if(phase_=="end")enter(pending_,start_);else phase_="loop";}
        const auto sample=current().timeline.sample(static_cast<std::uint64_t>(std::max(0.0,now-start_)*1000000));const auto index=reduced?0:sample.frame;return {&current().frames.at(index),requested_,active_,variant_->id,phase_,index};
    }
};
}
