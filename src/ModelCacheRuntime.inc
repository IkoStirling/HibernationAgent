    mutable std::mutex modelCacheMutex;
    void recordModelUsage(const std::string& profileId,const std::string& purpose,const Json& usage){
        if(cfg.channel){if(cfg.channel->providerUsage)cfg.channel->providerUsage(profileId,purpose,usage);return;}
        if(profileId.empty()||profileId.size()>128||purpose.size()>48||!usage.is_object())return;
        std::lock_guard guard(modelCacheMutex);auto before=get("ha.model-cache","usage",{{"version",1},{"since_ms",serviceNowMs()},{"connections",Json::object()}});auto value=before.value;
        auto& connections=value["connections"];if(!connections.contains(profileId)&&connections.size()>=32)return;
        auto& connection=connections[profileId];if(!connection.is_object())connection=Json::object();
        auto& groups=connection["purposes"];if(!groups.is_object())groups=Json::object();if(!groups.contains(purpose)&&groups.size()>=24)return;
        groups[purpose]=addCacheUsage(groups.value(purpose,Json::object()),usage);connection["total"]=addCacheUsage(connection.value("total",Json::object()),usage);
        value["updated_ms"]=serviceNowMs();commit({mutation("ha.model-cache","usage",before,value,"provider-usage")},"provider-usage");
    }
    Json modelCacheStatus()const{std::lock_guard guard(modelCacheMutex);return get("ha.model-cache","usage",{{"version",1},{"since_ms",nullptr},{"connections",Json::object()}}).value;}
    void recordBatchUsage(const Json& usage,const std::string& purpose){
        for(const auto* role:{"actor","review"})if(usage.contains(std::string(role)+"_usage"))recordModelUsage(usage.value(std::string(role)+"_profile_id",std::string{}),purpose+(std::string_view(role)=="review"?"_review":""),usage.at(std::string(role)+"_usage"));
    }
