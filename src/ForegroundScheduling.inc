    bool foregroundAvailable()const{
        if(workshopBusy()||napcatActive())return false;
        if(!journalHealthy)return false;
        for(const auto& job:jobs()){
            if(intention_life::solitudeJob(job.spec.kind))continue;
            if(cfg.channel&&job.state!=JobState::Running&&job.spec.kind=="role.turn"){
                const auto input=Json::parse(job.spec.payload);const auto request=get("ha.requests",input.value("request_id",std::string{}));
                if(request.value.value("status",std::string{})=="abandoned")continue;
            }
            if(job.state==JobState::Running||job.state==JobState::Pending&&
                (job.spec.id=="chat"||job.spec.id=="event"||job.spec.id=="operator.tool"||job.spec.dueAtMs<=serviceNowMs()))return false;
        }
        return true;
    }
    bool yieldedBackground(const std::string& id)const{
        const auto held=get("ha.control","foreground-yield",Json::array());
        return std::any_of(held.value.begin(),held.value.end(),[&](const auto& item){return item.at("id").template get<std::string>()==id;});
    }
    void forgetBackgroundYield(const std::string& id){
        auto held=get("ha.control","foreground-yield",Json::array());auto remaining=held.value;
        std::erase_if(remaining.get_ref<Json::array_t&>(),[&](const auto& item){return item.at("id").template get<std::string>()==id;});
        if(remaining!=held.value)commit({mutation("ha.control","foreground-yield",held,remaining)},"explicit-background-pause");
    }
    void yieldBackground(std::int64_t now){
        auto held=get("ha.control","foreground-yield",Json::array());auto values=held.value;
        std::vector<std::string> pause;
        for(const auto& job:jobs())if(intention_life::solitudeJob(job.spec.kind)&&!job.pauseRequested&&
            (job.state==JobState::Running||job.state==JobState::Pending&&job.spec.dueAtMs<=now)){
            if(std::none_of(values.begin(),values.end(),[&](const auto& item){return item.at("id").template get<std::string>()==job.spec.id;}))
                values.push_back({{"id",job.spec.id},{"token",job.runToken},{"original_due",job.spec.dueAtMs}});
            pause.push_back(job.spec.id);
        }
        // Persist ownership before changing the queue. Recovery can finish a
        // partial yield without mistaking an independently paused task for ours.
        if(values!=held.value)commit({mutation("ha.control","foreground-yield",held,values)},"foreground-yield");
        for(const auto& id:pause)if(queue->pauseJob(id,true,now)!=QueueStatus::Ok)throw std::runtime_error("background_yield");
        nextWorkerPoll=0;
    }
    void reconcileBackground(std::int64_t now){
        auto held=get("ha.control","foreground-yield",Json::array());if(held.value.empty())return;
        const bool release=!runtimePaused&&!inputActive&&foregroundAvailable()&&worker->running()==0;
        const auto snapshot=jobs();Json remaining=Json::array();
        for(const auto& item:held.value){
            const auto found=std::find_if(snapshot.begin(),snapshot.end(),[&](const auto& job){return job.spec.id==item.at("id").get<std::string>();});
            if(found==snapshot.end()||!intention_life::solitudeJob(found->spec.kind)||found->runToken!=item.at("token")||found->state==JobState::Succeeded||found->state==JobState::Failed)continue;
            // A reschedule may have committed immediately before a crash. Its
            // changed due time is enough to acknowledge that completed step.
            if(found->state==JobState::Pending&&!found->pauseRequested&&found->spec.dueAtMs!=item.at("original_due"))continue;
            if(found->state==JobState::Running||found->state==JobState::Pending){
                if(!found->pauseRequested&&queue->pauseJob(found->spec.id,true,now)!=QueueStatus::Ok)throw std::runtime_error("background_yield_recovery");
                remaining.push_back(item);continue;
            }
            if(release&&found->state==JobState::Paused){
                auto deferred=found->spec;
                deferred.dueAtMs=std::max(item.at("original_due").get<std::int64_t>(),now+std::clamp<std::int64_t>(cfg.idleBeforeSolitudeMs,5000,60000));
                if(!schedule(deferred))throw std::runtime_error("background_resume");
            }else remaining.push_back(item);
        }
        if(remaining!=held.value)commit({mutation("ha.control","foreground-yield",held,remaining)},"foreground-resume");
    }
