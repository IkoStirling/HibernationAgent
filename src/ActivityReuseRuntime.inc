    std::mt19937 activityReuseRandom{std::random_device{}()};
    bool activityReuseInitialized=false,activityEconomyFallback=false,activityWasDeferred=false;
    std::string activityReuseSignature()const{return Json::array({persona.at("version"),cfg.plugins.value("widget",Json::object()).value("skin",std::string("dinosaur")),cfg.plugins.value("widget",Json::object()).value("animationPack",std::string{})}).dump();}
    Json activityLibrary()const{return get("ha.autonomy","reusable",emptyActivityLibrary()).value;}
    void rememberActivity(const Json& completed){
        auto old=get("ha.autonomy","reusable",emptyActivityLibrary());const auto value=retainActivity(old.value,completed,activityReuseSignature(),serviceNowMs());
        if(value!=old.value)commit({mutation("ha.autonomy","reusable",old,value,"activity-reuse")},"activity-reuse");
    }
    void initializeActivityReuse(){
        if(activityReuseInitialized)return;activityReuseInitialized=true;
        const auto plan=activityQueue();for(const auto& completed:plan.at("recent"))rememberActivity(completed);
    }
    bool reusePeakActivity(std::int64_t now,const Json& snapshots){
        auto old=get("ha.autonomy","reusable",emptyActivityLibrary());
        const auto candidates=reusableCandidates(old.value,snapshots.value("widget",Json::object()),snapshots.value("habitat",Json::object()),activityCommands(snapshots,true),activityReuseSignature(),now,life.snapshot().value("energy",1.0));
        if(candidates.empty())return false;
        const auto& selected=candidates.at(std::uniform_int_distribution<std::size_t>(0,candidates.size()-1)(activityReuseRandom));
        const auto receipt=enqueueActivities(Json::array({selected.at("command")}),"peak-reuse:"+selected.value("source_activity",std::string{}));
        if(!receipt.value("ok",false))return false;
        auto value=old.value;value["last_used"]=selected.at("key");value["last_used_ms"]=now;value["next_ms"]=now+45000;
        for(auto& item:value["items"])if(item.at("key")==selected.at("key")){item["uses"]=item.value("uses",0)+1;item["last_used_ms"]=now;}
        commit({mutation("ha.autonomy","reusable",old,value,"peak-reuse")},"peak-reuse");return true;
    }
    void notifyEconomyFallback(bool enabled){
        if(activityEconomyFallback==enabled)return;activityEconomyFallback=enabled;
        if(plugins)plugins->event({{"kind","runtime.autonomy_fallback"},{"enabled",enabled}});
    }
