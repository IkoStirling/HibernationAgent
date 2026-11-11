    mutable std::recursive_mutex mindMutex;
    mutable mind::State currentMind;
    mutable bool mindLoaded=false,mindLoadFailed=false;
    std::int64_t mindLastTick=0,mindNextSelection=0,mindForegroundUntil=0;
    mind::Settings mindSettings()const{return mind::Settings::read(cfg.cognition);}
    void loadMind()const{
        if(mindLoaded)return;const auto saved=get("ha.mind","working");
        if(saved.revision&&!currentMind.load(saved.value))mindLoadFailed=true;mindLoaded=true;
    }
    void saveMind(){
        if(cfg.channel||!mindSettings().enabled)return;std::lock_guard lock(mindMutex);loadMind();
        // Invalid future/corrupt state is preserved for diagnosis, never overwritten.
        if(mindLoadFailed)return;const auto saved=get("ha.mind","working");const auto value=currentMind.snapshot();
        if(saved.revision&&saved.value==value)return;commit({mutation("ha.mind","working",saved,value,"continuity-mind")},"continuity-mind");
    }
    Json mindStatus()const{
        std::lock_guard lock(mindMutex);loadMind();auto value=currentMind.status();value["enabled"]=mindSettings().enabled;value["load_error"]=mindLoadFailed;return value;
    }
    bool mindSourcesAllowed(const Json& item,const MemoryVisibility& policy)const{
        for(const auto& ref:item.value("narrative_sources",Json::array()))if(!activityNarrativeSourceAllowed(ref,policy))return false;
        return !item.contains("intention")||currentIntention(item.at("intention"),&policy);
    }
    Json channelMindView(Json view,ProposalContext* context=nullptr)const{
        if(!cfg.channel||!cfg.channel->sharedOwner)return Json::object();
        const auto tag=memoryRootTag(cfg.channel->sharedMemoryRoot);
        std::function<void(Json&)> visit=[&](Json& value){
            if(value.is_object()&&value.contains("id")&&value.contains("scope")&&value.contains("kind")&&value.contains("revision")){
                auto key=value.at("id").get<std::string>();auto scope=value.at("scope").get<std::string>();
                if(scope==archiveScope||scope==memoryScope){key=tag+key;scope=tag+scope;value["id"]=key;value["scope"]=scope;}
                if(context)context->narrativeSources[scope+":"+key]={key,value.at("revision"),value.at("kind"),scope};
            }
            if(value.is_structured())for(auto& child:value)visit(child);
        };visit(view);view.erase("source_bindings");return view;
    }
    Json mindCommandView(const Json& command)const{
        if(!command.is_object())return command;Json value=Json::object();
        for(const auto* field:{"id","kind","seconds","x","y","state","object_id","action","piece_id","game_id","recipe_id","intention","expected_revision","status","source_id"})if(command.contains(field))value[field]=command.at(field);
        if(command.contains("purpose"))value["purpose"]=excerpt(command.at("purpose"),180);
        if(command.contains("content")){value["planned_content_retained_by_program"]=true;value["planned_content_bytes"]=command.at("content").get_ref<const std::string&>().size();}
        return value;
    }
    Json mindContext(ProposalContext* context=nullptr)const{
        if(cfg.channel)return channelMindView(cfg.channel->sharedMind,context);
        if(!mindSettings().enabled)return Json::object();Json view;
        {std::lock_guard lock(mindMutex);loadMind();if(mindLoadFailed)return Json{{"available",false}};view=currentMind.view();}
        const auto policy=visibility();Json pending=Json::array();ProposalContext bindings;
        for(auto item:view.at("suspended")){
            if(item.at("status")!="pending")continue;bool allowed=true;
            for(const auto& command:item.at("commands"))if(!mindSourcesAllowed(command,policy))allowed=false;
            if(item.at("interrupted").is_object()&&!mindSourcesAllowed(item.at("interrupted"),policy))allowed=false;
            if(!allowed)continue;
            {Json plan={{"entries",item.at("commands")},{"active",item.at("interrupted")}};captureActivityNarrativeSources(plan,bindings);}
            item["commands"]=activityForModel(Json{{"entries",item.at("commands")}},&policy).at("entries");
            if(item.at("interrupted").is_object())item["interrupted"]=activityForModel(Json{{"active",item.at("interrupted")}},&policy).at("active");
            for(auto& command:item["commands"])command=mindCommandView(command);item["interrupted"]=mindCommandView(item.at("interrupted"));
            pending.push_back(std::move(item));
        }
        view["suspended"]=std::move(pending);view["interpretations"]=Json::array();view["withdrawn_interpretations"]=Json::array();
        const auto interpretationIds=get("ha.index","interpretations",Json::array()).value;
        for(auto it=interpretationIds.rbegin();it!=interpretationIds.rend();++it){const auto& key=*it;
            const auto row=get("ha.interpretation",key);if(!row.revision)continue;
            if(row.value.at("state")=="withdrawn"){view["withdrawn_interpretations"].push_back({{"id",key},{"revision",row.revision},{"state","withdrawn"}});continue;}
            bool allowed=true;for(const auto& ref:row.value.at("sources"))if(!activityNarrativeSourceAllowed(ref,policy))allowed=false;
            if(!allowed)continue;auto note=row.value;note["revision"]=row.revision;
            for(const auto& ref:note.at("sources")){const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};bindings.narrativeSources[source.scope+":"+source.id]=source;}
            view["interpretations"].push_back(std::move(note));if(view["interpretations"].size()>=8)break;
        }
        while(view["predictions"].size()>6)view["predictions"].erase(0);
        while(view.dump().size()>8192&&!view["interpretations"].empty())view["interpretations"].erase(view["interpretations"].end()-1);
        while(view.dump().size()>8192&&!view["suspended"].empty())view["suspended"].erase(0);
        if(context)for(const auto& [key,ref]:bindings.narrativeSources)context->narrativeSources[key]=ref;
        else view["source_bindings"]=activityNarrativeSources(bindings); // Native QQ handshake; removed before model input.
        return view;
    }
    void mindSuspend(const Json& plan,const std::string& reason){
        if(cfg.channel||!mindSettings().enabled)return;std::lock_guard lock(mindMutex);loadMind();if(mindLoadFailed)return;
        currentMind.suspend(plan,reason,mindSettings());saveMind();cognitiveAudit("cognition.activity.suspended",{{"reason",reason},{"queue",plan},{"working",currentMind.view()}});
    }
    void mindCancelPending(const std::string& reason){
        if(reason!="chosen_cancel"||cfg.channel||!mindSettings().enabled)return;
        std::lock_guard lock(mindMutex);loadMind();for(const auto& r:currentMind.view().at("suspended"))if(r.at("status")=="pending")currentMind.settle(r.at("id"),r.at("revision"),"abandoned");saveMind();
    }
    void mindActivity(const Json& command,const std::string& outcome,bool late=false){
        if(cfg.channel||!mindSettings().enabled)return;std::lock_guard lock(mindMutex);loadMind();if(mindLoadFailed)return;
        if(outcome=="queued")currentMind.queued(command);else currentMind.outcome(command,outcome,late);
    }
    std::string mindObservationKey(const Json& command,const Json& snapshots)const{
        const auto kind=command.value("kind",std::string{});
        if(kind=="creative_read")for(const auto& p:snapshots.value("atelier",Json::object()).value("pieces",Json::array()))if(p.at("id")==command.at("piece_id"))return "piece:"+p.at("id").get<std::string>()+":"+std::to_string(p.value("revision",std::int64_t(0)));
        if(kind=="reference_read")return "reference:"+command.at("language").get<std::string>()+":"+command.at("key").get<std::string>()+":"+command.value("detail",std::string("lead"))+":"+std::to_string(command.value("offset",0));
        if(kind!="look_nearest"&&!(kind=="interact"&&command.value("action",std::string{})=="look"))return {};
        const auto widget=snapshots.value("widget",Json::object());Json target=nullptr;double closest=1e10;
        for(const auto& p:snapshots.value("habitat",Json::object()).value("objects",Json::array())){
            if(kind=="interact"&&p.at("id")!=command.at("object_id"))continue;
            const double d=std::hypot(p.value("x",0.)-widget.value("x",0.),p.value("y",0.)-widget.value("y",0.));if(d<closest){closest=d;target=p;}
        }
        if(target.is_null())return {};return "object:"+target.at("id").get<std::string>()+":"+target.value("phase",std::string{});
    }
    bool mindObservationAllowed(const Json& command,const std::string& source,const Json& snapshots)const{
        const auto settings=mindSettings();if(!settings.enabled||!settings.observations||(!source.starts_with("activity-generation")&&!source.starts_with("peak-reuse:")))return true;
        const auto key=mindObservationKey(command,snapshots);if(key.empty())return true;std::lock_guard lock(mindMutex);loadMind();return currentMind.observationDue(key,settings);
    }
    void mindObserved(const Json& command){
        const auto key=command.value("observation_key",std::string{});if(key.empty()||!mindSettings().enabled)return;
        std::lock_guard lock(mindMutex);loadMind();currentMind.observed(key);
    }
    void mindInput(){mindForegroundUntil=serviceNowMs()+15000;mindNextSelection=0;}
    void mindTick(std::int64_t now,bool paused){
        if(cfg.channel||!mindSettings().enabled){mindLastTick=now;return;}
        const auto elapsed=mindLastTick?now-mindLastTick:0;mindLastTick=now;
        {std::lock_guard lock(mindMutex);loadMind();if(mindLoadFailed)return;currentMind.advance(elapsed,paused);}
        if(paused||now<mindNextSelection)return;mindNextSelection=now+1000;
        // Consistent lock order: activity -> personal/source checks -> mind. No
        // plugin invocation or model call while holding the mind lock.
        std::lock_guard activityLock(activityMutex);const auto plan=activityQueue();
        Json candidates=Json::array();
        if(inputActive||busy()||now<mindForegroundUntil)candidates.push_back({{"id","conversation"},{"kind","conversation"},{"score",1.0},{"foreground",true}});
        const auto active=plan.at("active");if(active.is_object())candidates.push_back({{"id",active.at("id")},{"kind","ongoing_activity"},{"score",.8}});
        const auto state=life.snapshot();if(state.value("energy",1.)<.35)candidates.push_back({{"id","rest"},{"kind","body_need"},{"score",.85}});
        Json pending;{std::lock_guard lock(mindMutex);pending=currentMind.view().at("suspended");}
        const auto policy=visibility();for(const auto& r:pending)if(r.at("status")=="pending"){
            bool allowed=true,expired=false;for(const auto& c:r.at("commands")){if(!mindSourcesAllowed(c,policy))allowed=false;if(c.value("expires_ms",now+1)<=now)expired=true;}
            if(expired){std::lock_guard lock(mindMutex);currentMind.settle(r.at("id"),r.at("revision"),"invalidated");continue;}
            if(r.at("interrupted").is_object()&&!mindSourcesAllowed(r.at("interrupted"),policy))allowed=false;
            if(allowed&&candidates.size()<5)candidates.push_back({{"id",r.at("id")},{"kind","unfinished_intention"},{"score",.55}});
        }
        if(plugins){const auto snapshots=plugins->liveSnapshots();for(const auto& o:snapshots.value("habitat",Json::object()).value("objects",Json::array())){
            if(candidates.size()>=6)break;const std::string key="object:"+o.at("id").get<std::string>()+":"+o.value("phase",std::string{});
            std::lock_guard lock(mindMutex);if(currentMind.observationDue(key,mindSettings()))candidates.push_back({{"id",o.at("id")},{"kind","unexamined_object"},{"score",.3},{"observation",{{"tool","habitat.act"},{"args",{{"id",o.at("id")},{"action","look"}}}}}});
        }}
        if(candidates.empty())candidates.push_back({{"id","quiet"},{"kind","quiet"},{"score",.1}});
        std::lock_guard lock(mindMutex);const auto before=currentMind.view();currentMind.select(candidates,mindSettings());const auto after=currentMind.view();
        if(before.value("focus",Json(nullptr))!=after.value("focus",Json(nullptr)))cognitiveAudit("cognition.attention.changed",{{"reason","attention_selection"},{"before",before.value("focus",Json(nullptr))},{"after",after.value("focus",Json(nullptr))},{"candidate_count",candidates.size()}});
    }
    Json resumeActivity(const Json& args){
        std::lock_guard activityLock(activityMutex);const auto settings=mindSettings();if(!settings.enabled)return toolFailure("cognition_disabled");
        const auto key=args.at("id").get<std::string>();const auto revision=args.at("expected_revision").get<std::int64_t>();
        std::lock_guard lock(mindMutex);loadMind();auto item=currentMind.resumable(key,revision);if(item.is_null())return toolFailure("resume_stale_or_expired");
        if(args.at("decision")=="abandon"){currentMind.settle(key,revision,"abandoned");saveMind();cognitiveAudit("cognition.activity.resume_decision",{{"status","abandoned"},{"reason","model_choice"},{"before_revision",revision},{"suspended_id",key}});return toolSuccess({{"abandoned",true}});}
        if(!activityQueue().at("active").is_null()||!activityQueue().at("entries").empty())return toolFailure("activity_queue_not_empty");
        const auto policy=visibility();Json commands=Json::array(),sources=Json::array();
        for(auto command:item.at("commands")){
            if(command.value("expires_ms",serviceNowMs()+1)<=serviceNowMs()||!mindSourcesAllowed(command,policy)){currentMind.settle(key,revision,"invalidated");saveMind();return toolFailure("resume_source_or_intention_changed");}
            for(const auto& ref:command.value("narrative_sources",Json::array()))if(std::find(sources.begin(),sources.end(),ref)==sources.end())sources.push_back(ref);
            for(const auto* field:{"id","source","expires_ms","narrative_sources"})command.erase(field);commands.push_back(std::move(command));
        }
        if(commands.empty())return toolFailure("resume_requires_new_plan");
        auto receipt=enqueueActivities(commands,"resume:"+key,sources);if(receipt.value("ok",false)){currentMind.settle(key,revision,"resumed");saveMind();}cognitiveAudit("cognition.activity.resume_decision",{{"status",receipt.value("ok",false)?"resumed":"rejected"},{"reason","model_choice"},{"before_revision",revision},{"suspended_id",key},{"receipt",receipt}});return receipt;
    }
    Json mindTool(const std::string& name,const Json& args,ProposalContext& context,const std::string& trace){
        if(!mindSettings().enabled)return toolFailure("cognition_disabled");
        if(!context.channelIdentity.empty()&&!ownerChannel(context.channelIdentity))return toolFailure("private_role_operation_denied");
        if(name=="activity.resume")return resumeActivity(args);
        if(name=="mind.observe")return toolSuccess(mindContext(&context));
        std::lock_guard lock(mindMutex);loadMind();if(mindLoadFailed)return toolFailure("cognition_state_unavailable");
        if(name=="mind.attend"){const bool ok=currentMind.attend(args.at("id"),args.at("expected_revision"),mindSettings());if(ok)saveMind();cognitiveAudit("cognition.attention.proposal",{{"status",ok?"accepted":"rejected"},{"reason",ok?"model_choice":"stale_or_foreground"},{"arguments",args},{"trace_id",trace},{"revision",currentMind.status().at("revision")}},trace);return ok?toolSuccess({{"selected",true},{"revision",currentMind.status().at("revision")}}):toolFailure("attention_stale_or_foreground");}
        if(name!="mind.interpret")return toolFailure("tool_not_available");
        const auto key=args.at("id").get<std::string>();if(!validRequestId(key))return toolFailure("interpretation_id");
        const auto before=get("ha.interpretation",key);if(before.revision>=256)return toolFailure("interpretation_change_limit");if(before.revision!=args.at("expected_revision"))return toolFailure("interpretation_revision");
        const auto policy=visibility();Json refs=Json::array();for(const auto& sourceId:args.at("source_ids")){
            const auto found=context.sources.find(sourceId.get<std::string>());if(found==context.sources.end())return toolFailure("interpretation_source_not_current");const auto& source=found->second;
            const auto row=get(source.scope,source.id);if(row.revision!=source.revision||!policy.source(source)||!appraisalExperienceRecord(row.value))return toolFailure("interpretation_source_not_experience");
            refs.push_back({{"id",source.id},{"scope",source.scope},{"kind",source.kind},{"revision",source.revision}});
        }
        auto index=get("ha.index","interpretations",Json::array());if(!before.revision&&index.value.size()>=32)return toolFailure("interpretation_limit");
        Json note={{"id",key},{"text",args.at("text")},{"state",args.at("state")},{"confidence",args.at("confidence")},{"sources",refs},{"created_at",before.value.value("created_at",serviceNowMs())},{"updated_at",serviceNowMs()},{"scope","tentative_interpretation_not_fact"}};
        std::vector<RecordMutation> changes={mutation("ha.interpretation",key,before,note,trace)};
        if(before.revision)changes.push_back({"ha.interpretation-history",key+"-"+std::to_string(before.revision),0,1,before.value.dump(),trace});
        else {index.value.push_back(key);changes.push_back(mutation("ha.index","interpretations",index,index.value,trace));}
        commit(changes,trace);cognitiveAudit("cognition.interpretation.changed",{{"status",args.at("state")},{"revision",before.revision+1},{"before_revision",before.revision},{"note",note},{"reason","model_proposal_accepted"},{"trace_id",trace}},trace);return toolSuccess({{"id",key},{"revision",before.revision+1},{"state",args.at("state")},{"fact",false}});
    }
