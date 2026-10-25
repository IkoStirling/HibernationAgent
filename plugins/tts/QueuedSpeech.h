#pragma once

// One waveOut stream per utterance. The producer may reserve at most three
// segments, including the segment in synthesis and buffers already playing.
// Audio headers own stable storage until WHDR_DONE; page changes never reopen
// the device or purge queued audio. No dialogue/audio cache is written to disk.
struct QueuedSpeechResult {
    bool started=false,failed=false,limited=false;
    double elapsed=0,firstMs=0,peak=0,seconds=0;
    unsigned segments=0,maximumBuffered=0,underruns=0;
};
template<class Render,class Started,class Progress>
QueuedSpeechResult playQueuedSpeech(const std::string& text,double maximum,int volume,
    const std::function<bool()>& cancelled,Render render,Started started,Progress progress){
    struct Piece{Wav audio;std::size_t character=0;};
    struct Shared{std::mutex mutex;std::condition_variable cv;std::deque<Piece> ready;bool done=false,failed=false,limited=false;unsigned buffered=0,maximumBuffered=0,segments=0;} shared;
    std::atomic<bool> abort=false;
    auto stopped=[&]{return abort.load()||cancelled();};
    const auto preparation=std::chrono::steady_clock::now();
    auto producer=std::async(std::launch::async,[&]{
        double seconds=0;std::size_t character=0;
        try{
            for(const auto& part:textPages(text,24)){
                {std::unique_lock lock(shared.mutex);while(shared.buffered>=3&&!stopped())shared.cv.wait_for(lock,std::chrono::milliseconds(10));if(stopped())break;++shared.buffered;shared.maximumBuffered=std::max(shared.maximumBuffered,shared.buffered);}
                auto audio=render(part,std::max(1.0,maximum-seconds),stopped);
                if(!audio){std::lock_guard lock(shared.mutex);--shared.buffered;shared.failed=!stopped();break;}
                const auto count=textOffsets(part).size()-1;
                if(audio->pcm.empty()){std::lock_guard lock(shared.mutex);--shared.buffered;character+=count;continue;}
                const auto remaining=static_cast<std::size_t>(std::max(0.0,maximum-seconds)*audio->format.nSamplesPerSec)*audio->format.nBlockAlign;
                if(audio->pcm.size()>remaining)audio->pcm.resize(remaining);
                audio->seconds=static_cast<double>(audio->pcm.size())/audio->format.nAvgBytesPerSec;
                seconds+=audio->seconds;
                {std::lock_guard lock(shared.mutex);if(stopped()){--shared.buffered;break;}shared.ready.push_back({std::move(*audio),character});++shared.segments;if(seconds>=maximum-1.0/24000)shared.limited=true;}
                shared.cv.notify_all();character+=count;if(seconds>=maximum-1.0/24000)break;
            }
        }catch(...){std::lock_guard lock(shared.mutex);shared.failed=!stopped();}
        {std::lock_guard lock(shared.mutex);shared.done=true;}shared.cv.notify_all();
    });
    struct Block{Wav audio;WAVEHDR header{};};
    std::deque<std::unique_ptr<Block>> blocks;
    std::vector<std::pair<double,std::size_t>> cues;
    HWAVEOUT output=nullptr;WAVEFORMATEX format{};QueuedSpeechResult result;
    double duration=0,lastPosition=0;const auto total=textOffsets(text).size()-1;
    auto speaking=preparation,nextProgress=preparation;bool wasEmpty=false;
    while(!cancelled()){
        std::optional<Piece> next;bool done=false,failed=false;
        {std::lock_guard lock(shared.mutex);if(!shared.ready.empty()){next=std::move(shared.ready.front());shared.ready.pop_front();}done=shared.done;failed=shared.failed;}
        if(failed){result.failed=true;break;}
        if(next){
            auto block=std::make_unique<Block>();block->audio=std::move(next->audio);
            if(!output){format=block->audio.format;if(waveOutOpen(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){output=nullptr;result.failed=true;break;}if(waveOutPause(output)!=MMSYSERR_NOERROR){result.failed=true;break;}}
            else if(std::memcmp(&format,&block->audio.format,16)!=0){result.failed=true;break;}
            for(const auto& [at,n]:block->audio.cues)cues.emplace_back(duration+at,next->character+n);
            duration+=block->audio.seconds;
            for(std::size_t at=0;at+2<=block->audio.pcm.size();at+=2){int16_t value;std::memcpy(&value,block->audio.pcm.data()+at,2);result.peak=std::max(result.peak,std::abs(value/32768.0));}
            gain(block->audio,volume);block->header.lpData=block->audio.pcm.data();block->header.dwBufferLength=static_cast<DWORD>(block->audio.pcm.size());
            blocks.push_back(std::move(block));auto& header=blocks.back()->header;
            if(waveOutPrepareHeader(output,&header,sizeof(header))!=MMSYSERR_NOERROR||waveOutWrite(output,&header,sizeof(header))!=MMSYSERR_NOERROR){result.failed=true;break;}
            if(!result.started){result.firstMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparation).count();speaking=std::chrono::steady_clock::now();nextProgress=speaking;result.started=true;started(duration,result.firstMs,result.peak);if(waveOutRestart(output)!=MMSYSERR_NOERROR){result.failed=true;break;}}
            if(wasEmpty){++result.underruns;wasEmpty=false;}
        }
        while(!blocks.empty()&&(blocks.front()->header.dwFlags&WHDR_DONE)){
            waveOutUnprepareHeader(output,&blocks.front()->header,sizeof(WAVEHDR));blocks.pop_front();{std::lock_guard lock(shared.mutex);--shared.buffered;}shared.cv.notify_all();
        }
        const auto now=std::chrono::steady_clock::now();
        const bool finished=result.started&&done&&blocks.empty()&&!next;
        if(result.started&&(now>=nextProgress||finished)){
            MMTIME position{};position.wType=TIME_SAMPLES;double seconds=lastPosition;
            if(finished)seconds=duration;else if(waveOutGetPosition(output,&position,sizeof(position))==MMSYSERR_NOERROR){if(position.wType==TIME_SAMPLES)seconds=static_cast<double>(position.u.sample)/format.nSamplesPerSec;else if(position.wType==TIME_BYTES)seconds=static_cast<double>(position.u.cb)/format.nAvgBytesPerSec;else if(position.wType==TIME_MS)seconds=position.u.ms/1000.0;}
            seconds=std::clamp(seconds,lastPosition,duration);lastPosition=seconds;std::size_t visible=0,before=0;double previous=0;
            for(const auto& [at,n]:cues){if(seconds>=at){visible=n;previous=at;before=n;continue;}if(at>previous)visible=before+static_cast<std::size_t>((n-before)*(seconds-previous)/(at-previous));break;}
            unsigned segments=0,buffered=0;bool complete=false;{std::lock_guard lock(shared.mutex);segments=shared.segments;buffered=shared.maximumBuffered;complete=shared.done;}
            progress(seconds,duration,std::min(total,visible),total,segments,buffered,complete);nextProgress=now+std::chrono::milliseconds(40);
        }
        if(finished||(!result.started&&done&&!next))break;
        if(result.started&&now-speaking>=std::chrono::duration<double>(maximum)){result.limited=true;break;}
        if(result.started&&blocks.empty()&&!done)wasEmpty=true;
        if(!next)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    abort=true;shared.cv.notify_all();
    if(output){waveOutReset(output);for(auto& block:blocks)if(block->header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(output,&block->header,sizeof(WAVEHDR));waveOutClose(output);}
    producer.get();
    result.elapsed=result.started?std::chrono::duration<double>(std::chrono::steady_clock::now()-speaking).count():0;
    {std::lock_guard lock(shared.mutex);result.failed=result.failed||shared.failed;result.limited=result.limited||shared.limited;result.segments=shared.segments;result.maximumBuffered=shared.maximumBuffered;}
    result.seconds=lastPosition;return result;
}
