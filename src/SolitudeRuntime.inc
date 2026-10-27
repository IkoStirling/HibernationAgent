    struct ActivityOpportunity {Doc ledger;Json references=Json::array(),keys=Json::array();};
    ActivityOpportunity newActivityOpportunity(std::int64_t now,bool forced){
        ActivityOpportunity result;if(cfg.channel||!cfg.reflectOnActivities)return result;
        result.ledger=get("ha.solitude","activities",emptyActivityConsideration());
        if(!activityConsiderationDue(result.ledger.value,now,forced))return result;
        Json recent;{std::lock_guard guard(activityMutex);recent=activityQueue().value("recent",Json::array());}
        const auto policy=visibility();std::set<std::string> seen;std::vector<std::pair<std::string,Doc>> sources;
        const auto add=[&](const std::string& key){
            if(key.empty()||!seen.insert(key).second)return;const auto source=get(archiveScope,key);if(!source.revision)return;
            const auto kind=source.value.value("kind",std::string{});if(!policy.source(Source{key,source.revision,kind,archiveScope}))return;
            if(!activityConsiderationKey(source.value).empty())sources.emplace_back(key,source);
        };
        for(auto it=recent.rbegin();it!=recent.rend();++it)if(it->value("status",std::string{})=="completed")add(it->value("source_id",std::string{}));
        // Only model-invoked observations enter this index. Administrator tool
        // tests and a bare plugin snapshot are not the character's new work.
        for(const auto& key:get("ha.observations","recent",Json::array()).value)add(key.get<std::string>());
        std::stable_sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){return a.second.value.value("created_at",std::int64_t(0))>b.second.value.value("created_at",std::int64_t(0));});
        for(const auto& [key,source]:sources){
            if(result.references.size()>=3)break;
            const auto created=source.value.value("created_at",std::int64_t(0));if(created<=0||created>now||now-created>86400000)continue;
            const auto kind=source.value.value("kind",std::string{});if(!policy.source(Source{key,source.revision,kind,archiveScope}))continue;
            const auto identity=activityConsiderationKey(source.value);if(activityAlreadyConsidered(result.ledger.value,identity)||std::find(result.keys.begin(),result.keys.end(),identity)!=result.keys.end())continue;
            result.references.push_back({{"scope",archiveScope},{"id",key},{"revision",source.revision},{"kind",kind}});result.keys.push_back(identity);
        }
        return result;
    }
