#pragma once

// One AYAudio stream per utterance. The producer may reserve at most three
// segments, including the segment in synthesis and buffers already playing.
// Immutable PCM segments remain owned until the source cursor passes them;
// page changes never reopen the device or purge queued audio. No cache on disk.
struct QueuedSpeechResult {
    bool started=false,failed=false,limited=false;
    double elapsed=0,firstMs=0,peak=0,seconds=0;
    unsigned segments=0,maximumBuffered=0,underruns=0;
};
template<class Render,class Started,class Progress>
QueuedSpeechResult playQueuedSpeech(ayt::audio::AudioEngine& engine,const std::function<float()>& pan,const std::string& text,double maximum,int volume,
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

    struct Block{Wav audio;std::size_t offset=0;uint64_t end=0;std::vector<float> pcm;};
    std::deque<Block> blocks;
    std::vector<std::pair<double,std::size_t>> cues;
    QueuedSpeechResult result;ayt::audio::AudioStreamId stream=0;ayt::audio::VoiceHandle voice=0;
    double duration=0,lastPosition=0;uint64_t written=0,frames=0;bool eos=false;
    const auto total=textOffsets(text).size()-1;
    auto speaking=preparation,nextProgress=preparation;bool wasEmpty=false;
    while(!cancelled()){
        std::optional<Piece> next;bool done=false,failed=false;
        {std::lock_guard lock(shared.mutex);if(!shared.ready.empty()){next=std::move(shared.ready.front());shared.ready.pop_front();}done=shared.done;failed=shared.failed;}
        if(failed){result.failed=true;break;}
        if(next){
            Block block;block.audio=std::move(next->audio);
            for(const auto& [at,n]:block.audio.cues)cues.emplace_back(duration+at,next->character+n);
            duration+=block.audio.seconds;frames+=block.audio.pcm.size()/2;block.end=frames;
            block.pcm.resize(block.audio.pcm.size()/2);
            for(std::size_t i=0;i<block.pcm.size();++i){int16_t value;std::memcpy(&value,block.audio.pcm.data()+i*2,2);block.pcm[i]=value/32768.0f;result.peak=std::max(result.peak,static_cast<double>(std::abs(block.pcm[i])));}
            block.audio.pcm.clear();blocks.push_back(std::move(block));
            if(!stream){ayt::audio::AudioStreamDesc desc;desc.advanceClockOnUnderrun=false;desc.sampleRate=24000;desc.channels=1;desc.capacityFrames=65536;stream=engine.openStream(desc);if(!stream){result.failed=true;break;}}
        }
        bool fullyPushed=true;
        for(auto& block:blocks){
            if(block.offset<block.pcm.size()){const auto n=engine.streamPush(stream,block.pcm.data()+block.offset,static_cast<uint32_t>(block.pcm.size()-block.offset));block.offset+=n;written+=n;if(block.offset<block.pcm.size()){fullyPushed=false;break;}}
        }
        if(!result.started&&written){
            ayt::audio::SpatializationParams spatial;spatial.stereoPan=pan();
            voice=engine.playStream(stream,ayt::audio::AudioBus::Voice,spatial,volume/100.0f);
            if(!voice){result.failed=true;break;}
            result.firstMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparation).count();speaking=std::chrono::steady_clock::now();nextProgress=speaking;result.started=true;started(duration,result.firstMs,result.peak);
        }
        if(result.started){ayt::audio::SpatializationParams spatial;spatial.stereoPan=pan();engine.setVoiceSpatial(voice,spatial);engine.submitFrame(.005f);}
        bool noReady;{std::lock_guard lock(shared.mutex);noReady=shared.ready.empty();done=shared.done;}
        if(result.started&&done&&noReady&&fullyPushed&&!eos){engine.markStreamEndOfStream(stream);eos=true;}
        auto position=result.started?engine.voicePositionFrames(voice):uint64_t(0);
        ayt::audio::VoiceRuntime state;const bool finished=result.started&&eos&&(!engine.voiceState(voice,state)||!state.playing);
        if(finished)position=frames;
        position=std::max(position,static_cast<uint64_t>(lastPosition*24000));
        while(!blocks.empty()&&position>=blocks.front().end){blocks.pop_front();{std::lock_guard lock(shared.mutex);--shared.buffered;}shared.cv.notify_all();}
        const auto now=std::chrono::steady_clock::now();
        if(result.started&&(now>=nextProgress||finished)){
            const double seconds=std::clamp(position/24000.0,lastPosition,duration);lastPosition=seconds;std::size_t visible=0,before=0;double previous=0;
            for(const auto& [at,n]:cues){if(seconds>=at){visible=n;previous=at;before=n;continue;}if(at>previous)visible=before+static_cast<std::size_t>((n-before)*(seconds-previous)/(at-previous));break;}
            unsigned segments=0,buffered=0;bool complete=false;{std::lock_guard lock(shared.mutex);segments=shared.segments;buffered=shared.maximumBuffered;complete=shared.done;}
            progress(seconds,duration,std::min(total,visible),total,segments,buffered,complete);nextProgress=now+std::chrono::milliseconds(40);
        }
        if(finished||(!result.started&&done&&noReady))break;
        if(result.started&&now-speaking>=std::chrono::duration<double>(maximum)){result.limited=true;break;}
        const bool empty=result.started&&!done&&position>=written;if(empty&&!wasEmpty)++result.underruns;wasEmpty=empty;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    abort=true;shared.cv.notify_all();
    if(voice)engine.stop(voice);if(stream)engine.closeStream(stream);
    producer.get();
    result.elapsed=result.started?std::chrono::duration<double>(std::chrono::steady_clock::now()-speaking).count():0;
    {std::lock_guard lock(shared.mutex);result.failed=result.failed||shared.failed;result.limited=result.limited||shared.limited;result.segments=shared.segments;result.maximumBuffered=shared.maximumBuffered;}
    result.seconds=lastPosition;return result;
}
