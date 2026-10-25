#pragma once
#include <sapi.h>
#include <sphelper.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include <vector>
namespace hibernation::plugins {
// SAPI writes PCM to bounded, private memory. Playback uses the same device path
// as HTTP speech, rather than SAPI's cached direct-audio output token.
class SpeechMemory final:public IStream {
    std::atomic<ULONG> references_{1};Microsoft::WRL::ComPtr<IStream> inner_;std::mutex mutex_;std::function<bool()> cancelled_;static constexpr ULONGLONG limit=8*1024*1024;
public:
    SpeechMemory(IStream* inner,std::function<bool()> cancelled):inner_(inner),cancelled_(std::move(cancelled)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_ISequentialStream||id==IID_IStream){*out=static_cast<IStream*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--references_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Read(void* data,ULONG bytes,ULONG* read)override{std::lock_guard lock(mutex_);return inner_->Read(data,bytes,read);}
    HRESULT STDMETHODCALLTYPE Write(const void* data,ULONG bytes,ULONG* written)override{std::lock_guard lock(mutex_);if(written)*written=0;if(cancelled_())return STG_E_REVERTED;ULARGE_INTEGER at{};LARGE_INTEGER zero{};if(FAILED(inner_->Seek(zero,STREAM_SEEK_CUR,&at))||at.QuadPart>limit||bytes>limit-at.QuadPart)return STG_E_MEDIUMFULL;return inner_->Write(data,bytes,written);}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,DWORD origin,ULARGE_INTEGER* result)override{std::lock_guard lock(mutex_);ULONGLONG base=0;if(origin==STREAM_SEEK_CUR){ULARGE_INTEGER at{};LARGE_INTEGER zero{};if(FAILED(inner_->Seek(zero,STREAM_SEEK_CUR,&at)))return STG_E_SEEKERROR;base=at.QuadPart;}else if(origin==STREAM_SEEK_END){STATSTG stat{};if(FAILED(inner_->Stat(&stat,STATFLAG_NONAME)))return STG_E_SEEKERROR;base=stat.cbSize.QuadPart;}else if(origin!=STREAM_SEEK_SET)return STG_E_INVALIDFUNCTION;if(base>limit||move.QuadPart<-static_cast<LONGLONG>(base)||move.QuadPart>static_cast<LONGLONG>(limit-base))return STG_E_SEEKERROR;return inner_->Seek(move,origin,result);}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size)override{std::lock_guard lock(mutex_);return size.QuadPart<=limit?inner_->SetSize(size):STG_E_MEDIUMFULL;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Commit(DWORD flags)override{std::lock_guard lock(mutex_);return inner_->Commit(flags);}HRESULT STDMETHODCALLTYPE Revert()override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat,DWORD flags)override{std::lock_guard lock(mutex_);return inner_->Stat(stat,flags);}HRESULT STDMETHODCALLTYPE Clone(IStream**)override{return STG_E_INVALIDFUNCTION;}
};
inline bool renderSpeech(ISpVoice* voice,const std::wstring& text,int rate,const std::function<bool()>& cancelled,WAVEFORMATEX& format,std::vector<char>& pcm){
    using Microsoft::WRL::ComPtr;ComPtr<IStream> backing;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&backing)))return false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);auto stop=[cancelled,deadline]{return cancelled()||std::chrono::steady_clock::now()>=deadline;};
    ComPtr<IStream> memory;memory.Attach(new SpeechMemory(backing.Get(),stop));ComPtr<ISpStream> stream;if(FAILED(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&stream))))return false;
    CSpStreamFormat desired;if(FAILED(desired.AssignFormat(SPSF_22kHz16BitMono)))return false;format=*desired.WaveFormatExPtr();
    if(FAILED(stream->SetBaseStream(memory.Get(),SPDFID_WaveFormatEx,&format))||FAILED(voice->SetOutput(stream.Get(),TRUE)))return false;
    voice->SetRate(rate);voice->SetVolume(100);bool done=false;
    if(SUCCEEDED(voice->Speak(text.c_str(),SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_XML,nullptr)))while(!stop()){const auto status=voice->WaitUntilDone(20);if(status==S_OK){done=true;break;}if(FAILED(status))break;}
    voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);voice->SetOutput(nullptr,TRUE);
    STATSTG stat{};if(!done||cancelled()||FAILED(memory->Stat(&stat,STATFLAG_NONAME))||!stat.cbSize.QuadPart||stat.cbSize.QuadPart>8*1024*1024||stat.cbSize.QuadPart%format.nBlockAlign)return false;
    LARGE_INTEGER zero{};if(FAILED(memory->Seek(zero,STREAM_SEEK_SET,nullptr)))return false;pcm.resize(static_cast<std::size_t>(stat.cbSize.QuadPart));ULONG read=0;if(FAILED(memory->Read(pcm.data(),static_cast<ULONG>(pcm.size()),&read))||read!=pcm.size()){pcm.clear();return false;}return true;
}
}
