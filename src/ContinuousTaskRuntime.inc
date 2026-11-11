    mutable std::recursive_mutex continuousTaskMutex;
    std::int64_t nextContinuousTaskTick=0;
    bool continuousWorkPending()const{
        if(!cfg.continuousTasks.value("enabled",true))return false;std::lock_guard guard(continuousTaskMutex);
        for(const auto& key:get("ha.tasks","index",Json::array()).value){const auto row=get("ha.tasks",key);const auto status=row.value.value("status",std::string{});if((status=="ready"||status=="running")&&row.value.value("next_ms",std::int64_t(0))<=serviceNowMs()+5000)return true;}return false;
    }
    static Json taskSourceJson(const Source& s){return {{"id",s.id},{"revision",s.revision},{"kind",s.kind},{"scope",s.scope}};}
    static Source taskSourceValue(const Json& s){return {s.at("id"),s.at("revision"),s.at("kind"),s.at("scope")};}
    Json taskBindings()const{return {{"drafts",utf8(cfg.draftsRoot.lexically_normal())},{"materials",utf8(cfg.materialsRoot.lexically_normal())}};}
    bool taskChannelAllowed(const Json& identity)const{
        if(identity.empty())return true;
        const auto settings=cfg.plugins.value("napcat",Json::object());const auto access=ChannelAccess::read(settings);
        return identity.value("message_type",std::string{})=="private"&&identity.value("sender_is_owner",false)&&napcatCanControl(identity)&&!access.isolateMemory;
    }
    std::filesystem::path taskChannelRoot(const Json& identity)const{
        return cfg.dataRoot/"channels"/"napcat"/identity.at("self_user_id").get<std::string>()/"private"/identity.at("sender_user_id").get<std::string>();
    }
    bool taskSourceAccessible(const Source& source,const MemoryVisibility* supplied=nullptr)const{
        try{const auto row=get(source.scope,source.id);return row.revision==source.revision&&(supplied?supplied->source(source):visibility().source(source));}catch(...){return false;}
    }
    std::optional<Source> taskCurrentUser(const Json& args,const ProposalContext& context)const{
        if(context.eventKind!="user_message"||!context.skillId.empty()||!context.taskId.empty())return std::nullopt;
        const auto key=args.at("source_id").get<std::string>();const auto found=context.sources.find(key);
        if(found==context.sources.end()||found->second.kind!="user_message")return std::nullopt;
        auto source=found->second;
        if(!context.channelIdentity.empty()){
            if(!taskChannelAllowed(context.channelIdentity)||key!=context.channelInputId)return std::nullopt;
            const auto tag=memoryRootTag(taskChannelRoot(context.channelIdentity));if(!sharedMemoryId(source.id)){source.id=tag+source.id;source.scope=tag+source.scope;}
        }else if(key!=context.eventId)return std::nullopt;
        return taskSourceAccessible(source)?std::optional<Source>(source):std::nullopt;
    }
    Json taskEvidence(const std::string& key)const{return get("ha.task-evidence",key,Json::array()).value;}
    bool taskAccessible(const Json& value)const{
        if(!taskChannelAllowed(value.value("channel_identity",Json::object())))return false;
        const auto policy=visibility();
        for(const auto& ref:value.at("instructions"))if(!taskSourceAccessible(taskSourceValue(ref),&policy))return false;
        for(const auto& ref:get("ha.task-provenance",value.at("id"),Json::array()).value)if(!taskSourceAccessible(taskSourceValue(ref),&policy))return false;
        return true;
    }
    bool taskForCaller(const Json& task,const ProposalContext& context)const{
        if(context.channelIdentity.empty())return true;const auto origin=task.value("channel_identity",Json::object());if(origin.empty())return taskChannelAllowed(context.channelIdentity);
        for(const auto* key:{"self_user_id","sender_user_id","message_type"})if(origin.value(key,std::string{})!=context.channelIdentity.value(key,std::string{}))return false;return true;
    }
    Json taskListing(const ProposalContext* context=nullptr,bool closed=true,std::size_t limit=32)const{
        if(cfg.channel)return cfg.channel->sharedTasks;
        std::lock_guard guard(continuousTaskMutex);Json values=Json::array();
        const auto index=get("ha.tasks","index",Json::array()).value;for(auto it=index.rbegin();it!=index.rend()&&values.size()<limit;++it){const auto row=get("ha.tasks",*it);if(!row.revision||(!closed&&continuous::terminal(row.value.at("status").get<std::string>())))continue;if(context&&(!taskForCaller(row.value,*context)||!taskAccessible(row.value)))continue;values.push_back(continuous::statusView(row.value,row.revision));}
        return {{"enabled",cfg.continuousTasks.value("enabled",true)},{"tasks",values},{"guide","目标和简短检查点持续保存；安排不等于完成。QQ号主私聊建立的获准目标在桌面后台执行，不自动向QQ主动发送；可在原会话查询进度。"}};
    }
    void rememberTaskObservation(const ProposalContext& context,const Source& source){
        if(context.taskId.empty())return;std::lock_guard guard(continuousTaskMutex);auto before=get("ha.task-evidence",context.taskId,Json::array());auto refs=before.value;
        if(std::none_of(refs.begin(),refs.end(),[&](const auto& ref){return ref.at("id")==source.id;})){refs.push_back(taskSourceJson(source));while(refs.size()>24)refs.erase(refs.begin());commit({mutation("ha.task-evidence",context.taskId,before,refs,context.eventId)},"continuous-task-observation");}
        retainTaskProvenance(context);
    }
    void retainTaskProvenance(const ProposalContext& context){
        if(context.taskId.empty())return;auto before=get("ha.task-provenance",context.taskId,Json::array());auto refs=before.value;
        auto add=[&](const auto& sources){for(const auto& [key,source]:sources){if(std::none_of(refs.begin(),refs.end(),[&](const auto& ref){return ref.at("id")==source.id&&ref.at("scope")==source.scope;}))refs.push_back(taskSourceJson(source));}};
        add(context.sources);add(context.narrativeSources);
        if(refs.size()>256){auto task=get("ha.tasks",context.taskId);auto value=task.value;value["status"]="blocked";value["last_issue"]="task_provenance_limit";commit({mutation("ha.tasks",context.taskId,task,value)},"task-provenance-bound");return;}
        if(refs!=before.value)commit({mutation("ha.task-provenance",context.taskId,before,refs)},"task-provenance-retained");
    }
    bool taskExecutionAllowed(const ProposalContext& context)const{
        std::lock_guard guard(continuousTaskMutex);const auto row=get("ha.tasks",context.taskId);
        return cfg.continuousTasks.value("enabled",true)&&row.revision&&row.value.at("status")=="running"&&row.value.at("active_step")==context.taskStep&&row.value.at("bindings")==taskBindings()&&taskAccessible(row.value);
    }
    LocalReply controlContinuousTask(const Json& body,bool compactReply=false){
        if(!body.is_object()||body.size()!=3||!body.contains("id")||!body.at("id").is_string()||!body.contains("expected_revision")||!body.at("expected_revision").is_number_integer()||!body.contains("action")||!body.at("action").is_string())return error(400,"task_control_arguments");
        const auto key=body.at("id").get<std::string>(),action=body.at("action").get<std::string>();if(!continuous::safeId(key)||(action!="pause"&&action!="resume"&&action!="cancel"))return error(400,"task_control_arguments");
        std::lock_guard guard(continuousTaskMutex);auto before=get("ha.tasks",key);if(!before.revision)return error(404,"task_missing");if(body.at("expected_revision")!=before.revision)return error(409,"task_revision_conflict");auto value=before.value;
        if(continuous::terminal(value.at("status").get<std::string>()))return error(409,"task_already_closed");
        if(action=="resume"&&(runtimePaused||!taskAccessible(value)||value.at("bindings")!=taskBindings()||value.at("steps")>=value.at("max_steps")||serviceNowMs()>=value.at("deadline_ms").get<std::int64_t>()))return error(409,"task_resume_unavailable");
        value["status"]=action=="cancel"?"abandoned":action=="pause"?"paused":value.value("active_step",0)>0?"running":"ready";value["failure_count"]=0;value["last_issue"]="";value["next_ms"]=serviceNowMs()+2000;value["updated_ms"]=serviceNowMs();
        commit({mutation("ha.tasks",key,before,value)},"operator-task-control");
        for(const auto& job:jobs())if(job.spec.id=="task.runner"&&Json::parse(job.spec.payload).value("continuous_task",std::string{})==key){
            if(action!="resume"&&(job.state==JobState::Running||job.state==JobState::Pending))queue->pauseJob(job.spec.id,true,serviceNowMs());
            if(action=="resume"&&job.state==JobState::Paused){forgetBackgroundYield(job.spec.id);forgetPauseOwnership(job.spec.id);auto resumed=job.spec;resumed.dueAtMs=serviceNowMs()+2000;if(!schedule(resumed))return error(409,"task_resume_busy");}
        }
        if(compactReply)return reply(200,{{"revision",before.revision+1}});
        return reply(200,continuous::statusView(value,before.revision+1));
    }
    Json taskTool(const std::string& name,const Json& args,ProposalContext& context,const std::string& traceId){
        std::lock_guard guard(continuousTaskMutex);
        if(!cfg.continuousTasks.value("enabled",true))return toolFailure("continuous_tasks_disabled");
        if(!taskChannelAllowed(context.channelIdentity))return toolFailure("task_channel_not_allowed");
        if(name=="task.status"){
            auto list=taskListing(&context,true,args.contains("id")?32:8);if(args.contains("id")){Json selected=Json::array();for(const auto& task:list.at("tasks"))if(task.at("id")==args.at("id"))selected.push_back(task);list["tasks"]=std::move(selected);}return toolSuccess(list);
        }
        if(name=="task.start"){
            const auto source=taskCurrentUser(args,context);if(!source)return toolFailure("task_current_user_required");
            auto index=get("ha.tasks","index",Json::array());std::size_t active=0;
            for(const auto& key:index.value){const auto row=get("ha.tasks",key);if(!row.revision)continue;
                if(!continuous::terminal(row.value.at("status").get<std::string>())){++active;if(row.value.at("instructions").front().at("id")==source->id)return toolSuccess({{"task",continuous::statusView(row.value,row.revision)},{"accepted",true},{"completed",false},{"reused",true}});}}
            if(active>=cfg.continuousTasks.value("maxActive",3))return toolFailure("task_active_limit");
            while(index.value.size()>=32){auto old=std::find_if(index.value.begin(),index.value.end(),[&](const auto& key){return continuous::terminal(get("ha.tasks",key).value.value("status",std::string{}));});if(old==index.value.end())return toolFailure("task_history_index_full");index.value.erase(old);}
            const auto key=id("task");const auto now=serviceNowMs();Json allowed=Json::array();
            for(const auto& tool:scopedTools(context)){const auto n=tool.at("name").get<std::string>();if(n!="task.start"&&n!="task.control"&&n!="task.revise"&&n!="schedule.create"&&n!="schedule.update"&&n!="skills.activate"&&n!="skills.deactivate")allowed.push_back(n);}
            Json value={{"version",1},{"id",key},{"goal",args.at("goal")},{"completion_criterion",args.at("completion_criterion")},{"status","ready"},{"summary",""},{"next_step","开始核对当前可用资料与现场"},{"steps",0},{"active_step",0},{"max_steps",cfg.continuousTasks.value("maxSteps",24)},{"failure_count",0},{"next_ms",now+2000},{"created_ms",now},{"updated_ms",now},{"deadline_ms",now+86400000},{"instructions",Json::array({taskSourceJson(*source)})},{"allowed_tools",allowed},{"bindings",taskBindings()},{"checkpoint",nullptr},{"last_issue",""},{"origin_kind",context.channelIdentity.empty()?"desktop":"qq_owner_private"},{"channel_identity",context.channelIdentity}};
            context.createdTaskId=key;index.value.push_back(key);commit({{"ha.tasks",key,0,1,value.dump(),traceId},mutation("ha.tasks","index",index,index.value,traceId)},"continuous-task-accepted");
            return toolSuccess({{"task",continuous::statusView(value,1)},{"accepted",true},{"completed",false}});
        }
        const auto key=args.at("id").get<std::string>();if(!continuous::safeId(key))return toolFailure("task_identity");auto before=get("ha.tasks",key);
        if(!before.revision||!taskForCaller(before.value,context)||!taskAccessible(before.value))return toolFailure("task_unavailable");
        if(args.at("expected_revision")!=before.revision)return toolFailure("task_revision_conflict");auto value=before.value;
        if(continuous::terminal(value.at("status").get<std::string>()))return toolFailure("task_already_closed");
        if(name=="task.checkpoint"){
            if(context.taskId!=key||context.taskStep!=value.value("active_step",0)||value.at("status")!="running")return toolFailure("task_current_step_required");
            const auto status=args.at("status").get<std::string>();bool concrete=false;Json refs=Json::array();
            for(const auto& id:args.at("source_ids")){const auto known=context.sources.find(id.get<std::string>());if(known==context.sources.end()||!taskSourceAccessible(known->second))return toolFailure("task_evidence_unavailable");const auto record=get(known->second.scope,known->second.id);concrete=concrete||continuous::completionEvidence(record.value);refs.push_back(taskSourceJson(known->second));}
            if(status=="completed"&&!concrete)return toolFailure("task_completion_evidence_required");
            if(status=="continue"&&args.at("next_step").get<std::string>().find_first_not_of(" \r\n\t")==std::string::npos)return toolFailure("task_next_step_required");
            value["checkpoint"]={{"step",context.taskStep},{"status",status},{"summary",args.at("summary")},{"next_step",args.at("next_step")},{"sources",refs},{"wait_seconds",args.value("wait_seconds",2)},{"trace_id",traceId}};
            retainTaskProvenance(context);
        }else if(name=="task.control"||name=="task.revise"){
            const auto source=taskCurrentUser(args,context);if(!source)return toolFailure("task_current_user_required");
            if(name=="task.revise"){
                if(value.at("instructions").size()>=8)return toolFailure("task_instruction_limit");value["instructions"].push_back(taskSourceJson(*source));value["goal"]=args.at("goal");value["completion_criterion"]=args.at("completion_criterion");value["status"]="ready";value["checkpoint"]=nullptr;value["active_step"]=0;value["next_step"]="按用户的新指示核对已有结果，再选择下一步";
            }else{
                const auto action=args.at("action").get<std::string>();if(action=="resume"&&(value.at("steps")>=value.at("max_steps")||serviceNowMs()>=value.at("deadline_ms").get<std::int64_t>()))return toolFailure("task_budget_exhausted");
                value["status"]=action=="cancel"?"abandoned":action=="pause"?"paused":value.value("active_step",0)>0?"running":"ready";
            }
            value["failure_count"]=0;value["last_issue"]="";value["next_ms"]=serviceNowMs()+2000;
        }else return toolFailure("tool_not_available");
        value["updated_ms"]=serviceNowMs();commit({mutation("ha.tasks",key,before,value,traceId)},"continuous-task-checkpoint");return toolSuccess({{"task",continuous::statusView(value,before.revision+1)},{"saved",true},{"completed",false}});
    }
    bool taskCheckpointed(const ProposalContext& context)const{
        if(context.taskId.empty())return true;const auto row=get("ha.tasks",context.taskId);return row.revision&&row.value.at("checkpoint").is_object()&&row.value.at("checkpoint").value("step",0)==context.taskStep;
    }
    void settleContinuousTask(const JobInfo& job,const Json& result){
        const auto payload=Json::parse(job.spec.payload);if(!payload.contains("continuous_task"))return;
        std::lock_guard guard(continuousTaskMutex);const auto key=payload.at("continuous_task").get<std::string>();auto before=get("ha.tasks",key);if(!before.revision)return;auto value=before.value;
        if(value.value("settled_token",std::int64_t(-1))==job.runToken||value.value("active_step",0)!=payload.value("task_step",0))return;
        value["settled_token"]=job.runToken;
        if(value.at("status")=="running"&&result.value("status",std::string{})=="deferred"&&result.value("reason",std::string{})=="circadian_sleep"){
            value["status"]="ready";value["deferred_reason"]="circadian_sleep";value["next_ms"]=serviceNowMs()+1000;value["updated_ms"]=serviceNowMs();commit({mutation("ha.tasks",key,before,value,"task.runner")},"continuous-task-sleep-deferred");return;
        }
        if(value.at("status")=="running"){
            if(!taskAccessible(value)||value.at("bindings")!=taskBindings()){
                value["status"]="blocked";value["last_issue"]="task_source_or_workspace_changed";commit({mutation("ha.tasks",key,before,value,"task.runner")},"continuous-task-settle-denied");return;
            }
            const auto checkpoint=value.at("checkpoint");
            if(checkpoint.is_object()&&checkpoint.value("step",0)==payload.value("task_step",0)){
                const auto desired=checkpoint.at("status").get<std::string>();value["status"]=desired=="continue"?"ready":desired;value["summary"]=checkpoint.at("summary");value["next_step"]=checkpoint.at("next_step");value["next_ms"]=serviceNowMs()+checkpoint.at("wait_seconds").get<std::int64_t>()*1000;value["failure_count"]=0;value["last_issue"]="";value["active_step"]=0;
                const auto observed=taskEvidence(key);const auto fingerprint=observed.empty()?std::string{}:observed.back().at("id").get<std::string>();const bool progress=fingerprint!=value.value("progress_fingerprint",std::string{});value["progress_fingerprint"]=fingerprint;value["no_progress"]=progress?0:value.value("no_progress",0)+1;if(desired=="continue"&&value.at("no_progress").get<int>()>=3){value["status"]="blocked";value["last_issue"]="task_no_progress";}
            }else{
                const auto code=result.value("call_error",Json::object()).value("code",std::string("task_checkpoint_missing"));const int failures=value.value("failure_count",0)+1;value["failure_count"]=failures;value["last_issue"]=code;
                const bool budget=continuous::budgetDeferred(code);
                const bool temporary=budget||code=="rate_limited"||code=="connection_failed"||code=="timeout"||code=="provider_unavailable"||code=="model_call_budget"||code=="background_budget"||code=="background_budget_reserved";
                value["status"]=budget||temporary&&failures<3?"ready":"blocked";if(budget){value["failure_count"]=0;value["deferred_reason"]=code;}value["next_ms"]=serviceNowMs()+60000;if(budget)value["next_ms"]=std::max(value.at("next_ms").get<std::int64_t>(),code=="background_daily_input_budget"||code=="background_daily_call_budget"?backgroundBudgetStatus().value("resets_at_ms",serviceNowMs()+60000):modelBudgetStatus().value("resets_at_ms",serviceNowMs()+60000));
                if(code=="model_call_budget"||code=="background_budget_reserved")value["next_ms"]=std::max(value.at("next_ms").get<std::int64_t>(),modelBudgetStatus().value("resets_at_ms",serviceNowMs()+60000));
                // Retain the stable step identity and its receipts on retry.
            }
        }
        value["updated_ms"]=serviceNowMs();commit({mutation("ha.tasks",key,before,value,"task.runner")},"continuous-task-step-settled");
    }
    bool activeContinuousTask(const Json& payload,Json& view,ProposalContext& context)const{
        if(!payload.contains("continuous_task"))return true;std::lock_guard guard(continuousTaskMutex);auto row=get("ha.tasks",payload.at("continuous_task"));
        if(!cfg.continuousTasks.value("enabled",true)||!row.revision||row.value.at("status")!="running"||row.value.at("active_step")!=payload.at("task_step")||!taskAccessible(row.value)||row.value.at("bindings")!=taskBindings())return false;
        context.taskId=row.value.at("id");context.taskStep=row.value.at("active_step");context.channelIdentity=row.value.value("channel_identity",Json::object());for(const auto& name:row.value.at("allowed_tools"))context.taskTools.insert(name.get<std::string>());view=continuous::statusView(row.value,row.revision);return true;
    }
    bool recoverableTaskCheckpoint(const Json& payload)const{
        if(!payload.contains("continuous_task"))return false;Json view;ProposalContext context;
        return activeContinuousTask(payload,view,context)&&taskCheckpointed(context);
    }
    void continuousTasksTick(std::int64_t now){
        if(cfg.channel||now<nextContinuousTaskTick)return;nextContinuousTaskTick=now+500;
        if(!cfg.continuousTasks.value("enabled",true)||runtimePaused||circadianAsleep()||inputActive||napcatActive()||worker->running()||!journalHealthy||workshopBusy())return;
        for(const auto& job:jobs())if(job.spec.id=="task.runner"&&(job.state==JobState::Pending||job.state==JobState::Running||job.state==JobState::Paused)){
            const auto payload=Json::parse(job.spec.payload);const auto task=get("ha.tasks",payload.value("continuous_task",std::string{}));const auto status=task.value.value("status",std::string{});
            if(status=="ready"||status=="running"||job.state==JobState::Running)return;
        }
        if(!foregroundAvailable())return;
        for(const auto& entry:speechPresentation)if(entry.second.value("state",std::string{})=="waiting"||entry.second.value("state",std::string{})=="speaking")return;
        const auto activities=activityQueue();if(!activities.at("active").is_null()||!activities.at("entries").empty())return;
        std::lock_guard guard(continuousTaskMutex);
        for(const auto& key:get("ha.tasks","index",Json::array()).value){auto before=get("ha.tasks",key);if(!before.revision)continue;auto value=before.value;auto state=value.at("status").get<std::string>();
            if(state=="blocked"&&continuous::budgetDeferred(value.value("last_issue",std::string{}))&&taskAccessible(value)&&value.at("bindings")==taskBindings()&&now<value.at("deadline_ms").get<std::int64_t>()){
                value["status"]="ready";value["failure_count"]=0;value["next_ms"]=now;value["deferred_reason"]="budget_recovered";commit({mutation("ha.tasks",key,before,value,"task.runner")},"continuous-task-budget-recovered");before=get("ha.tasks",key);value=before.value;state="ready";
            }
            if(state!="ready"&&state!="running")continue;if(now<value.at("next_ms").get<std::int64_t>())continue;
            std::string issue;if(cfg.maxToolRounds<2)issue="task_requires_two_tool_rounds";else if(!taskAccessible(value))issue="task_source_or_authority_unavailable";else if(value.at("bindings")!=taskBindings())issue="task_workspace_changed";else if(now>=value.at("deadline_ms").get<std::int64_t>())issue="task_deadline";else if(value.value("active_step",0)==0&&value.at("steps")>=value.at("max_steps"))issue="task_step_budget";
            if(!issue.empty()){value["status"]="blocked";value["last_issue"]=issue;commit({mutation("ha.tasks",key,before,value,"task.runner")},"continuous-task-blocked");continue;}
            if(value.value("active_step",0)==0){value["steps"]=value.at("steps").get<int>()+1;value["active_step"]=value.at("steps");value["checkpoint"]=nullptr;}
            value.erase("deferred_reason");value["status"]="running";value["updated_ms"]=now;commit({mutation("ha.tasks",key,before,value,"task.runner")},"continuous-task-step-started");
            Json refs=value.at("instructions");const auto evidence=taskEvidence(key);for(std::size_t i=evidence.size()>6?evidence.size()-6:0;i<evidence.size();++i)refs.push_back(evidence[i]);
            Json payload={{"force",true},{"continuous_task",key},{"task_step",value.at("active_step")},{"topic",value.at("goal")},{"references",refs}};
            auto job=spec("task.runner","role.solitude",payload,now);job.maxAttempts=2;
            if(!schedule(job))throw std::runtime_error("continuous_task_schedule");return;
        }
    }
