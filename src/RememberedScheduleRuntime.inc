    mutable std::recursive_mutex rememberedMutex;
    std::int64_t nextRememberedTick=0;
    double rememberedDraw()const{std::uint64_t bits=0;if(!ayt::crypto::tryGenerateRandomBytes(reinterpret_cast<std::uint8_t*>(&bits),sizeof(bits)))throw std::runtime_error("schedule_random_unavailable");return double(bits>>11)/9007199254740992.0;}
    bool rememberedSourcesAllowed(const Json& refs)const{
        if(!refs.is_array()||refs.empty()||refs.size()>9)return false;const auto policy=visibility();
        for(const auto& ref:refs){const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};const auto row=get(source.scope,source.id);if(!row.revision||row.revision!=source.revision||!policy.source(source)||(row.value.contains("sources")&&!policy.allows(row.value)))return false;}return true;
    }
    Json rememberedReferences(const Json& args,const ProposalContext& context)const{
        if(args.contains("source_ids"))return checkedSources(args.at("source_ids"),context);
        for(const auto& key:{context.channelInputId,context.eventId})if(!key.empty()&&context.sources.contains(key)){const auto& s=context.sources.at(key);return Json::array({{{"id",s.id},{"revision",s.revision},{"scope",s.scope},{"kind",s.kind}}});}
        if(!context.scheduleId.empty()){const auto row=get("ha.schedules",context.scheduleId);if(row.revision&&!row.value.at("pending").is_null()){const auto key=row.value.at("pending").value("wake_source_id",std::string{});if(context.sources.contains(key)){const auto& s=context.sources.at(key);return Json::array({{{"id",s.id},{"revision",s.revision},{"scope",s.scope},{"kind",s.kind}}});}}}
        return nullptr;
    }
    bool rememberedActive(const std::string& key,const std::string& opportunity)const{
        if(key.empty())return true;std::lock_guard lock(rememberedMutex);const auto row=get("ha.schedules",key);return row.revision&&row.value.at("status")=="active"&&reminder::phase(row.value,serviceNowMs())!="expired"&&!row.value.at("pending").is_null()&&row.value.at("pending").at("key")==opportunity&&serviceNowMs()<=row.value.at("pending").at("end_ms").get<std::int64_t>();
    }
    void rememberedSave(const std::string& key,const Doc& before,Json value,const std::string& reason){value["updated_ms"]=serviceNowMs();commit({mutation("ha.schedules",key,before,std::move(value),reason)},reason);}
    void rememberedPauseSlot(const std::string& key){queue->pauseJob("routine."+key,true,serviceNowMs());forgetBackgroundYield("routine."+key);forgetPauseOwnership("routine."+key);}
    Json rememberedList(const Json& args)const{
        std::lock_guard lock(rememberedMutex);Json values=Json::array();const auto key=args.value("id",std::string{});const auto query=args.value("query",std::string{});const auto index=get("ha.schedules","index",Json::array()).value;
        for(auto it=index.rbegin();it!=index.rend();++it){if(!key.empty()&&*it!=key)continue;const auto row=get("ha.schedules",*it);if(!row.revision||!rememberedSourcesAllowed(row.value.at("references")))continue;if(key.empty()&&!args.value("include_inactive",false)&&row.value.at("status")!="active"&&row.value.at("status")!="paused")continue;if(!query.empty()&&row.value.at("topic").get<std::string>().find(query)==std::string::npos)continue;
            auto view=reminder::view(row.value,row.revision,serviceNowMs());view["when"]=row.value.at("when_spec");view["state"]=row.value.at("status");view["due_ms"]=row.value.at("next_ms");view["interval_ms"]=row.value.at("when").value("interval_ms",std::int64_t(0));values.push_back(std::move(view));if(values.size()>=static_cast<std::size_t>(args.value("limit",12)))break;}
        if(values.empty()&&!key.empty())return toolFailure("schedule_missing_or_source_unavailable");
        if(key.empty())for(const auto& job:jobs())if(job.spec.id.starts_with("routine.")&&!get("ha.schedules",job.spec.id.substr(8)).revision){const auto payload=Json::parse(job.spec.payload);if(rememberedSourcesAllowed(payload.value("references",Json::array())))values.push_back({{"id",job.spec.id.substr(8)},{"state",stateName(job.state)},{"due_ms",job.spec.dueAtMs},{"interval_ms",job.spec.intervalMs},{"topic",payload.value("topic",std::string{})},{"legacy",true}});if(values.size()>=24)break;}
        while(values.size()>static_cast<std::size_t>(args.value("limit",12)))values.erase(values.end()-1);
        return toolSuccess({{"tasks",values},{"wake_requires_speech",false},{"storage","encrypted_sqlite"}});
    }
    Json rememberedTool(const std::string& name,const Json& args,ProposalContext& context,const std::string& traceId){
        if(!sharedOwner()||(!context.channelIdentity.empty()&&!ownerChannel(context.channelIdentity)))return toolFailure("schedule_private_owner_required");
        if(name=="schedule.list")return rememberedList(args);
        const auto key=args.at("id").get<std::string>();if(!reminder::safeId(key)||key=="index")return toolFailure("schedule_id");std::lock_guard lock(rememberedMutex);auto before=get("ha.schedules",key);
        if(name=="schedule.cancel"){
            if(!before.revision){const auto status=queue->pauseJob("routine."+key,true,serviceNowMs());return status==QueueStatus::Ok?toolSuccess({{"paused",true},{"legacy",true}}):toolFailure("schedule_missing_or_running");}
            auto value=before.value;value["status"]="cancelled";value["pending"]=nullptr;rememberedSave(key,before,value,traceId);rememberedPauseSlot(key);return toolSuccess({{"id",key},{"cancelled",true},{"revision",before.revision+1},{"history_retained",true}});
        }
        if(before.revision&&!rememberedSourcesAllowed(before.value.at("references")))return toolFailure("schedule_source_unavailable");
        if(!context.taskId.empty()||!context.skillId.empty())return toolFailure("schedule_dialogue_or_own_activity_required");
        if(name=="schedule.create"&&before.revision)return toolFailure("schedule_exists_use_update");
        if(name=="schedule.update"&&(!before.revision||before.revision!=args.at("expected_revision")))return toolFailure(before.revision?"schedule_revision_conflict":"schedule_missing");
        if(name=="schedule.update"&&args.contains("state")&&args.at("state")=="paused"&&args.size()==3){auto value=before.value;value["status"]="paused";rememberedSave(key,before,value,traceId);rememberedPauseSlot(key);return toolSuccess({{"id",key},{"paused",true},{"revision",before.revision+1}});}
        auto refs=rememberedReferences(args,context);if(refs.is_null()||!rememberedSourcesAllowed(refs))return toolFailure("schedule_source_required");
        try{
            const auto now=serviceNowMs();const auto topic=args.value("topic",before.revision?before.value.at("topic").get<std::string>():std::string{});if(topic.empty()||topic.find_first_not_of(" \r\n\t")==std::string::npos)return toolFailure("schedule_topic_required");
            const auto memory=args.value("memory",before.revision?before.value.at("memory").get<std::string>():std::string("medium"));reminder::lifetime(memory);Json intent=args.value("when",before.revision?before.value.at("when_spec"):Json::object()),when;
            if(intent.empty()&&args.contains("after_ms")){const auto after=args.at("after_ms").get<std::int64_t>(),period=args.value("interval_ms",std::int64_t(0));if(period&&period<5*reminder::minute)return toolFailure("schedule_interval_too_short");when={{"type",period?"interval":"at"},{"first_ms",now+after}};intent={{"type",period?"interval":"at"},{"after_minutes",std::max<std::int64_t>(1,after/reminder::minute)}};if(period){when["interval_ms"]=period;intent["every_minutes"]=period/reminder::minute;}}
            else {if(intent.empty())return toolFailure("schedule_when_required");if(!ayt::serializer::validateJsonDocument(intent.dump(),reminder::whenSchema().dump()))return toolFailure("schedule_when_invalid");when=reminder::normalizeWhen(intent,now);}
            if(before.revision&&before.value.at("topic")==topic&&before.value.at("memory")==memory&&before.value.at("when_spec")==intent&&before.value.at("references")==refs&&before.value.at("status")=="active")return toolSuccess({{"task",reminder::view(before.value,before.revision,now)},{"deduplicated",true}});
            const auto index=get("ha.schedules","index",Json::array());std::size_t active=0;for(const auto& id:index.value){const auto row=get("ha.schedules",id);if(row.revision&&(row.value.at("status")=="active"||row.value.at("status")=="paused"))++active;}
            if((!before.revision&&index.value.size()>=256)||(active>=32&&(!before.revision||(before.value.at("status")!="active"&&before.value.at("status")!="paused"))))return toolFailure("schedule_capacity");
            if(!before.revision)for(const auto& job:jobs())if(job.spec.id=="routine."+key)return toolFailure("schedule_legacy_id_reserved");
            auto value=reminder::fresh(key,topic,memory,when,refs,now);value["status"]=args.value("state",std::string("active"));value["when_spec"]=intent;value["created_from"]=context.eventId;if(before.revision){value["created_ms"]=before.value.at("created_ms");value["wake_count"]=before.value.at("wake_count");}
            std::vector<RecordMutation> changes={mutation("ha.schedules",key,before,value,traceId)};if(!before.revision){auto ids=index.value;ids.push_back(key);changes.push_back(mutation("ha.schedules","index",index,ids,traceId));}commit(std::move(changes),"remembered-schedule-save");if(before.revision)rememberedPauseSlot(key);rememberedArm(key,now);
            const auto saved=get("ha.schedules",key);auto view=reminder::view(saved.value,saved.revision,now);view["when"]=intent;return toolSuccess({{"id",key},{"due_ms",saved.value.at("next_ms")},{"interval_ms",when.value("interval_ms",std::int64_t(0))},{"task",view},{"parameters_inferred",true},{"wake_requires_speech",false}});
        }catch(const std::exception& error){const std::string code=error.what();return toolFailure(code.starts_with("schedule_")?code.c_str():"schedule_arguments");}
    }
    void rememberedArm(const std::string& key,std::int64_t now){
        auto before=get("ha.schedules",key);if(!before.revision||before.value.at("status")!="active")return;auto value=before.value;
        if(reminder::phase(value,now)=="expired"){value["status"]="expired";value["pending"]=nullptr;rememberedSave(key,before,value,"schedule-expired");rememberedPauseSlot(key);return;}
        if(value.at("pending").is_null()){
            if(value.at("when").at("type")=="event")return;auto opportunity=reminder::due(value);if(value.at("when").at("type")=="window")opportunity=reminder::window(value.at("when"),std::max(now,value.at("next_ms").get<std::int64_t>()));
            const auto at=value.at("when").at("type")=="window"?std::max(now,opportunity.start)+static_cast<std::int64_t>(rememberedDraw()*double(opportunity.end-std::max(now,opportunity.start))):opportunity.start;
            value["pending"]={{"key",opportunity.key},{"start_ms",opportunity.start},{"end_ms",opportunity.end},{"at_ms",at},{"state","armed"},{"trigger",nullptr}};rememberedSave(key,before,value,"schedule-armed");before=get("ha.schedules",key);value=before.value;
        }
        const auto& pending=value.at("pending");const auto slot="routine."+key;for(const auto& job:jobs())if(job.spec.id==slot&&(job.state==JobState::Pending||job.state==JobState::Running||job.state==JobState::Paused)&&Json::parse(job.spec.payload).value("schedule_occurrence",std::string{})==pending.at("key").get<std::string>())return;
        auto refs=value.at("references");const auto trigger=pending.value("trigger",Json(nullptr));if(trigger.is_object()&&trigger.contains("reference")&&refs.size()<9)refs.push_back(trigger.at("reference"));
        Json payload={{"force",true},{"topic",value.at("topic")},{"references",refs},{"remembered_schedule",key},{"schedule_occurrence",pending.at("key")}};auto job=spec(slot,"role.solitude",payload,pending.at("at_ms"));job.intervalMs=0;schedule(job);
    }
    void rememberedSignal(const std::string& kind,const std::string& eventKey,const std::string& text,const Json& data=Json::object(),const Json& reference=Json(nullptr)){
        if(cfg.channel)return;std::lock_guard lock(rememberedMutex);const auto now=serviceNowMs();
        for(const auto& key:get("ha.schedules","index",Json::array()).value){auto before=get("ha.schedules",key);if(!before.revision||before.value.at("status")!="active"||!before.value.at("pending").is_null()||now<before.value.at("eligible_ms").get<std::int64_t>()||reminder::phase(before.value,now)=="expired"||!reminder::eventMatches(before.value.at("when"),kind,text+" "+eventKey,data))continue;
            auto value=before.value;auto& seen=value["recent_events"];if(std::find(seen.begin(),seen.end(),eventKey)!=seen.end())continue;seen.push_back(eventKey);while(seen.size()>32)seen.erase(seen.begin());Json trigger={{"event",kind},{"event_key",eventKey},{"observed_ms",now}};if(!reference.is_null())trigger["reference"]=reference;
            value["pending"]={{"key","event:"+eventKey},{"start_ms",now},{"end_ms",now+10*reminder::minute},{"at_ms",now},{"state","armed"},{"trigger",trigger}};rememberedSave(key,before,value,"schedule-event-matched");nextRememberedTick=0;
        }
    }
    std::optional<JobOutcome> rememberedPreflight(const JobClaim& claim){
        const auto payload=Json::parse(claim.job.spec.payload);const auto key=payload.value("remembered_schedule",std::string{});if(key.empty())return {};
        std::lock_guard lock(rememberedMutex);auto before=get("ha.schedules",key);const auto silent=[&](const char* reason){return std::optional<JobOutcome>(JobOutcome{true,false,Json{{"status","no_update"},{"reason",reason},{"remembered_schedule",key}}.dump(),{}});};
        if(!before.revision||before.value.at("status")!="active"||before.value.at("pending").is_null()||before.value.at("pending").at("key")!=payload.at("schedule_occurrence"))return silent("schedule_inactive_or_revised");
        auto value=before.value;const auto now=serviceNowMs();auto& pending=value["pending"];std::string rejection;
        if(reminder::phase(value,now)=="expired"){value["status"]="expired";rejection="schedule_expired";}
        else if(now>pending.at("end_ms").get<std::int64_t>())rejection="schedule_window_missed";
        else if(!rememberedSourcesAllowed(value.at("references"))){value["status"]="blocked";rejection="schedule_source_unavailable";}
        if(rejection.empty()&&!pending.contains("draw")){pending["draw"]=rememberedDraw();pending["probability"]=reminder::chance(value,now,life.snapshot());pending["selected"]=pending.at("draw").get<double>()<pending.at("probability").get<double>();if(!pending.at("selected").get<bool>())rejection="schedule_not_recalled";}
        else if(rejection.empty()&&pending.contains("selected")&&!pending.at("selected").get<bool>())rejection="schedule_not_recalled";
        if(!rejection.empty()){pending["rejection"]=rejection;rememberedSave(key,before,value,"schedule-gate");return silent(rejection.c_str());}
        if(!pending.contains("wake_source_id")){const auto source=id("schedule-wake");Json event={{"kind","outer_event"},{"text","自己的一项定时计划获得了一次唤醒机会；尚不表示内容已完成。"},{"schedule_id",key},{"created_at",now},{"sources",value.at("references")}};commit({{archiveScope,source,0,1,event.dump(),"remembered-schedule-wake"}},source);pending["wake_source_id"]=source;}
        pending["state"]="running";rememberedSave(key,before,value,"schedule-started");return {};
    }
    Json rememberedWakeContext(const Json& payload,ProposalContext& context)const{
        const auto key=payload.value("remembered_schedule",std::string{});if(key.empty())return nullptr;std::lock_guard lock(rememberedMutex);const auto row=get("ha.schedules",key);if(!row.revision||row.value.at("pending").is_null())return nullptr;
        context.scheduleId=key;context.scheduleOccurrence=payload.at("schedule_occurrence");auto result=reminder::view(row.value,row.revision,serviceNowMs());result["when"]=row.value.at("when_spec");result["trigger"]=row.value.at("pending").value("trigger",Json(nullptr));result["meaning"]="这是自己的计划唤醒，不是新的用户消息；可以执行、调整、拒绝或保持沉默。";return result;
    }
    void rememberedSettled(const JobInfo& job,const Json& result){
        const auto payload=Json::parse(job.spec.payload);const auto key=payload.value("remembered_schedule",std::string{});if(key.empty())return;
        std::lock_guard lock(rememberedMutex);auto before=get("ha.schedules",key);if(!before.revision||before.value.at("pending").is_null()||before.value.at("pending").at("key")!=payload.at("schedule_occurrence"))return;
        auto value=before.value;const auto pending=value.at("pending");const auto now=serviceNowMs();const bool woke=pending.value("state",std::string{})=="running"&&job.state==JobState::Succeeded&&result.value("status",std::string{})=="completed";value["last_decision"]={{"opportunity",pending.at("key")},{"at_ms",now},{"result",pending.value("rejection",result.value("status",std::string("failed")))},{"woke",woke},{"spoke",result.contains("reply_id")}};
        if(woke)value["wake_count"]=value.at("wake_count").get<int>()+1;value["pending"]=nullptr;value["eligible_ms"]=now+value.at("cooldown_ms").get<std::int64_t>();
        if(value.at("status")=="active"){if(reminder::once(value)&&(woke||value.at("when").at("type")=="at"))value["status"]=woke?"completed":"missed";else reminder::advance(value,now,pending.at("end_ms"));}
        rememberedSave(key,before,value,"schedule-outcome");nextRememberedTick=0;
    }
    void rememberedWeather(const Json& event,const std::string& source){
        const auto before=get("ha.schedule-signals","weather");const auto code=event.value("weather_code",-1);if(before.revision&&before.value.value("weather_code",-1)==code)return;
        commit({mutation("ha.schedule-signals","weather",before,{{"weather_code",code},{"source_id",source}},"weather-observed")},"schedule-weather-change");
        rememberedSignal("weather_changed",source,"",event,{{"id",source},{"scope","ha.plugin-events"},{"revision",1},{"kind",event.at("kind")}});
    }
    void rememberedSchedulesTick(std::int64_t now){
        if(cfg.channel||now<nextRememberedTick)return;nextRememberedTick=now+1000;std::lock_guard lock(rememberedMutex);
        const auto current=get("ha.state","current").value;const auto last=current.value("lastUserInteractionMs",current.value("lastInteractionMs",now));if(!runtimePaused&&!runtimeOffline&&now-last>=10*reminder::minute)rememberedSignal("idle","idle:"+std::to_string(last),"",Json::object());
        for(const auto& key:get("ha.schedules","index",Json::array()).value){const auto row=get("ha.schedules",key);if(!row.revision||row.value.at("status")!="active")continue;if(reminder::phase(row.value,now)=="expired"||(!runtimePaused&&!runtimeOffline))rememberedArm(key,now);}
    }
    Json rememberedStats()const{
        std::lock_guard lock(rememberedMutex);Json out={{"active",0},{"paused",0},{"expired",0},{"completed",0},{"blocked",0},{"fading",0},{"memory_policy","program_inferred"}};for(const auto& key:get("ha.schedules","index",Json::array()).value){const auto row=get("ha.schedules",key);if(!row.revision)continue;const auto state=row.value.at("status").get<std::string>();if(out.contains(state))out[state]=out.at(state).get<int>()+1;if(state=="active"&&reminder::phase(row.value,serviceNowMs())=="fading")out["fading"]=out.at("fading").get<int>()+1;}return out;
    }
