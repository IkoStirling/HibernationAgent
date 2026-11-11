    Json circadianView()const{
        if(cfg.channel)return cfg.channel->circadianState;
        return plugins?circadianContext(plugins->liveSnapshot("rhythm").value("circadian",Json::object())):Json::object();
    }
    bool circadianAsleep()const{return circadianView().value("phase",std::string{})=="sleeping";}
    Json circadianOperatorStatus()const{const auto value=circadianView();Json view={{"available",!cfg.channel&&plugins&&plugins->enabled("rhythm")},{"private_content_included",false}};for(const auto* field:{"configured","phase","remaining_seconds","elapsed_seconds","active_rest_seconds"})if(value.contains(field))view[field]=value.at(field);return view;}
    bool circadianHoldInput(const std::string& request){
        if(cfg.channel)return cfg.channel->circadianHold;
        if(!circadianAsleep()||!plugins)return false;
        plugins->event({{"kind","runtime.circadian.disturbance"},{"id",request},{"reason","message"}});
        const bool held=circadianAsleep();
        // A synchronous host decision prevents the widget's generic receiving
        // pose from accidentally waking the role. Pose release follows normally.
        plugins->event({{"kind","runtime.circadian.status"},{"sleeping",held}});
        cognitiveAudit("cognition.sleep.input",{{"request_id",request},{"status",held?"held_without_model":"woke"},{"reason","message"}},request);
        return held;
    }
    std::optional<JobOutcome> circadianPreflight(const JobClaim& claim,const Json& payload){
        const bool alone=intention_life::solitudeJob(claim.job.spec.kind);
        if(alone&&circadianAsleep())return JobOutcome{true,false,Json{{"status",payload.contains("continuous_task")||payload.contains("remembered_schedule")?"deferred":"no_update"},{"kind","solitude"},{"reason","circadian_sleep"},{"model_called",false}}.dump(),{}};
        if(!payload.value("circadian_held",false))return {};
        const auto request=payload.value("request_id",std::string{}),event=payload.value("event_id",std::string{}),trace=id("sleep-trace");
        JobOutcome outcome{true,false,Json{{"status","completed"},{"kind",payload.value("kind",std::string("user_message"))},{"request_id",request},{"trace_id",trace},{"occurred_at",serviceNowMs()},{"reason","circadian_sleep"},{"model_called",false}}.dump(),{}};
        outcome.updates.push_back({"ha.trace",trace,0,1,Json{{"created_at",serviceNowMs()},{"request_id",request},{"event_id",event},{"silent",true},{"model_called",false},{"validation","circadian_input_retained"},{"circadian",circadianView()}}.dump(),event});
        if(!request.empty()){auto saved=get("ha.requests",request);auto value=saved.value;value["status"]="completed";value["trace_id"]=trace;value["silent_reason"]="circadian_sleep";outcome.updates.push_back(mutation("ha.requests",request,saved,value,event));}
        return outcome;
    }
