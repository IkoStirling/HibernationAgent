    std::mutex pauseControlMutex_;
    void forgetPauseOwnership(const std::string& id){
        std::lock_guard guard(pauseControlMutex_);auto control=get("ha.control","current");if(!control.value.contains("pause_owned"))return;auto value=control.value;auto& owned=value["pause_owned"].get_ref<Json::array_t&>();
        std::erase_if(owned,[&](const auto& item){return item.at("id").template get<std::string>()==id;});
        if(value!=control.value)commit({mutation("ha.control","current",control,value)},"explicit-job-pause");
    }
    Json pauseOwnedJobs(const Doc& control)const{
        if(control.value.contains("pause_owned"))return control.value.at("pause_owned");
        Json owned=Json::array();
        // Older archives did not distinguish manual task pause from global
        // pause. Reserved slots are recoverable; preserve user routines paused.
        if(control.value.value("paused",false))for(const auto& job:jobs())
            if(job.state==JobState::Paused&&!job.spec.id.starts_with("routine.")&&!yieldedBackground(job.spec.id))owned.push_back({{"id",job.spec.id},{"token",job.runToken}});
        return owned;
    }
    void applyPauseOwnership(const Json& owned,bool pause,std::int64_t now){
        const auto snapshot=jobs();
        for(const auto& item:owned){
            const auto found=std::find_if(snapshot.begin(),snapshot.end(),[&](const auto& job){return job.spec.id==item.at("id").get<std::string>();});
            if(found==snapshot.end()||found->runToken!=item.at("token")||found->state==JobState::Succeeded||found->state==JobState::Failed)continue;
            // Operator media/voice tests are single-use interactions. A new
            // request may replace the paused slot; resuming never replays it.
            if(!pause&&(found->spec.id=="operator.tool"||found->spec.id=="qq.tool"||yieldedBackground(found->spec.id)))continue;
            if(found->pauseRequested!=pause||pause&&found->state==JobState::Pending)
                if(queue->pauseJob(found->spec.id,pause,now)!=QueueStatus::Ok)throw std::runtime_error("owned_job_pause");
        }
    }
    // Manual pause and offline are independent reasons for suspending work.
    // Ownership belongs to their union, so leaving offline never clears an
    // operator's pre-existing pause or resumes individually paused jobs.
    Json controlState(const Json& changes,std::int64_t now){
        std::lock_guard guard(pauseControlMutex_);
        auto before=get("ha.control","current");auto value=before.value;auto owned=pauseOwnedJobs(before);
        value["manual_offline"]=balance::manual(value);
        if(changes.contains("offline"))value["manual_offline"]=changes.at("offline");
        for(const auto* key:{"paused","balance_offline","balance_blocks"})if(changes.contains(key))value[key]=changes.at(key);
        value["offline"]=balance::offline(value);
        const bool pause=value.value("paused",false)||value.value("offline",false);
        runtimeOffline=value.value("offline",false);
        if(pause){
            for(const auto& job:jobs())if((job.state==JobState::Pending||job.state==JobState::Running)&&!job.pauseRequested&&!yieldedBackground(job.spec.id))
                if(std::none_of(owned.begin(),owned.end(),[&](const auto& item){return item.at("id").template get<std::string>()==job.spec.id;}))owned.push_back({{"id",job.spec.id},{"token",job.runToken}});
            value["pause_owned"]=owned;
            commit({mutation("ha.control","current",before,value)},"control-suspend");runtimePaused=true;
            if(queue->setPaused(true,now)!=QueueStatus::Ok)throw std::runtime_error("queue_pause");
            applyPauseOwnership(owned,true,now);
        }else{
            applyPauseOwnership(owned,false,now);
            value["pause_owned"]=Json::array();
            commit({mutation("ha.control","current",before,value)},"control-resume");runtimePaused=false;
            if(queue->setPaused(false,now)!=QueueStatus::Ok)throw std::runtime_error("queue_resume");
            if(before.value.value("paused",false)||balance::offline(before.value)){
                // Resume eligibility, never stale in-flight actions. The normal
                // idle gate, peak policy and manual per-job pauses still apply.
                nextActivityFill=now;nextActivityTick=now;nextReactionFill=now+2000;
            }
        }
        if(pause){reactionGenerator.cancel();cancelActivities(runtimeOffline?"offline":"paused");cancelRoutedReplies(runtimeOffline?"offline":"paused");}
        if(plugins)plugins->event({{"kind","runtime.paused"},{"paused",pause},{"offline",runtimeOffline.load()},{"balance_offline",value.value("balance_offline",false)}});
        nextWorkerPoll=0;return value;
    }
    void restorePauseControl(){
        std::lock_guard guard(pauseControlMutex_);
        const auto now=serviceNowMs();auto control=get("ha.control","current");const auto owned=pauseOwnedJobs(control);
        if(!control.value.contains("pause_owned")){auto value=control.value;value["pause_owned"]=owned;commit({mutation("ha.control","current",control,value)},"pause-ownership-upgrade");}
        // The application record is authoritative across an interrupted control
        // transition. Align the queue before its first worker poll.
        if(queue->setPaused(runtimePaused,now)!=QueueStatus::Ok)throw std::runtime_error("queue_pause_recovery");
        if(runtimePaused)applyPauseOwnership(owned,true,now);
    }
