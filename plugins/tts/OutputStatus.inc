    std::thread outputMonitor_;PluginJson outputObservation_=PluginJson::object();std::atomic<bool> audioReady_=false;std::atomic<int> activeVolume_=-1;PluginJson audioBackend_=PluginJson::object();
    static PluginJson volumeObservation(const ayt::audio::OutputVolume& volume){return {{"known",volume.known},{"muted",volume.muted},{"master",volume.master},{"channels",volume.channels}};}
    static PluginJson endpointObservation(const ayt::audio::OutputEndpoint& endpoint){return {{"available",endpoint.available},{"name",endpoint.name},{"volume",volumeObservation(endpoint.volume)}};}
    void observeOutput(){
        while(!stop_){
            try{
                const auto value=ayt::audio::queryProcessOutput();const auto state=value.status==ayt::audio::OutputQueryStatus::Ok?"ok":value.status==ayt::audio::OutputQueryStatus::Partial?"partial":value.status==ayt::audio::OutputQueryStatus::Unsupported?"unsupported":"unavailable";
                PluginJson sessions=PluginJson::array();for(const auto& item:value.processSessions)sessions.push_back({{"endpoint_name",item.endpointName},{"default_for_console",item.defaultForConsole},{"default_for_multimedia",item.defaultForMultimedia},{"state",item.state==ayt::audio::OutputSessionState::Active?"active":item.state==ayt::audio::OutputSessionState::Inactive?"inactive":item.state==ayt::audio::OutputSessionState::Expired?"expired":"unknown"},{"session",volumeObservation(item.session)},{"endpoint",volumeObservation(item.endpoint)}});
                PluginJson observed={{"checked_at_ms",nowMs()},{"query_state",state},{"truncated",value.truncated},{"console",endpointObservation(value.console)},{"multimedia",endpointObservation(value.multimedia)},{"own_sessions",std::move(sessions)}};
                {std::lock_guard lock(mutex_);outputObservation_=std::move(observed);}
            }catch(...){std::lock_guard lock(mutex_);outputObservation_={{"checked_at_ms",nowMs()},{"query_state","unavailable"}};}
            for(int i=0;i<50&&!stop_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    // Caller holds mutex_; querying the OS never happens on a tool/UI thread.
    PluginJson outputStatusLocked()const{
        auto value=outputObservation_;value["engine"]=audioBackend_;value["engine"]["ready"]=audioReady_.load();value["configured_volume"]=cfg_.value("volume",75);
        const bool speaking=active_.load();value["active_volume"]=speaking?PluginJson(activeVolume_.load()):PluginJson(nullptr);
        value["assessment"]=assessAudioDelivery(outputObservation_,audioReady_.load(),speaking?activeVolume_.load():cfg_.value("volume",75),nowMs());
        value["note"]="只读本进程的输出路径；没有录音、改变设备或音量，也不能确认用户是否听到或忽视。";return value;
    }
    bool prepareAudioOutput(){
        if(audioReady_)return true;const bool ready=audioEngine_&&audioEngine_->isInitialized();
        if(ready){const auto info=audioEngine_->backendInfo();std::lock_guard lock(mutex_);audioBackend_={{"name",info.name?info.name:"unknown"},{"real_device",info.realDevice},{"sample_rate",info.sampleRate},{"channels",info.channels}};audioReady_=info.realDevice;}
        return audioReady_.load();
    }
