    const std::string personalPursuitInstance=id("personal-instance");
    void rememberPersonalPursuitTrace(const ProposalContext& context,const std::string& trace){
        if(context.personalPursuitId.empty())return;auto before=get("ha.autonomy","personal-pursuit",Json::object());auto value=before.value;
        if(!value.value("active",Json(nullptr)).is_object()||value.at("active").value("owner_instance",std::string{})!=personalPursuitInstance)return;
        value["active"]["trace_id"]=trace;commit({mutation("ha.autonomy","personal-pursuit",before,value,trace)},trace);
    }
    void appendPersonalPursuitReceipt(std::vector<RecordMutation>& changes,const Source& source,const ProposalContext& context,const std::string& trace){
        if(context.personalPursuitId.empty())return;const auto before=get("ha.autonomy","personal-pursuit",Json::object());auto value=before.value;
        if(!value.value("active",Json(nullptr)).is_object()||value.at("active").value("trace_id",std::string{})!=trace)return;
        auto& active=value["active"];auto refs=active.value("receipt_sources",Json::array());Json ref={{"id",source.id},{"revision",source.revision},{"kind",source.kind},{"scope",source.scope}};
        if(std::find(refs.begin(),refs.end(),ref)!=refs.end())return;refs.push_back(ref);while(refs.size()>32)refs.erase(refs.begin());active["receipt_sources"]=refs;
        changes.push_back(mutation("ha.autonomy","personal-pursuit",before,value,source.id));
    }
    bool personalPursuitEnabled()const{return !cfg.channel&&autonomousMode()&&cfg.plugins.value("widget",Json::object()).value("autonomyPursuitEnabled",true);}
    Json personalPursuitDomains()const{
        Json domains=Json::array();if(!plugins)return domains;if(plugins->enabled("canteen"))domains.push_back("kitchen");if(plugins->enabled("reading")&&cfg.plugins.value("reading",Json::object()).value("autonomousReading",false))domains.push_back("reading");if(plugins->enabled("atelier")||plugins->enabled("pixel-canvas"))domains.push_back("creative");if(plugins->enabled("widget"))domains.push_back("desktop");return domains;
    }
    Json personalPursuitState()const{return get("ha.autonomy","personal-pursuit",Json::object()).value;}
    std::string personalPursuitWaitReason(const Json& value,std::int64_t now)const{
        if(!personalPursuitEnabled())return "disabled";
        if(runtimePaused)return runtimeOffline.load()?"offline":"paused";
        if(inputActive)return "user_input";
        if(circadianAsleep())return "sleeping";
        if(!value.value("active",Json(nullptr)).is_null())return "in_progress";
        if(backgroundBudgetStatus().value("blocked",false))return "daily_budget";
        if(!discretionaryBudgetAvailable())return "return_budget_reserve";
        if(now<value.value("next_ms",std::int64_t(0)))return "cooldown";
        for(const auto& job:jobs())if(job.spec.id=="autonomy.personal-pursuit"&&(job.state==JobState::Pending||job.state==JobState::Running||job.state==JobState::Paused))return "finishing_previous";
        return {};
    }
    bool personalPursuitReady(std::int64_t now)const{return personalPursuitWaitReason(personalPursuitState(),now).empty();}
    Json personalPursuitAvailability()const{
        const auto value=personalPursuitState();const auto reason=personalPursuitWaitReason(value,serviceNowMs());return {{"enabled",personalPursuitEnabled()},{"ready",reason.empty()},{"wait_reason",reason},{"next_ms",value.value("next_ms",std::int64_t(0))},{"domains",personalPursuitDomains()},{"active",intention_life::briefPursuit(value.value("active",Json(nullptr)))}};
    }
    Json personalPursuitOperatorState()const{
        const auto value=personalPursuitAvailability();return {{"enabled",value.at("enabled")},{"ready",value.at("ready")},{"wait_reason",value.at("wait_reason")},{"next_ms",value.at("next_ms")},{"active",value.at("active").is_object()}};
    }
    Json schedulePersonalPursuit(const Json& command){
        const auto now=serviceNowMs();if(!personalPursuitReady(now))return toolFailure("personal_pursuit_not_ready");
        const auto binding=command.at("intention");if(!currentIntention(binding))return toolFailure("activity_intention_changed_or_unavailable");
        const auto area=command.at("domain").get<std::string>();if(!intention_life::domain(area))return toolFailure("personal_pursuit_domain");
        const auto refs=command.value("narrative_sources",Json::array());const auto policy=visibility();for(const auto& ref:refs)if(!activityNarrativeSourceAllowed(ref,policy))return toolFailure("activity_source_unavailable");
        const auto domains=personalPursuitDomains();if(std::find(domains.begin(),domains.end(),area)==domains.end())return toolFailure("personal_pursuit_domain_unavailable");
        const auto row=get("ha.personal",binding.at("id"));Json sources=refs;
        for(const auto* field:{"evidence_sources","decision_sources"})for(const auto& ref:row.value.value(field,Json::array()))if(std::find(sources.begin(),sources.end(),ref)==sources.end())sources.push_back(ref);
        if(sources.empty())return toolFailure("personal_pursuit_source_required");
        const auto key=command.at("id").get<std::string>();
        Json active={{"id",key},{"intention",binding},{"domain",area},{"owner_instance",personalPursuitInstance},{"admitted_ms",now},{"expires_ms",now+static_cast<std::int64_t>(1000*cfg.plugins.value("widget",Json::object()).value("autonomyPursuitMaxSeconds",90.0))},{"narrative_sources",sources}};
        auto before=get("ha.autonomy","personal-pursuit",Json::object());auto value=before.value;value["active"]=active;value["next_ms"]=now+static_cast<std::int64_t>(1000*cfg.plugins.value("widget",Json::object()).value("autonomyPursuitCooldownSeconds",120.0));
        commit({mutation("ha.autonomy","personal-pursuit",before,value,key)},key);
        const auto topic=std::string("你之前留给自己的一个打算：")+row.value.at("topic").get<std::string>()+"。当时想的是："+row.value.at("note").get<std::string>()+
            "。此刻有一小段自己的时间，看看手边的情况和真正发生的反馈，挑愿意的一步。可以顺着它多做一点，碰到等待先停一会儿，也可以让注意力去别处。回复与安静都合适。self.reconsider保留搁置或重续的变化，self.close留给已经做完或放下的打算。";
        if(!schedule(spec("autonomy.personal-pursuit","role.personal-pursuit",{{"force",true},{"personal_pursuit",intention_life::briefPursuit(active)},{"topic",topic},{"references",intention_life::contentBasis(row.value)}},now))){
            const auto saved=get("ha.autonomy","personal-pursuit",Json::object());auto failed=saved.value;failed["active"]=nullptr;commit({mutation("ha.autonomy","personal-pursuit",saved,failed,key)},key);return toolFailure("personal_pursuit_scheduler_busy");
        }
        return toolSuccess({{"accepted",true},{"completed",false},{"job_id","autonomy.personal-pursuit"},{"intention",binding}});
    }
    bool preparePersonalPursuit(const Json& payload,ProposalContext& context)const{
        if(!payload.contains("personal_pursuit"))return true;
        const auto& requested=payload.at("personal_pursuit");const auto active=personalPursuitState().value("active",Json(nullptr));
        if(!personalPursuitReadyForRun(requested,active))return false;
        const auto binding=requested.at("intention");context.personalPursuitId=binding.at("id");context.personalPursuitRevision=binding.at("revision");context.personalPursuitDomain=requested.at("domain");context.personalPursuitExpiresMs=active.at("expires_ms");return true;
    }
    bool personalPursuitReadyForRun(const Json& requested,const Json& active)const{
        if(!personalPursuitEnabled()||runtimePaused||inputActive||!active.is_object()||requested.at("id")!=active.at("id")||serviceNowMs()>active.at("expires_ms").get<std::int64_t>())return false;
        if(active.value("owner_instance",std::string{})!=personalPursuitInstance)return false;
        const auto policy=visibility();for(const auto& ref:active.value("narrative_sources",Json::array()))if(!activityNarrativeSourceAllowed(ref,policy))return false;
        const auto domains=personalPursuitDomains();return std::find(domains.begin(),domains.end(),requested.at("domain"))!=domains.end()&&currentIntention(requested.at("intention"),&policy);
    }
    void personalPursuitTick(std::int64_t now){
        auto before=get("ha.autonomy","personal-pursuit",Json::object());const auto active=before.value.value("active",Json(nullptr));if(!active.is_object())return;
        const auto all=jobs();const auto found=std::find_if(all.begin(),all.end(),[](const auto& j){return j.spec.id=="autonomy.personal-pursuit";});
        const bool restarted=active.value("owner_instance",std::string{})!=personalPursuitInstance,yielded=found!=all.end()&&found->state==JobState::Paused;
        // The executable holds dataRoot's exclusive InstanceLock. A prior
        // process cannot still execute this slot; use the engine's fenced
        // finish to release its stale mutex without waiting for lease expiry.
        if(restarted&&found!=all.end()&&found->state==JobState::Running){
            const auto payload=Json::parse(found->spec.payload,nullptr,false);
            if(payload.is_object()&&payload.value("personal_pursuit",Json::object()).value("id",std::string{})==active.at("id").get<std::string>()){
                const auto settled=queue->finish(JobClaim{*found},JobOutcome{true,false,Json{{"status","no_update"},{"reason","personal_process_restarted"}}.dump(),{}},now);
                if(settled!=QueueStatus::Ok&&settled!=QueueStatus::Conflict)throw std::runtime_error("personal_recovery_lease");
            }
        }
        if(!restarted&&!yielded&&found!=all.end()&&(found->state==JobState::Pending||found->state==JobState::Running))return;
        Json actual=active.value("receipt_sources",Json::array()),tools=Json::array();std::string status=restarted||yielded?"interrupted":"not_run";bool started=active.contains("trace_id");
        if(found!=all.end()){
            const auto payload=Json::parse(found->spec.payload,nullptr,false);
            if(payload.is_object()&&payload.value("personal_pursuit",Json::object()).value("id",std::string{})==active.at("id").get<std::string>()){
                const auto result=Json::parse(found->result,nullptr,false);if(result.is_object()&&result.contains("trace_id")){
                    const auto trace=get("ha.trace",result.at("trace_id"));started=started||trace.revision>0||!actual.empty();
                    for(const auto& tool:trace.value.value("tool_results",Json::array())){
                        tools.push_back({{"tool",tool.at("tool")},{"ok",tool.value("ok",false)},{"error_code",tool.value("error_code",std::string{})}});
                        const auto id=tool.value("source_id",std::string{});if(id.empty())continue;const auto row=get(archiveScope,id);if(!row.revision)continue;
                        Json ref={{"id",id},{"revision",row.revision},{"kind",row.value.at("kind")},{"scope",archiveScope}};if(std::find(actual.begin(),actual.end(),ref)==actual.end())actual.push_back(std::move(ref));
                    }
                    if(!restarted&&!yielded)status=result.value("status",std::string{})=="completed"?(actual.empty()?"considered":"acted"):"interrupted";
                }
            }
        }
        const auto attemptId="intent-step-"+active.at("id").get<std::string>();
        if(!get(archiveScope,attemptId).revision){
            Json attempt={{"intention",active.at("intention")},{"activity_id",active.at("id")},{"action","pursue_intention"},{"domain",active.at("domain")},{"status",status},{"interruption_reason",restarted?"application_restarted":yielded?"foreground_yield":""},{"started",started},{"ended_ms",now},{"result_sources",actual},{"tools",tools}};
            const Json record={{"kind","personal_attempt"},{"observation",{{"kind","intention.activity"},{"attempt",attempt}}},{"text","一段自己的时间；只记录这段实际工具结果，不自动宣布整个打算完成。"},{"sources",actual},{"narrative_sources",active.at("narrative_sources")},{"created_at",now}};
            auto index=get("ha.personal-attempts",active.at("intention").at("id"),Json::array());index.value.push_back(attemptId);while(index.value.size()>6)index.value.erase(0);
            commit({{archiveScope,attemptId,0,1,record.dump(),active.at("id")},mutation("ha.personal-attempts",active.at("intention").at("id"),index,index.value,active.at("id"))},active.at("id"));
        }
        auto value=before.value;value["active"]=nullptr;value["last"]={{"intention",active.at("intention")},{"domain",active.at("domain")},{"status",status},{"ended_ms",now},{"source_id",attemptId}};
        commit({mutation("ha.autonomy","personal-pursuit",before,value,"personal-pursuit-finished")},"personal-pursuit-finished");
        auto plan=activityQueue();bool changed=false;for(auto& item:plan["recent"])if(item.value("id",std::string{})==active.at("id").get<std::string>()){
            item["effect_outcome"]=status;item["effect_ended_ms"]=now;if(!actual.empty())item["source_id"]=actual.back().at("id");changed=true;
        }if(changed)saveActivities(plan);
        planningStimulus(now,false);
    }
    Json personalPursuitBody(const Json& snapshots)const{
        const auto active=personalPursuitState().value("active",Json(nullptr));if(!active.is_object()||!currentIntention(active.at("intention")))return personalPendingEffectBody(snapshots);
        const auto row=get("ha.personal",active.at("intention").at("id"));std::string phase="waiting";
        for(const auto& job:jobs())if(job.spec.id=="autonomy.personal-pursuit")phase=job.state==JobState::Running?"considering_or_acting":job.state==JobState::Paused?"interrupted_by_conversation":"waiting";
        return {{"intention",active.at("intention")},{"topic",row.value.at("topic")},{"domain",active.at("domain")},{"phase",phase},{"goal_completed",false}};
    }
