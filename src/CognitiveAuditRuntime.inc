    mutable std::mutex cognitiveAuditMutex;
    Json cognitiveAuditPending=Json::array();std::int64_t nextCognitiveAuditFlush=0;
    std::uint64_t cognitiveAuditDropped=0;
    std::string cognitiveAuditSessionId;
    std::atomic<bool> cognitiveAuditSessionOpen{false};
    void startCognitiveAuditSession(){
        if(cfg.channel||cognitiveAuditSessionOpen||!mindSettings().enabled)return;
        const auto previous=get("ha.cognitive-audit","session");const auto now=serviceNowMs();
        cognitiveAuditSessionId=id("audit-session");
        const auto closure=previous.revision?cognitive_audit::previousClosure(previous.value):Json(nullptr);
        Json value={{"version",1},{"id",cognitiveAuditSessionId},{"opened_at",now},{"clean_shutdown",false},{"previous_run_clean",closure}};
        commit({mutation("ha.cognitive-audit","session",previous,value,"audit-session")},cognitiveAuditSessionId);cognitiveAuditSessionOpen=true;
        cognitiveAudit("cognition.runtime.started",{{"status",closure==false?"recovered_after_unclean_exit":"started"},{"reason",previous.revision?(closure.is_null()?"previous_session_unknown":"previous_session_recorded"):"first_audited_session"}},cognitiveAuditSessionId);
    }
    void closeCognitiveAuditSession(){
        if(!cognitiveAuditSessionOpen)return;
        cognitiveAudit("cognition.runtime.stopped",{{"status","audit_tail_checkpointed"},{"reason","normal_shutdown"}},cognitiveAuditSessionId);
        flushCognitiveAudit(serviceNowMs(),true);
        const auto before=get("ha.cognitive-audit","session");
        if(before.value.value("id",std::string{})!=cognitiveAuditSessionId)throw std::runtime_error("audit_session_changed");
        auto value=before.value;value["clean_shutdown"]=true;value["closed_at"]=serviceNowMs();
        commit({mutation("ha.cognitive-audit","session",before,value,"audit-session")},cognitiveAuditSessionId);cognitiveAuditSessionOpen=false;
    }
    void cognitiveAudit(std::string kind,Json detail=Json::object(),std::string correlation={}){
        if(cfg.channel||!mindSettings().enabled||!kind.starts_with("cognition."))return;
        Json row={{"version",1},{"id",id("mind-audit")},{"kind",std::move(kind)},{"created_at",serviceNowMs()},{"correlation",std::move(correlation)}};
        for(const auto* field:{"status","reason","source_id","action_id","action_kind","trace_id","request_id","revision","before_revision","late_effect","token","queued_count","candidate_count"})if(detail.contains(field)&&detail.at(field).is_primitive())row[field]=detail.at(field);
        if(detail.dump().size()<=16384)row["private"]=std::move(detail);else row["private_details_omitted"]=true;
        std::lock_guard lock(cognitiveAuditMutex);if(cognitiveAuditPending.size()>=128){++cognitiveAuditDropped;return;}cognitiveAuditPending.push_back(std::move(row));
    }
    void auditActivities(const Json& before,const Json& after,const std::string& reason){
        if(!mindSettings().enabled)return;for(auto row:cognitive_audit::activityTransitions(before,after,serviceNowMs(),reason)){
            const auto kind=row.at("kind").get<std::string>(),correlation=row.value("correlation",std::string{});cognitiveAudit(kind,std::move(row),correlation);
        }
    }
    void flushCognitiveAudit(std::int64_t now,bool force=false){
        if(cfg.channel||(!force&&now<nextCognitiveAuditFlush))return;nextCognitiveAuditFlush=now+500;
        std::lock_guard lock(cognitiveAuditMutex);if(cognitiveAuditPending.empty()&&!cognitiveAuditDropped)return;
        auto index=get("ha.cognitive-audit","index",Json::array());auto counters=get("ha.cognitive-audit","status",{{"version",1},{"events",0},{"dropped",0},{"by_kind",Json::object()},{"last_at",0}});
        auto value=counters.value;std::vector<RecordMutation> changes;
        for(const auto& row:cognitiveAuditPending){changes.push_back({"ha.cognitive-trace",row.at("id"),0,1,row.dump(),row.value("correlation",std::string{})});index.value.push_back(cognitive_audit::metadata(row));value["events"]=value.at("events").get<std::int64_t>()+1;auto& count=value["by_kind"][row.at("kind").get<std::string>()];count=count.is_number_integer()?count.get<std::int64_t>()+1:1;value["last_at"]=row.at("created_at");}
        while(index.value.size()>500)index.value.erase(0);value["dropped"]=value.at("dropped").get<std::uint64_t>()+cognitiveAuditDropped;
        changes.push_back(mutation("ha.cognitive-audit","index",index,index.value,"cognitive-audit"));changes.push_back(mutation("ha.cognitive-audit","status",counters,value,"cognitive-audit"));
        commit(changes,"cognitive-audit");auto committed=std::move(cognitiveAuditPending);cognitiveAuditPending=Json::array();cognitiveAuditDropped=0;
        // Audit text stays encrypted. Existing virtual event channels contain
        // only identifiers/status and are not another source of experiences.
        for(const auto& row:committed)fact(row.at("id"),"cognition",cognitive_audit::metadata(row),row.at("created_at"),row.value("correlation",std::string{}));
    }
    Json cognitiveAuditStatus()const{
        auto value=get("ha.cognitive-audit","status",{{"version",1},{"events",0},{"dropped",0},{"by_kind",Json::object()},{"last_at",0}}).value;value["enabled"]=mindSettings().enabled;value["private_content_included"]=false;
        const auto session=get("ha.cognitive-audit","session");value["previous_run_clean"]=session.value.value("previous_run_clean",Json(nullptr));value["audit_session_open"]=cognitiveAuditSessionOpen.load();return value;
    }
    Json cognitiveAuditPage(const Json& query)const{
        if(!query.is_object()||query.size()>2)return {{"error","trace_arguments"}};
        for(const auto& [key,value]:query.items())if(key!="limit"&&key!="before_id")return {{"error","trace_arguments"}};
        if((query.contains("limit")&&!query.at("limit").is_number_integer())||(query.contains("before_id")&&!query.at("before_id").is_string()))return {{"error","trace_arguments"}};
        const auto limit=query.value("limit",std::int64_t(50));if(limit<1||limit>100)return {{"error","trace_limit"}};
        const auto before=query.value("before_id",std::string{});const auto rows=get("ha.cognitive-audit","index",Json::array()).value;Json result=Json::array();bool admitted=before.empty();
        for(auto it=rows.rbegin();it!=rows.rend();++it){if(!admitted){if(it->at("id")==before)admitted=true;continue;}result.push_back(*it);if(result.size()>=static_cast<std::size_t>(limit))break;}
        return {{"version",1},{"events",result},{"private_content_included",false},{"next_before_id",result.empty()?Json(nullptr):result.back().at("id")},{"status",cognitiveAuditStatus()}};
    }
