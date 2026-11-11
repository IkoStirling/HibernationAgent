    mutable std::mutex modelCacheMutex;
    void recordModelUsage(const std::string& profileId,const std::string& purpose,const Json& usage,bool budgetObserved=false){
        if(cfg.channel){if(cfg.channel->providerUsage)cfg.channel->providerUsage(profileId,purpose,usage);return;}
        if(profileId.empty()||profileId.size()>128||purpose.size()>48||!usage.is_object())return;
        if(!budgetObserved)observeBackgroundUsage(purpose,usage);
        std::lock_guard guard(modelCacheMutex);auto before=get("ha.model-cache","usage",{{"version",1},{"since_ms",serviceNowMs()},{"connections",Json::object()}});auto value=before.value;
        value=addModelCacheUsage(std::move(value),profileId,purpose,usage);
        value["updated_ms"]=serviceNowMs();commit({mutation("ha.model-cache","usage",before,value,"provider-usage")},"provider-usage");
    }
    Json modelCacheStatus()const{std::lock_guard guard(modelCacheMutex);return get("ha.model-cache","usage",{{"version",1},{"since_ms",nullptr},{"connections",Json::object()}}).value;}
    void recordBatchUsage(const Json& usage,const std::string& purpose){
        if(usage.value("model_cache_observed",false))return;
        for(const auto* role:{"actor","review"})if(usage.contains(std::string(role)+"_usage"))recordModelUsage(usage.value(std::string(role)+"_profile_id",std::string{}),purpose+(std::string_view(role)=="review"?"_review":""),usage.at(std::string(role)+"_usage"),usage.value("background_budget_observed",false));
    }
