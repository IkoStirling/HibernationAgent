#pragma once
#include <Hibernation/CharacterAnimation.h>
#include <Windows.h>
#include <wincodec.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace hibernation::character {
struct Image {int width=0,height=0;std::vector<std::uint32_t> pixels;};
template<class T>struct Com {T* value=nullptr;~Com(){if(value)value->Release();}T** put(){return &value;}T* operator->()const{return value;}};
inline std::shared_ptr<Image> decode(const std::filesystem::path& path){
    const auto apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::shared_ptr<Image> result;
    {Com<IWICImagingFactory> factory;Com<IWICBitmapDecoder> decoder;Com<IWICBitmapFrameDecode> frame;Com<IWICFormatConverter> converter;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())))&&SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.put()))&&SUCCEEDED(decoder->GetFrame(0,frame.put()))){UINT width=0,height=0;if(SUCCEEDED(frame->GetSize(&width,&height))&&width>0&&height>0&&width<=16384&&height<=16384&&std::uint64_t(width)*height<=16*1024*1024&&SUCCEEDED(factory->CreateFormatConverter(converter.put()))&&SUCCEEDED(converter->Initialize(frame.value,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){auto image=std::make_shared<Image>();image->width=width;image->height=height;image->pixels.resize(static_cast<std::size_t>(width)*height);if(SUCCEEDED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(image->pixels.size()*4),reinterpret_cast<BYTE*>(image->pixels.data()))))result=std::move(image);}}}
    if(SUCCEEDED(apartment))CoUninitialize();return result;
}
// One bounded background decoder per selected pack. Paint never reads a file.
// Shared immutable images survive eviction until the current draw is complete.
class Assets {
    std::shared_ptr<const Pack> pack_;mutable std::mutex mutex_;std::condition_variable cv_;std::deque<std::string> queue_;std::set<std::string> queued_,failed_;struct Cached {std::shared_ptr<Image> image;std::uint64_t used;};std::map<std::string,Cached> cache_;std::size_t budget_,bytes_=0;std::uint64_t use_=0;bool stopped_=false;std::thread worker_;
    bool insert(const std::string& name,std::shared_ptr<Image> image){if(!image)return false;const auto needed=image->pixels.size()*4;if(needed>budget_)return false;while(bytes_+needed>budget_){auto oldest=cache_.end();for(auto it=cache_.begin();it!=cache_.end();++it)if(it->second.image.use_count()==1&&(oldest==cache_.end()||it->second.used<oldest->second.used))oldest=it;if(oldest==cache_.end())return false;bytes_-=oldest->second.image->pixels.size()*4;cache_.erase(oldest);}bytes_+=needed;cache_[name]={std::move(image),++use_};return true;}
    void run(){for(;;){std::string name;{std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stopped_||!queue_.empty();});if(stopped_)return;name=queue_.front();queue_.pop_front();}std::shared_ptr<Image> image;try{image=decode(pack_->files.at(name));}catch(...){} {std::lock_guard lock(mutex_);queued_.erase(name);if(!insert(name,std::move(image)))failed_.insert(name);}}}
public:
    Assets(std::shared_ptr<const Pack> pack,std::size_t budget):pack_(std::move(pack)),budget_(budget){const auto& initial=pack_->states.at("idle").front();const auto& frames=initial.start.frames.empty()?initial.loop.frames:initial.start.frames;const auto& ref=frames.front();auto image=decode(pack_->files.at(ref.image));require(image&&ref.x+ref.w<=image->width&&ref.y+ref.h<=image->height);require(insert(ref.image,std::move(image)));
        // Pointer transitions cannot wait for the first asynchronous decode.
        // Warm only the first frame of bounded urgent states, not the whole pack.
        for(const auto* state:{"dragged","dragged_side","landing"})if(pack_->states.contains(state))for(const auto& variant:pack_->states.at(state)){const auto& clip=variant.start.frames.empty()?variant.loop:variant.start;const auto& name=clip.frames.front().image;if(!cache_.contains(name)){auto first=decode(pack_->files.at(name));if(!insert(name,std::move(first)))failed_.insert(name);}}
        worker_=std::thread([this]{run();});}
    ~Assets(){{std::lock_guard lock(mutex_);stopped_=true;}cv_.notify_all();if(worker_.joinable())worker_.join();}
    void request(const std::string& name){std::lock_guard lock(mutex_);if(cache_.contains(name)||failed_.contains(name)||queued_.contains(name)||queue_.size()>=16)return;queued_.insert(name);queue_.push_back(name);cv_.notify_one();}
    std::shared_ptr<const Image> get(const std::string& name){std::lock_guard lock(mutex_);auto found=cache_.find(name);if(found==cache_.end())return {};found->second.used=++use_;return found->second.image;}
    bool failed(const std::string& name)const{std::lock_guard lock(mutex_);return failed_.contains(name);}
    Json status()const{std::lock_guard lock(mutex_);return {{"cache_bytes",bytes_},{"cache_limit_bytes",budget_},{"cached_images",cache_.size()},{"pending_images",queued_.size()},{"failed_images",failed_.size()}};}
};
inline bool regionValid(const ImageRef& ref,const Image& image){return ref.x>=0&&ref.y>=0&&ref.x+ref.w<=image.width&&ref.y+ref.h<=image.height;}
}
