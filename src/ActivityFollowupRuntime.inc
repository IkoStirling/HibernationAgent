    bool activityFollowupSourcesAllowed(const Json& watch)const{
        const auto refs=watch.value("narrative_sources",Json::array());const auto policy=visibility();
        return !refs.empty()&&std::all_of(refs.begin(),refs.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);});
    }
    Json enqueueWithFollowup(const Json& args,ProposalContext& context,const std::string& trace){
        if(!args.contains("follow_up")||args.at("follow_up").is_null())return enqueueActivities(args.at("commands"),trace,activityNarrativeSources(context));
        if(cfg.channel||(!context.channelIdentity.empty()&&!ownerChannel(context.channelIdentity))||!context.skillId.empty()||!context.taskId.empty()||context.eventKind!="user_message")return toolFailure("follow_up_owner_dialogue_required");
        if(!ayt::serializer::validateJsonDocument(args.at("follow_up").dump(),activityFollowupParameters().dump()))return toolFailure("follow_up_arguments");
        const auto& request=args.at("follow_up");if(request.at("topic").get_ref<const std::string&>().find_first_not_of(" \t\r\n")==std::string::npos||args.at("commands").empty())return toolFailure("follow_up_requires_activity");
        const auto origin=context.sources.find(context.channelInputId.empty()?context.eventId:context.channelInputId);if(origin==context.sources.end()||origin->second.scope!=archiveScope||!visibility().source(origin->second))return toolFailure("follow_up_source_required");
        const auto narrative=activityNarrativeSources(context);const Json watchSources={{"narrative_sources",narrative}};if(!activityFollowupSourcesAllowed(watchSources))return toolFailure("follow_up_source_unavailable");
        std::lock_guard activityGuard(activityMutex);std::lock_guard lock(rememberedMutex);auto index=get("ha.schedules","index",Json::array());std::size_t live=0,watches=0;std::set<std::string> usedSlots;
        for(const auto& key:index.value){const auto row=get("ha.schedules",key);if(row.revision&&(row.value.at("status")=="active"||row.value.at("status")=="paused")){++live;if(row.value.contains("follow_up")){++watches;usedSlots.insert(row.value.at("follow_up").value("slot",std::string{}));}}}
        // Reuse four scheduler slots with monotonic run tokens. Historical
        // promises and results keep their unique IDs in the encrypted ledger.
        const auto currentJobs=jobs();std::string slot;
        for(int n=0;n<4&&slot.empty();++n){const auto candidate="routine.followup-slot-"+std::to_string(n);if(usedSlots.contains(candidate))continue;const auto found=std::find_if(currentJobs.begin(),currentJobs.end(),[&](const auto& job){return job.spec.id==candidate;});if(found!=currentJobs.end()&&found->state!=JobState::Running)slot=candidate;else if(found==currentJobs.end()&&currentJobs.size()<32)slot=candidate;}
        if(index.value.size()>=240){Json retained=Json::array();std::size_t history=0;for(auto it=index.value.rbegin();it!=index.value.rend();++it){const auto row=get("ha.schedules",*it);const bool inactive=row.revision&&row.value.contains("follow_up")&&row.value.at("status")!="active"&&row.value.at("status")!="paused";if(inactive&&history++>=64)continue;retained.push_back(*it);}std::reverse(retained.begin(),retained.end());index.value=std::move(retained);}
        if(index.value.size()>=256||live>=32||watches>=4||slot.empty())return toolFailure("follow_up_capacity");
        std::string key;const auto now=serviceNowMs();const auto deadline=now+request.at("timeout_seconds").get<std::int64_t>()*1000;
        auto receipt=enqueueActivities(args.at("commands"),trace,narrative,[&](const Json& ids,const Json& plan){
            key="followup-"+contentDigest(trace+ids.dump()).substr(0,24);
            const auto& source=origin->second;const auto refs=Json::array({{{"id",source.id},{"revision",source.revision},{"kind",source.kind},{"scope",source.scope}}});
            auto value=reminder::fresh(key,request.at("topic"),"short",{{"type","at"},{"first_ms",deadline}},refs,now);
            value["status"]="active";value["when_spec"]={{"type","at"},{"after_minutes",std::max(1,(request.at("timeout_seconds").get<int>()+59)/60)}};
            value["reply_route"]=contextReplyRoute(context);
            // A promised result is not an ordinary probabilistic recollection.
            // Keep it through the next daily-budget reset, but not indefinitely.
            value["expires_ms"]=now+2*reminder::day;value["fade_ms"]=value.at("expires_ms");
            value["follow_up"]={{"activity_ids",ids},{"slot",slot},{"state","waiting"},{"deadline_ms",deadline},{"narrative_sources",narrative},{"delivery_failures",0}};
            const auto queueBefore=get("ha.autonomy","queue");std::vector<RecordMutation> changes={mutation("ha.autonomy","queue",queueBefore,plan,trace),mutation("ha.schedules",key,get("ha.schedules",key),value,trace)};
            index.value.push_back(key);changes.push_back(mutation("ha.schedules","index",index,index.value,trace));
            for(const auto& activity:ids)changes.push_back({"ha.activity-followups",activity,0,1,Json{{"schedule_id",key}}.dump(),trace});
            commit(std::move(changes),trace);auditActivities(queueBefore.value,plan,"queue_commit");
        });
        if(!receipt.value("ok",false))return receipt;
        rememberedArm(key,now);nextRememberedTick=0;bool armed=false;for(const auto& job:jobs())if(job.spec.id==slot&&(job.state==JobState::Pending||job.state==JobState::Paused)&&Json::parse(job.spec.payload).value("remembered_schedule",std::string{})==key)armed=true;
        if(!armed){auto row=get("ha.schedules",key);auto value=row.value;value["status"]="blocked";value["follow_up"]["state"]="scheduler_unavailable";rememberedSave(key,row,value,"followup-scheduler-unavailable");}
        receipt["result"]["follow_up"]={{"registered",armed},{"id",key},{"deadline_ms",deadline},{"trigger","activity_terminal_or_timeout"}};return receipt;
    }
    void recordFollowupActivityOutcome(const Json& activity){
        const auto activityId=activity.at("id").get<std::string>();if(!get("ha.activity-followups",activityId).revision)return;
        const auto key="followup-outcome-"+activityId;if(get(archiveScope,key).revision)return;
        Json actual=Json::object();for(const auto* field:{"status","ended_ms","started_ms","gather_result","effect_result"})if(activity.contains(field))actual[field]=activity.at(field);
        Json request=Json::object();for(const auto* field:{"kind","object_id","recipe_id","item_id","action"})if(activity.contains(field))request[field]=activity.at(field);
        const Json event={{"kind","outer_event"},{"text","一项约定接续的桌面活动已有实际终止回执；request是原先打算，actual才是本次状态，不代表所有预期都已实现。"},{"activity_id",activityId},{"request",request},{"actual",actual},{"created_at",serviceNowMs()},{"sources",activity.value("narrative_sources",Json::array())}};
        commit({{archiveScope,key,0,1,event.dump(),"activity-terminal"}},"activity-terminal");
    }
    void refreshActivityFollowup(const std::string& key,std::int64_t now){
        auto before=get("ha.schedules",key);if(!before.revision||before.value.at("status")!="active"||!before.value.contains("follow_up"))return;
        auto value=before.value;auto& watch=value["follow_up"];if(!value.at("pending").is_null()&&value.at("pending").value("state",std::string{})=="running")return;
        if(!activityFollowupSourcesAllowed(watch)){value["status"]="blocked";value["pending"]=nullptr;watch["state"]="source_unavailable";rememberedSave(key,before,value,"followup-source-unavailable");rememberedPauseSlot(key);return;}
        if(watch.value("deferred_budget",false)){
            const auto budget=backgroundBudgetStatus(),hourly=modelBudgetStatus();if(budget.at("blocked").get<bool>()||hourly.at("remaining").get<int>()<=hourly.at("background_reserve").get<int>())return;
            watch["deferred_budget"]=false;value["pending"]=nullptr;value["when"]["first_ms"]=now;value["next_ms"]=now;rememberedSave(key,before,value,"followup-budget-ready");return;
        }
        if(watch.at("state")!="waiting")return;
        Json outcomes=Json::array(),refs=value.at("references");bool terminal=true;
        for(const auto& item:watch.at("activity_ids")){
            const auto outcomeId="followup-outcome-"+item.get<std::string>();const auto outcome=get(archiveScope,outcomeId);
            if(!outcome.revision){terminal=false;outcomes.push_back({{"activity_id",item},{"status","not_confirmed"}});continue;}
            outcomes.push_back({{"activity_id",item},{"status",outcome.value.at("actual").value("status",std::string("unknown"))}});
            refs.push_back({{"id",outcomeId},{"scope",archiveScope},{"revision",outcome.revision},{"kind","outer_event"}});
        }
        if(!terminal&&now<watch.at("deadline_ms").get<std::int64_t>())return;
        watch["state"]=terminal?"ready":"timed_out";watch["observed_outcomes"]=outcomes;value["references"]=refs;value["next_ms"]=now;
        value["pending"]={{"key","followup:"+key},{"start_ms",now},{"end_ms",value.at("expires_ms")},{"at_ms",now},{"state","armed"},{"trigger",{{"event","activity_follow_up"},{"all_terminal",terminal},{"outcomes",outcomes}}}};
        rememberedSave(key,before,value,"followup-ready");
    }
    bool deferActivityFollowup(const JobInfo& job,const Json& result,const Doc& before,const std::string& key){
        if(!before.value.contains("follow_up")||before.value.at("status")!="active"||before.value.at("pending").value("state",std::string{})!="running"||result.value("status",std::string{})=="completed")return false;
        auto value=before.value;const auto now=serviceNowMs();const auto budget=backgroundBudgetStatus(),hourly=modelBudgetStatus();
        const bool waiting=budget.at("blocked").get<bool>()||hourly.at("remaining").get<int>()<=hourly.at("background_reserve").get<int>();
        auto& watch=value["follow_up"];watch["deferred_budget"]=waiting;if(!waiting)watch["delivery_failures"]=watch.value("delivery_failures",0)+1;
        if(watch.value("delivery_failures",0)>=3||reminder::phase(value,now)=="expired")return false;
        std::int64_t at=now+60000;if(budget.at("blocked").get<bool>())at=std::max(at,budget.at("resets_at_ms").get<std::int64_t>());else if(waiting)at=std::max(at,hourly.at("resets_at_ms").get<std::int64_t>());
        value["pending"]=nullptr;value["when"]["first_ms"]=at;value["next_ms"]=at;value["last_decision"]={{"result",waiting?"deferred_budget":"delivery_retry"},{"at_ms",now},{"woke",false},{"spoke",false}};
        rememberedSave(key,before,value,"followup-deferred");nextRememberedTick=0;return true;
    }
    Json activityFollowupStatus()const{
        std::lock_guard lock(rememberedMutex);Json items=Json::array();const auto index=get("ha.schedules","index",Json::array());
        for(auto it=index.value.rbegin();it!=index.value.rend()&&items.size()<24;++it){const auto row=get("ha.schedules",*it);if(!row.revision||!row.value.contains("follow_up"))continue;const auto& watch=row.value.at("follow_up");
            items.push_back({{"id",*it},{"status",row.value.at("status")},{"state",watch.at("state")},{"deadline_ms",watch.at("deadline_ms")},{"next_ms",row.value.at("next_ms")},{"deferred_budget",watch.value("deferred_budget",false)},{"delivery_failures",watch.value("delivery_failures",0)},{"spoke",row.value.at("last_decision").is_object()&&row.value.at("last_decision").value("spoke",false)}});
        }return {{"items",items},{"private_content_included",false},{"uses_existing_scheduler",true}};
    }
