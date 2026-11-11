    ActivityGenerator activityGenerator;mutable std::recursive_mutex activityMutex;
    Json activityPromptHistory=Json::object();
    std::future<Json> activityEffect;std::shared_ptr<std::atomic<bool>> activityEffectCancelled;
    std::string activityEffectId,activityEffectTool;Json activityEffectArguments=Json::object();
    std::uint64_t activityGenerationEpoch=0;
    std::int64_t nextActivityFill=0,nextActivityTick=0,nextActivityMaintenance=0;
    bool activityInitialized=false,activityModeNotified=false,activityLastMode=false,activitySuspended=false;
    bool activityRestSuspended=false,activityBudgetWasBlocked=false;
    std::int64_t nextActivityBudgetCheck=0;
    std::string activityBudgetWait;
    std::string activityError;
    bool activityRoutineWaiting()const{
        if(!plugins||!plugins->enabled("rhythm"))return false;const auto rhythm=plugins->liveSnapshot("rhythm");
        const auto phase=rhythm.value("circadian",Json::object()).value("phase",std::string{});
        return rhythm.value("circadian_admission_ready",false)||phase=="queued"||phase=="requesting"||phase=="sleeping";
    }
    bool activityNarrativeSourceAllowed(const Json& ref,const MemoryVisibility& policy)const{
        if(cfg.channel)return false;
        const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};const auto row=get(source.scope,source.id);
        return row.revision==source.revision&&policy.source(source)&&(!row.value.contains("sources")||policy.allows(row.value));
    }
    void captureActivityNarrativeSources(const Json& plan,ProposalContext& context)const{
        const auto policy=visibility();const auto capture=[&](const Json& item){
            if(!item.is_object())return;const auto refs=item.value("narrative_sources",Json::array());
            if(refs.empty()||!std::all_of(refs.begin(),refs.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);}))return;
            for(const auto& ref:refs){const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};context.narrativeSources[source.scope+":"+source.id]=source;}
        };
        if(plan.contains("active"))capture(plan.at("active"));for(const auto* field:{"entries","next","recent"})for(const auto& item:plan.value(field,Json::array()))capture(item);
    }
    Json activityForModel(const Json& plan,const MemoryVisibility* supplied=nullptr)const{
        const auto owned=supplied?MemoryVisibility{}:visibility();const auto& policy=supplied?*supplied:owned;std::map<std::string,bool> checked;
        return activityNarrativeProjection(plan,[&](const Json& ref){
            const auto key=ref.dump();if(const auto found=checked.find(key);found!=checked.end())return found->second;
            return checked[key]=activityNarrativeSourceAllowed(ref,policy);
        });
    }
    Json activityNarrativeSources(const ProposalContext& context)const{
        std::map<std::string,Source> sources=context.narrativeSources;
        for(const auto& [key,source]:context.sources)sources[source.scope+":"+source.id]=source;
        Json refs=Json::array();for(const auto& [key,source]:sources)refs.push_back({{"id",source.id},{"revision",source.revision},{"kind",source.kind},{"scope",source.scope}});return refs;
    }
    Json activityCarryFeedback(bool modelView=true)const{
        auto value=get("ha.autonomy","carry-feedback",Json::object()).value;
        if(modelView&&value.value("access_revision",std::int64_t(-1))!=get("ha.access","current").revision)return Json::object();
        if(modelView){const auto refs=value.value("narrative_sources",Json::array());const auto policy=visibility();if(refs.empty()||!std::all_of(refs.begin(),refs.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);}))return Json::object();value.erase("narrative_sources");value.erase("access_revision");}
        return serviceNowMs()-value.value("created_at",std::int64_t(0))<=600000?value:Json::object();
    }
    Json activityReviewFeedback(ProposalContext* context=nullptr)const{
        auto value=get("ha.autonomy","review-feedback",Json::object()).value;if(value.empty())return value;
        const auto age=serviceNowMs()-value.value("created_at",std::int64_t(0));if(age<0||age>600000||value.value("access_revision",std::int64_t(-1))!=get("ha.access","current").revision)return Json::object();
        const auto refs=value.value("narrative_sources",Json::array());const auto policy=visibility();
        if(refs.empty()||!std::all_of(refs.begin(),refs.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);}))return Json::object();
        if(context)for(const auto& ref:refs){const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};context->narrativeSources[source.scope+":"+source.id]=source;}
        value.erase("narrative_sources");value.erase("access_revision");return value;
    }
    void saveActivityReviewFeedback(const ActivityBatch& batch,const std::string& key,bool accepted){
        const auto before=get("ha.autonomy","review-feedback",Json::object());
        if(accepted){if(!before.value.empty())commit({mutation("ha.autonomy","review-feedback",before,Json::object(),key)},key);return;}
        if(batch.status!="activity_review_rejected"||!batch.review.is_object())return;
        const Json value={{"generation",key},{"created_at",serviceNowMs()},{"decision",batch.review.at("decision")},{"reason",batch.review.at("reason")},{"guidance",batch.review.at("guidance")},{"narrative_sources",batch.narrativeSources},{"access_revision",get("ha.access","current").revision}};
        commit({mutation("ha.autonomy","review-feedback",before,value,key)},key);
    }
    Json activityEvidence(const Json& plan)const{
        Json evidence=Json::array();std::set<std::string> seen;const auto policy=visibility();const auto projection=get("ha.context","dialogue").value;
        const auto add=[&](const std::string& key){
            if(key.empty()||evidence.size()>=8||!seen.insert(key).second)return;
            const auto row=get(archiveScope,key);if(!row.revision)return;const auto kind=row.value.value("kind",std::string{});
            if(kind!="user_message"&&kind!="assistant_message"&&kind!="tool_observation"&&kind!="tool_attempt"&&kind!="outer_event")return;
            const Source ref{key,row.revision,kind,archiveScope};if(!policy.source(ref)||(row.value.contains("sources")&&!policy.allows(row.value))||isLegacyMetaReply(row.value)||(kind=="assistant_message"&&dialogueExcluded(projection,key)))return;
            Json data;
            if(kind=="tool_observation"||kind=="tool_attempt"){
                const auto delivery=get("ha.image-delivery",key);const bool acknowledged=delivery.value.value("ever_acknowledged",false);
                data=compactObservation(row.value,acknowledged,acknowledged?"acknowledged":"unknown");data["image_content_in_context"]=false;
            }else{const auto text=row.value.value("text",std::string{});data={{"kind",kind},{"text",excerpt(text,900)},{"text_is_excerpt",text.size()>900},{"created_at",row.value.value("created_at",std::int64_t(0))}};if(kind=="outer_event"&&row.value.contains("observation"))data["event_kind"]=row.value.at("observation").value("kind",std::string{});}
            evidence.push_back({{"id",key},{"revision",row.revision},{"kind",kind},{"scope",archiveScope},{"authorship",recordAuthorship(kind)},{"data",std::move(data)}});
        };
        const auto recent=plan.value("recent",Json::array());std::size_t own=0;for(auto it=recent.rbegin();it!=recent.rend()&&own<2;++it){const auto before=evidence.size();add(it->value("source_id",std::string{}));if(evidence.size()!=before)++own;}
        const auto observations=get("ha.observations","recent",Json::array()).value;for(auto it=observations.rbegin();it!=observations.rend()&&evidence.size()<3;++it)add(it->get<std::string>());
        const auto world=get("ha.world","recent",Json::array()).value;for(auto it=world.rbegin();it!=world.rend()&&evidence.size()<4;++it)add(it->get<std::string>());
        auto dialogueIds=recentDialogueIds();if(dialogueIds.size()>96)dialogueIds.erase(dialogueIds.begin(),dialogueIds.end()-96);
        Json encounters=Json::array();for(const auto& row:readIds(archiveScope,dialogueIds)){
            auto value=Json::parse(row.payload);const auto kind=value.value("kind",std::string{});if(kind!="user_message"&&kind!="assistant_message")continue;
            const Source ref{row.id,row.revision,kind,archiveScope};if(!policy.source(ref)||(value.contains("sources")&&!policy.allows(value))||isLegacyMetaReply(value)||(kind=="assistant_message"&&dialogueExcluded(projection,row.id)))continue;
            value["id"]=row.id;value["revision"]=row.revision;encounters.push_back(std::move(value));
        }
        const auto offered=get("ha.autonomy","offered-encounters",Json::array()).value;
        for(const auto& pick:conversationOpportunities(encounters,offered)){
            const auto before=evidence.size();add(pick.at("id"));if(evidence.size()==before)continue;
            for(const auto* field:{"context_role","encounter_id","not_a_current_request"})evidence.back()[field]=pick.at(field);
        }
        return evidence;
    }
    void markActivityEncounters(const ActivityBatch& batch,const std::string& key){
        const auto policy=visibility();Json actual=Json::array();for(const auto& item:batch.evidence){
            if(item.value("scope",std::string{})!=archiveScope||item.value("kind",std::string{})!="user_message")continue;
            const Source ref{item.at("id"),item.at("revision"),"user_message",archiveScope};const auto row=get(archiveScope,ref.id);
            if(row.revision==ref.revision&&policy.source(ref)&&(!row.value.contains("sources")||policy.allows(row.value)))actual.push_back(item);
        }
        const auto prior=get("ha.autonomy","offered-encounters",Json::array());const auto changed=rememberOfferedEncounters(prior.value,actual);
        if(changed!=prior.value)commit({mutation("ha.autonomy","offered-encounters",prior,changed,key)},key);
    }
    Json applyActivityCarry(const ActivityBatch& batch,const std::string& key){
        ProposalContext context;context.eventKind="activity_planning";const auto policy=visibility();
        for(const auto& item:batch.evidence){
            if(item.value("scope",std::string{})!=archiveScope)continue;
            const Source ref{item.at("id"),item.at("revision"),item.at("kind"),archiveScope};const auto row=get(archiveScope,ref.id);
            if(row.revision==ref.revision&&row.value.value("kind",std::string{})==ref.kind&&policy.source(ref)&&(!row.value.contains("sources")||policy.allows(row.value)))context.sources[ref.id]=ref;
        }
        const auto cancelled=[&]{return !autonomousMode()||runtimePaused||inputActive||busy()||batch.key!=activityKey();};
        auto results=applyPersonalCarry(batch.closeThreads,context,key,key,cancelled,PersonalCarryOperation::Close);
        for(auto& result:applyPersonalCarry(batch.carryForward,context,key,key,cancelled))results.push_back(std::move(result));
        if(batch.reconsideration.is_object())results.push_back({{"id",batch.reconsideration.at("id")},{"tool","self.reconsider"},{"receipt",applyPrivateOperation("self.reconsider",batch.reconsideration,context,key+".reconsideration",key,cancelled)}});
        if(batch.appraisal.is_object())results.push_back({{"id",batch.appraisal.at("source_id")},{"tool","state.appraise"},{"receipt",applyPrivateOperation("state.appraise",batch.appraisal,context,key+".appraisal",key,cancelled)}});
        if(batch.feeling.is_object())results.push_back({{"id",batch.feeling.at("source_id")},{"tool","self.feel"},{"receipt",applyPrivateOperation("self.feel",batch.feeling,context,key+".feeling",key,cancelled)}});
        if(batch.schedule.is_object())results.push_back({{"id",batch.schedule.at("id")},{"tool","schedule.create"},{"receipt",applyPrivateOperation("schedule.create",batch.schedule,context,key+".schedule",key,cancelled)}});
        if(batch.circadianRoutine.is_object()){
            auto receipt=toolFailure("circadian_unavailable");if(cancelled())receipt=toolFailure("cancelled");else if(plugins&&plugins->enabled("rhythm")&&cfg.plugins.value("rhythm",Json::object()).value("circadianEnabled",true))receipt=observedTool("rhythm.configure",plugins->invoke("rhythm.configure",batch.circadianRoutine,cancelled),context,key,&batch.circadianRoutine);
            results.push_back({{"id",key+".routine"},{"tool","rhythm.configure"},{"receipt",receipt}});cognitiveAudit("cognition.sleep.proposal",{{"status",receipt.value("ok",false)?"accepted":"rejected"},{"trace_id",key},{"proposal",batch.circadianRoutine},{"receipt",receipt}},key);
        }
        if(!results.empty()){
            Json summary=Json::array();for(const auto& result:results){const auto& receipt=result.at("receipt");Json item={{"id",result.at("id")},{"tool",result.at("tool")},{"ok",receipt.value("ok",false)}};
                if(receipt.value("ok",false)){if(receipt.at("result").contains("revision"))item["revision"]=receipt.at("result").at("revision");else if(receipt.at("result").contains("circadian"))item["revision"]=receipt.at("result").at("circadian").at("preferences_revision");item["deduplicated"]=receipt.at("result").value("deduplicated",false);}else item["error"]=receipt.value("error",Json::object());summary.push_back(std::move(item));}
            const auto before=get("ha.autonomy","carry-feedback");commit({mutation("ha.autonomy","carry-feedback",before,{{"generation",key},{"created_at",serviceNowMs()},{"access_revision",get("ha.access","current").revision},{"narrative_sources",batch.narrativeSources},{"results",summary}},key)},key);
        }return results;
    }
    std::int64_t activityPlanningBase()const{return static_cast<std::int64_t>(cfg.plugins.value("widget",Json::object()).value("autonomyRefillSeconds",60.0)*1000);}
    void planningResult(bool success,bool empty,std::int64_t now){
        auto before=get("ha.autonomy","cadence",emptyPlanningCadence());if(!validPlanningCadence(before.value)){nextActivityFill=now+activityPlanningBase();return;}
        const auto value=planningOutcome(before.value,success,empty,now,activityPlanningBase());nextActivityFill=value.at("next_ms");commit({mutation("ha.autonomy","cadence",before,value,"activity-planning")},"activity-planning");
    }
    void planningStimulus(std::int64_t now,bool user){
        auto before=get("ha.autonomy","cadence",emptyPlanningCadence());if(!validPlanningCadence(before.value))return;
        if(before.value.at("quiet")==0&&before.value.at("failures")==0)return;
        auto value=emptyPlanningCadence();value["last_ms"]=now;nextActivityFill=std::max(now,std::min(nextActivityFill,now+(user?30000:activityPlanningBase())));value["next_ms"]=nextActivityFill;commit({mutation("ha.autonomy","cadence",before,value,"activity-stimulus")},"activity-stimulus");
    }
    bool autonomousMode()const{return cfg.desktopEnabled&&plugins&&plugins->enabled("widget")&&cfg.plugins.value("widget",Json::object()).value("enabled",false)&&cfg.plugins.value("widget",Json::object()).value("autonomousMode",false);}
    Json activityQueue()const{return get("ha.autonomy","queue",{{"version",1},{"entries",Json::array()},{"active",nullptr},{"recent",Json::array()}}).value;}
    void stopActivities(){std::lock_guard lock(activityMutex);mindSuspend(activityQueue(),"shutdown");activityGenerator.stop();if(activityEffectCancelled)activityEffectCancelled->store(true);if(activityEffect.valid())activityEffect.wait();collectActivityEffect(serviceNowMs());}
    void saveActivities(const Json& value){auto before=get("ha.autonomy","queue");commit({mutation("ha.autonomy","queue",before,value,"autonomy")},"autonomy");auditActivities(before.value,value,"queue_commit");}
    Json activityStatus(bool modelView=false)const{std::lock_guard lock(activityMutex);const auto raw=activityQueue();const auto value=modelView?activityForModel(raw):raw;return {{"enabled",autonomousMode()},{"queued",value.at("entries").size()},{"active",value.at("active")},{"generating",activityGenerator.running()},{"last_error",activityError},{"planning_deferred_reason",circadianAsleep()?"circadian_sleep":activityRoutineWaiting()?"circadian_window":activityBudgetWait},{"personal_carry_feedback",activityCarryFeedback(modelView)},{"recent",value.at("recent")},{"planning_cadence",get("ha.autonomy","cadence",emptyPlanningCadence()).value},{"next_planning_ms",nextActivityFill},{"personal_pursuit",personalPursuitAvailability()},{"local_foraging",get("ha.autonomy","foraging-cadence",Json::object()).value},{"reusable_count",activityLibrary().value("items",Json::array()).size()},{"economy_local_fallback",activityEconomyFallback}};}
    void finishActivity(Json& plan,const std::string& status){if(plan.at("active").is_null())return;auto result=plan.at("active");result["status"]=status;result["ended_ms"]=serviceNowMs();recordFollowupActivityOutcome(result);result.erase("effect_result");if(result.contains("intention"))result["attempt_source_id"]=recordIntentionAttempt(result);mindActivity(result,status);if(status=="completed"){rememberActivity(result);mindObserved(result);}plan["recent"].push_back(result);while(plan["recent"].size()>8)plan["recent"].erase(0);plan["active"]=nullptr;}
    void cancelActivities(const std::string& reason){
        std::lock_guard lock(activityMutex);++activityGenerationEpoch;activityGenerator.cancel();activityPromptHistory=Json::object();if(activityEffectCancelled)activityEffectCancelled->store(true);
        nextActivityFill=(reason=="character_hidden"||reason=="paused"||reason=="mode_disabled")?std::max(nextActivityFill,serviceNowMs()+30000):serviceNowMs()+30000;
        if(reason=="new_input"||reason=="input_active"||reason=="pointer_interaction"||reason=="configuration_changed")planningStimulus(serviceNowMs(),true);
        auto plan=activityQueue();mindCancelPending(reason);if(plan.at("active").is_null()&&plan.at("entries").empty())return;mindSuspend(plan,reason);finishActivity(plan,reason);
        for(auto pending:plan.at("entries")){mindActivity(pending,"not_started_"+reason);pending["status"]="not_started_"+reason;pending["ended_ms"]=serviceNowMs();recordFollowupActivityOutcome(pending);if(pending.contains("intention"))recordIntentionAttempt(pending);}
        plan["entries"]=Json::array();saveActivities(plan);if(plugins)plugins->event({{"kind","runtime.activity_cancel"},{"reason",reason}});
    }
    void pruneChangedIntentionActivities(std::int64_t now){
        auto plan=activityQueue();std::optional<MemoryVisibility> policy;std::map<std::string,bool> checked;
        const auto pruned=pruneActivityIntentions(plan,now,[&](const Json& binding){const auto key=binding.dump();if(const auto found=checked.find(key);found!=checked.end())return found->second;if(!policy)policy=visibility();return checked[key]=currentIntention(binding,&*policy);});
        if(!pruned.stopActive&&pruned.removed.empty())return;
        ++activityGenerationEpoch;activityGenerator.cancel();nextActivityFill=now+30000;
        if(pruned.stopActive){if(activityEffectCancelled)activityEffectCancelled->store(true);finishActivity(plan,"intention_changed");}
        for(auto removed:pruned.removed){recordFollowupActivityOutcome(removed);if(removed.contains("intention"))removed["attempt_source_id"]=recordIntentionAttempt(removed);plan["recent"].push_back(std::move(removed));}
        while(plan["recent"].size()>8)plan["recent"].erase(0);plan["entries"]=pruned.pending;saveActivities(plan);
        if(pruned.stopActive){if(plugins)plugins->event({{"kind","runtime.activity_cancel"},{"reason","intention_changed"}});nextActivityTick=now+500;}
    }
    Json activityCommands(const Json& snapshots,bool background=false,bool declarations=false)const{
        std::set<std::string> tools;for(const auto& item:plugins->catalog())tools.insert(item.at("name").get<std::string>());
        Json commands=Json::array({"wait"});const auto widget=snapshots.value("widget",Json::object());
        if(tools.contains("widget.move")&&(declarations||!background||widget.value("roaming",false)))commands.push_back("move_to");
        if(tools.contains("widget.animate"))commands.push_back("pose");
        const bool hasObjects=declarations||!snapshots.value("habitat",Json::object()).value("objects",Json::array()).empty();
        if(tools.contains("habitat.act")){commands.push_back("eat_nearest");commands.push_back("look_nearest");if(hasObjects)commands.push_back("interact");}
        if(tools.contains("habitat.gather")&&snapshots.value("habitat",Json::object()).value("inventory_available",false)){commands.push_back("gather");commands.push_back("gather_for_recipe");}
        if(tools.contains("canteen.store"))commands.push_back("store_supplies");
        if(tools.contains("canteen.eat_supply"))commands.push_back("eat_inventory");
        if(tools.contains("habitat.arrange")&&hasObjects)commands.push_back("arrange");
        if(tools.contains("habitat.create"))commands.push_back("create_drop");
        const auto kitchen=snapshots.value("canteen",Json::object()).value("current",Json::object());
        if(tools.contains("canteen.cook")&&(declarations||!kitchen.value("can_cook",Json::array()).empty()))commands.push_back("canteen_cook");
        if(tools.contains("canteen.take")&&(declarations||kitchen.value("ready_count",0)>0||!kitchen.value("can_cook",Json::array()).empty()||kitchen.value("job",Json(nullptr)).is_object())&&!kitchen.value("delivery_pending",false))commands.push_back("take_meal");
        const auto atelier=snapshots.value("atelier",Json::object());
        if(tools.contains("atelier.write")&&(declarations||creativeSlotAvailable(atelier)))commands.push_back("creative_note");
        if(tools.contains("atelier.append")&&(declarations||!visibleContinuationDrafts(atelier).empty()))commands.push_back("creative_continue");
        if(tools.contains("atelier.read")&&(declarations||!atelier.value("pieces",Json::array()).empty()))commands.push_back("creative_read");
        const auto reading=snapshots.value("reading",Json::object());
        if(reading.value("autonomous_allowed",false)){if(tools.contains("reading.search"))commands.push_back("reference_search");if(tools.contains("reading.open")&&(declarations||!reading.value("last_search",Json::object()).value("pages",Json::array()).empty()||!reading.value("last_read",Json::object()).value("key",std::string{}).empty()))commands.push_back("reference_read");}
        const auto game=snapshots.value("pocket",Json::object()).value("current",Json::object()).value("game",Json(nullptr));
        if(tools.contains("pocket.start")&&(declarations||!game.is_object()||game.value("status",std::string{})!="active"))commands.push_back("puzzle_start");
        if(tools.contains("pocket.press")&&(declarations||(game.is_object()&&game.value("status",std::string{})=="active")))commands.push_back("puzzle_press");
        if(tools.contains("pocket.leave")&&(declarations||(game.is_object()&&game.value("status",std::string{})=="active")))commands.push_back("puzzle_leave");
        if(personalPursuitEnabled()&&(declarations||personalPursuitReady(serviceNowMs())))commands.push_back("pursue_intention");
        if(screenGlanceEnabled()&&(declarations||screen_glance::due(screenGlanceState(),serviceNowMs())))commands.push_back("screen_glance");
        if(tools.contains("rhythm.rest"))commands.push_back("rest");return commands;
    }
    Json enqueueActivities(const Json& commands,const std::string& source,const Json& narrativeSources=Json::array(),const std::function<void(const Json&,const Json&)>& save={}){
        std::lock_guard lock(activityMutex);
        if(!autonomousMode())return toolFailure("autonomous_mode_disabled");if(get("ha.control","current").value.at("paused")==true)return toolFailure("runtime_paused");
        auto checked=validateActivities(Json{{"version",1},{"commands",commands}}.dump());if(!checked.ok)return toolFailure(checked.code.c_str());const auto& normalized=checked.value.at("commands");
        auto plan=activityQueue();if(plan.at("entries").size()+normalized.size()+(plan.at("active").is_null()?0:1)>6)return toolFailure("activity_queue_full");
        const auto snapshots=plugins->liveSnapshots();const auto available=activityCommands(snapshots,source.starts_with("activity-generation")||source.starts_with("peak-reuse:"));const auto widget=snapshots.value("widget",Json::object());
        const auto kitchenNow=snapshots.value("canteen",Json::object()).value("current",Json::object());std::set<std::string> plannedMeals;const auto preparing=kitchenNow.value("job",Json(nullptr));if(preparing.is_object())plannedMeals.insert(preparing.at("recipe_id"));
        for(const auto& command:normalized){if(!mindObservationAllowed(command,source,snapshots))return toolFailure("observation_recently_satisfied");const auto kind=command.at("kind").get<std::string>();if(std::find(available.begin(),available.end(),kind)==available.end())return toolFailure("activity_dependency_missing");
            if(kind=="pursue_intention"){const auto domains=personalPursuitDomains();if(std::find(domains.begin(),domains.end(),command.at("domain"))==domains.end())return toolFailure("personal_pursuit_domain_unavailable");}
            if(kind=="canteen_cook")plannedMeals.insert(command.at("recipe_id"));
            if(kind=="take_meal"){bool availableMeal=false;for(const auto& meal:kitchenNow.value("shelf",Json::array()))if(meal.at("status")=="ready"&&(!command.contains("batch_id")||meal.at("id")==command.at("batch_id"))&&(!command.contains("recipe_id")||meal.at("recipe_id")==command.at("recipe_id")))availableMeal=true;
                if(!command.contains("batch_id")&&(!command.contains("recipe_id")?!plannedMeals.empty():plannedMeals.contains(command.at("recipe_id").get<std::string>())))availableMeal=true;if(!availableMeal)return toolFailure("activity_canteen_meal_unavailable");}
            if(command.contains("intention")&&!currentIntention(command.at("intention")))return toolFailure("activity_intention_changed_or_unavailable");
            if(kind=="reference_read"){
                const auto reading=snapshots.value("reading",Json::object());const auto search=reading.value("last_search",Json::object());const auto pages=search.value("pages",Json::array());const auto read=reading.value("last_read",Json::object());
                const bool searched=search.value("language",std::string{})==command.at("language").get<std::string>()&&std::any_of(pages.begin(),pages.end(),[&](const auto& page){return page.at("key")==command.at("key");});
                const bool known=read.value("language",std::string{})==command.at("language").get<std::string>()&&read.value("key",std::string{})==command.at("key").get<std::string>();
                if(!searched&&!known)return toolFailure("activity_reference_not_observed");
            }
            if(kind=="creative_continue"){
                const auto pieces=visibleContinuationDrafts(snapshots.value("atelier",Json::object()));
                const auto found=std::find_if(pieces.begin(),pieces.end(),[&](const auto& item){return item.at("id")==command.at("piece_id");});
                if(found==pieces.end()||found->at("stage")!="draft")return toolFailure("activity_piece_unavailable");
                if(found->at("revision")!=command.at("expected_revision"))return toolFailure("activity_piece_revision");
            }
            if(kind=="creative_read"){
                const auto pieces=snapshots.value("atelier",Json::object()).value("pieces",Json::array());
                if(std::none_of(pieces.begin(),pieces.end(),[&](const Json& piece){return piece.value("id",std::string{})==command.at("piece_id").get<std::string>();}))return toolFailure("activity_piece_unavailable");
            }
            if(kind=="puzzle_press"||kind=="puzzle_leave"){
                const auto game=snapshots.value("pocket",Json::object()).value("current",Json::object()).value("game",Json(nullptr));
                if(!game.is_object()||game.value("status",std::string{})!="active"||game.at("id")!=command.at("game_id"))return toolFailure("activity_game_changed");
                const auto revision=command.at("expected_revision").get<std::int64_t>(),current=game.at("revision").get<std::int64_t>();if(revision<current||revision>current+6)return toolFailure("activity_game_revision");
            }
            if(kind=="pose"){const auto poses=widget.value("animation_catalog",Json::array());if(std::none_of(poses.begin(),poses.end(),[&](const auto& pose){return pose.at("state")==command.at("state");}))return toolFailure("activity_pose_unavailable");if(command.at("state")=="breathing"&&life.snapshot().value("energy",1.0)>.4)return toolFailure("activity_not_fatigued");}
            if(kind=="interact"||kind=="arrange"){const auto objects=snapshots.value("habitat",Json::object()).value("objects",Json::array());if(std::none_of(objects.begin(),objects.end(),[&](const auto& item){return item.at("id")==command.at("object_id");}))return toolFailure("activity_target_unavailable");}
        }
        Json ids=Json::array();for(auto item:normalized){item["id"]=id("activity");ids.push_back(item.at("id"));item["expires_ms"]=serviceNowMs()+600000;item["source"]=source;item["narrative_sources"]=narrativeSources;mindActivity(item,"queued");plan["entries"].push_back(std::move(item));}if(save)save(ids,plan);else saveActivities(plan);
        return toolSuccess({{"accepted",normalized.size()},{"activity_ids",ids},{"completed",false},{"queued",plan.at("entries").size()}});
    }
    std::string activityKey()const{const auto& actor=profile(get("ha.settings","model").value.at("id"));const auto affect=life.emotionalContext();return Json::array({actor.id,actor.model.model,persona.at("version"),cfg.reviewEnabled,cfg.reviewProfileId,cfg.plugins,cfg.computer,cfg.cognition,pluginAttentionEpoch,activityGenerationEpoch,affect.at("dominant_emotion"),affect.at("dominant_desire"),affect.at("behavior").at("may_use_dry_irony"),idleEconomyBand()}).dump();}
    void activityEvent(const Json& event){
        std::lock_guard lock(activityMutex);if(!autonomousMode())return;const auto kind=event.value("kind",std::string{});
        if(kind=="habitat.appeared"||kind=="canteen.ready")planningStimulus(serviceNowMs(),false);
        if(kind=="widget.interacted"){cancelActivities("pointer_interaction");return;}
        const bool completed=kind=="habitat.consumed"||kind=="habitat.collected"||kind=="habitat.inspected"||kind=="habitat.arranged"||kind=="habitat.planted"||kind=="rhythm.rest_finished"||kind=="widget.move_finished";
        const bool interrupted=kind=="habitat.action_cancelled"||kind=="rhythm.rest_cancelled"||kind=="rhythm.rest_interrupted"||kind=="widget.move_cancelled";
        if(!completed&&!interrupted)return;auto plan=activityQueue();const auto& active=plan.at("active");if(active.is_null())return;
        const auto token=active.value("token",std::string{});if(token.empty()||event.value("token",std::string{})!=token)return;
        if(get("ha.activity-followups",active.at("id")).revision)plan["active"]["effect_result"]=event;finishActivity(plan,completed?"completed":"interrupted");saveActivities(plan);nextActivityTick=serviceNowMs()+500;
    }
    void prefetchActivities(std::int64_t now,const Json& snapshots,const Json& plan){
        // Drain already approved finite commands, without refilling behind them.
        // The rhythm plugin still waits for the actual empty activity queue.
        if(activityRoutineWaiting())return;
        if(continuousWorkPending())return;
        const auto last=get("ha.state","current").value.value("lastUserInteractionMs",std::int64_t(0));const double idle=cfg.plugins.value("widget",Json::object()).value("autonomyIdleSeconds",30.0);
        const auto actor=profile(get("ha.settings","model").value.at("id"));const bool deferred=callsFor(actor).deferPrefetch;
        if(activityWasDeferred&&!deferred)nextActivityFill=now;activityWasDeferred=deferred;
        if(activityGenerator.running()||now-last<idle*1000||reactionGenerator.running()||plan.at("entries").size()>1)return;
        // The disk queue has its own cooldown; a failed/quiet model planner's
        // backoff must never postpone local peak-time reuse.
        if(deferred){if(plan.at("active").is_null()&&plan.at("entries").empty())reuseLocalActivity(now,snapshots);return;}
        if(!idlePrefetchAllowed(now)){activityBudgetWait=discretionaryBudgetAvailable()?"system_idle":"return_budget_reserve";if(plan.at("active").is_null()&&plan.at("entries").empty())reuseLocalActivity(now,snapshots);return;}
        if(activityBudgetWait=="system_idle"||activityBudgetWait=="return_budget_reserve")activityBudgetWait.clear();
        if(now<nextActivityFill&&!activityBudgetWasBlocked)return;
        // A denied reservation is a wait condition, not a failed model choice.
        // Poll this inexpensive preflight at most once a second; the unified
        // reserveCall remains authoritative immediately before every real call.
        if(actor.model.protocol!=ayt::inference::Protocol::Mock){
            if(now>=nextActivityBudgetCheck){
                nextActivityBudgetCheck=now+1000;const auto daily=backgroundBudgetStatus(),hourly=modelBudgetStatus();
                const std::string reason=daily.at("blocked").get<bool>()?daily.at("blocked_reason").get<std::string>():hourly.at("remaining").get<int>()<=hourly.at("background_reserve").get<int>()?"hourly_reserve":"";
                const bool wasBlocked=activityBudgetWasBlocked;activityBudgetWasBlocked=!reason.empty();activityBudgetWait=reason;
                if(wasBlocked&&!activityBudgetWasBlocked)nextActivityFill=now;
            }
            if(activityBudgetWasBlocked){if(plan.at("active").is_null()&&plan.at("entries").empty())reuseLocalActivity(now,snapshots);return;}
        }else{activityBudgetWasBlocked=false;activityBudgetWait.clear();}
        if(now<nextActivityFill)return;std::optional<Profile> reviewer;if(cfg.reviewEnabled)reviewer=profile(cfg.reviewProfileId);
        const auto interval=static_cast<std::int64_t>(cfg.plugins.value("widget",Json::object()).value("autonomyRefillSeconds",60.0)*1000);
        nextActivityFill=now+interval;
        ProposalContext narrative;Json summaries=Json::array();const auto memories=memoryRows();const auto policy=visibility();for(auto it=memories.rbegin();it!=memories.rend()&&summaries.size()<4;++it){const auto memory=Json::parse(it->payload);if(!policy.memories.contains(it->id)&&policy.allows(memory)){summaries.push_back(libraryPage(memory,0,600));narrative.narrativeSources[it->id]={it->id,it->revision,memory.at("kind"),memoryScope};}}
        const auto self=personalPlanningContext();auto evidence=activityEvidence(plan);const auto economyMode=idleEconomyBand();if(economyMode>0){while(evidence.size()>3)evidence.erase(evidence.begin()+3);while(summaries.size()>1)summaries.erase(summaries.begin()+1);}authorizePersonalAttempts(self,narrative);captureActivityNarrativeSources(plan,narrative);
        for(const auto& ref:evidence)narrative.narrativeSources[ref.at("id")]=Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};
        auto environment=snapshots;environment.erase("widget");
        for(auto it=environment.begin();it!=environment.end();++it)it.value()=pluginCharacterContext(it.key(),it.value());
        Json time={{"now_ms",now}};time.update(presentationTimeContext(now));
        const auto state=modelLifeState(life.snapshot(),&narrative);
        const auto scheduleConfiguration=rememberedPlanningContext(narrative);
        const auto mindView=mindContext(&narrative);const auto previousReview=activityReviewFeedback(&narrative);
        const bool reusePrompt=ContextControl::read(callsFor(actor).context).backgroundProjection;
        const auto promptKey=Json::array({actor.id,actor.model.model,actor.model.origin,cfg.reviewEnabled,cfg.reviewProfileId,contentDigest(persona.dump()),cfg.plugins,cfg.computer,idleEconomyBand()}).dump();
        if(!reusePrompt||!background_projection::reusable(activityPromptHistory,promptKey,projectionFence(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);}))activityPromptHistory=Json::object();
        if(!activityPromptHistory.empty())for(const auto& ref:activityPromptHistory.at("sources")){const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};narrative.narrativeSources.try_emplace(source.scope+":"+source.id,source);}
        auto promptState=activityPromptHistory.value("state",Json::object());promptState["enabled"]=reusePrompt;promptState["source_fence"]=projectionFence();
        const auto promptAllowed=[this,fence=projectionFence(),refs=activityNarrativeSources(narrative)]{std::lock_guard guard(modelBudgetMutex);if(fence!=projectionFence())return false;const auto policy=visibility();return std::all_of(refs.begin(),refs.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);});};
        activityGenerator.start(actor,reviewer,persona,{{"_narrative_sources",activityNarrativeSources(narrative)},{"time_context",time},{"schedule_configuration",scheduleConfiguration},{"routine_configuration",{{"available",plugins&&plugins->enabled("rhythm")&&cfg.plugins.value("rhythm",Json::object()).value("circadianEnabled",true)},{"current",circadianView()}}},{"state",state},{"self",self},{"mind",mindView},{"evidence",evidence},{"previous_carry_forward_results",activityCarryFeedback()},{"previous_planning_error",activityError},{"previous_review_feedback",previousReview},{"personal_pursuit",personalPursuitAvailability()},{"screen_observation",screenGlanceAvailability()},{"body",bodyState()},{"surfaces",environment},{"available_commands",idle_economy::commands(activityCommands(snapshots,true),economyMode)},{"declared_commands",idle_economy::commands(activityCommands(snapshots,true,true),economyMode)},{"queue_slots",std::min<std::size_t>(economyMode>1?1:economyMode==1?2:6,6-plan.at("entries").size()-(plan.at("active").is_null()?0:1))},{"economy_mode",economyMode>1?"deep_idle":economyMode==1?"idle":"active"},{"memories",summaries},{"recent_activities",activityForModel(plan).at("recent")}},activityKey(),[this]{return consumeModelBudget(1,true);},[this](const Profile& p,int status){providerResult(p,status);},[this](const Profile& profile,const std::string& role,const ayt::inference::Usage& usage){recordModelUsage(profile.id,"activity"+(role=="review"?std::string("_review"):std::string{}),cacheUsageJson(usage));},std::move(promptState),promptAllowed,reviewPrefixEnabled(actor)?std::function<std::function<bool()>(const Profile&,ayt::inference::Request&,const Json&,Json&)>([this](const Profile& p,ayt::inference::Request& request,const Json& refs,Json& metadata){auto prepared=prepareReviewPrefix(p,request,refs,"activity");metadata=prepared.metadata;return std::function<bool()>([this,prepared=std::move(prepared)]{return reviewPrefixStillAllowed(prepared);});}):std::function<std::function<bool()>(const Profile&,ayt::inference::Request&,const Json&,Json&)>{});
    }
    void collectActivityEffect(std::int64_t now){
        // Finite local effects use one separate worker, keeping disk I/O out of
        // the desktop pump. Cancellation stops future work, not committed past.
        if(activityEffect.valid()&&activityEffect.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            Json receipt;try{receipt=activityEffect.get();}catch(...){receipt=toolFailure("activity_effect_failed");}
            ProposalContext evidence;evidence.eventKind="solitude";receipt=observedTool(activityEffectTool,std::move(receipt),evidence,activityEffectId,&activityEffectArguments);
            auto plan=activityQueue();const auto status=receipt.value("ok",false)?std::string("completed"):receipt.value("error",Json::object()).value("code",std::string("failed"));
            if(plan.at("active").is_object()&&plan.at("active").value("id",std::string{})==activityEffectId){plan["active"]["source_id"]=receipt.value("source_id",std::string{});if(get("ha.activity-followups",activityEffectId).revision)plan["active"]["effect_result"]=receipt;
                if(receipt.value("ok",false)&&activityEffectTool=="habitat.gather"&&!receipt.at("result").value("completed",false)){plan["active"]["gather_wait"]=true;plan["active"]["gather_id"]=receipt.at("result").at("gather_id");plan["active"]["deadline_ms"]=now+185000;}
                if(receipt.value("ok",false)&&(activityEffectTool=="canteen.cook"||activityEffectTool=="canteen.take")){const auto& result=receipt.at("result");plan["active"]["canteen_wait"]=true;plan["active"]["canteen_batch"]=activityEffectTool=="canteen.cook"?result.at("job").at("batch_id"):result.at("batch_id");if(result.contains("ticket"))plan["active"]["canteen_ticket"]=result.at("ticket");
                    if(activityEffectTool=="canteen.cook")plan["active"]["deadline_ms"]=now+static_cast<std::int64_t>(std::clamp(result.value("plan",Json::object()).value("seconds",120.0)*1000+30000,60000.0,1800000.0));
                    if(activityEffectTool=="canteen.cook")for(auto& next:plan["entries"])if(next.at("kind")=="take_meal"&&!next.contains("batch_id")&&(!next.contains("recipe_id")||next.at("recipe_id")==result.at("job").at("recipe_id"))){next["batch_id"]=result.at("job").at("batch_id");break;}}
                else if(!plan["active"].value("gather_wait",false))finishActivity(plan,status);}
            else for(auto& recent:plan["recent"])if(recent.value("id",std::string{})==activityEffectId){mindActivity(recent,status,true);if(status=="completed")mindObserved(recent);recent["effect_outcome"]=status;recent["effect_ended_ms"]=now;recent["source_id"]=receipt.value("source_id",std::string{});if(recent.contains("intention"))recent["attempt_source_id"]=recordIntentionAttempt(recent);}
            saveActivities(plan);activityEffectId.clear();activityEffectTool.clear();activityEffectArguments=Json::object();activityEffectCancelled.reset();nextActivityTick=now+500;
        }
    }
    void activitiesTick(std::int64_t now){
        inputIdleTick(now);std::lock_guard lock(activityMutex);const bool mode=autonomousMode();collectActivityEffect(now);personalPursuitTick(now);
        if(!activityModeNotified||mode!=activityLastMode){activityModeNotified=true;activityLastMode=mode;if(plugins)plugins->event({{"kind","runtime.autonomy"},{"enabled",mode}});}
        if(!activityInitialized){activityInitialized=true;cancelActivities("application_restarted");nextActivityFill=std::max(now+30000,restoredPlanningDue(get("ha.autonomy","cadence",emptyPlanningCadence()).value,now,activityPlanningBase()));}
        if(now<nextActivityMaintenance)return;nextActivityMaintenance=now+100;
        if(auto batch=activityGenerator.poll()){
            const bool budgetDeferred=batch->status=="model_call_budget";
            activityError=batch->status=="ok"||batch->status=="cancelled"||budgetDeferred?"":batch->status;const auto key=id("activity-generation");recordBatchUsage(batch->usage,"activity");
            if(budgetDeferred){activityBudgetWasBlocked=true;activityBudgetWait="model_budget";nextActivityBudgetCheck=0;}
            Json refs=Json::array();for(const auto& source:batch->evidence)refs.push_back({{"scope",source.at("scope")},{"id",source.at("id")},{"revision",source.at("revision")},{"kind",source.at("kind")}});
            commit({{"ha.activity-generations",key,0,1,Json{{"status",batch->status},{"proposal",batch->proposal},{"format_normalizations",batch->formatNormalizations},{"repair",batch->repair},{"review",batch->review},{"usage",batch->usage},{"timing_ms",{{"actor",batch->modelMs},{"review",batch->reviewMs}}},{"evidence_refs",refs},{"created_at",now}}.dump(),"autonomy"}},key);
            const bool current=mode&&!runtimePaused&&!activityRoutineWaiting()&&!inputActive&&!busy()&&(!batch->promptHistory.value("enabled",false)||batch->promptHistory.value("source_fence",Json(nullptr))==projectionFence())&&batch->key==activityKey()&&std::chrono::steady_clock::now()-batch->createdAt<std::chrono::minutes(2);
            if(current){
                if(batch->usage.value("actor_calls",0)>0)recordPersonalOffer(batch->offeredPersonal,now);
                saveActivityReviewFeedback(*batch,key,false);
                if(batch->status=="ok"){const auto receipt=enqueueActivities(batch->entries,key,batch->narrativeSources);if(!receipt.value("ok",false))activityError=receipt.at("error").at("code");
                    const auto results=receipt.value("ok",false)?applyActivityCarry(*batch,key):Json::array();
                    if(receipt.value("ok",false))saveActivityReviewFeedback(*batch,key,true);
                    if(receipt.value("ok",false)&&batch->promptHistory.value("enabled",false)){
                        const auto actor=profile(get("ha.settings","model").value.at("id"));const auto promptKey=Json::array({actor.id,actor.model.model,actor.model.origin,cfg.reviewEnabled,cfg.reviewProfileId,contentDigest(persona.dump()),cfg.plugins,cfg.computer,idleEconomyBand()}).dump();const auto policy=visibility();
                        if(std::all_of(batch->narrativeSources.begin(),batch->narrativeSources.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);}))activityPromptHistory=background_projection::bind(batch->promptHistory,promptKey,projectionFence(),batch->narrativeSources);else activityPromptHistory=Json::object();
                    }
                    if(receipt.value("ok",false)&&!runtimePaused&&!inputActive&&!busy()&&batch->key==activityKey()&&std::all_of(results.begin(),results.end(),[](const auto& result){return result.at("receipt").value("ok",false);}))markActivityEncounters(*batch,key);
                    auto archived=get("ha.activity-generations",key);auto detail=archived.value;detail["application_status"]=receipt.value("ok",false)?"accepted":"queue_rejected";detail["carry_forward_results"]=results;commit({mutation("ha.activity-generations",key,archived,detail,key)},key);
                    const bool privateChanged=std::any_of(results.begin(),results.end(),[](const auto& result){const auto& r=result.at("receipt");return r.value("ok",false)&&!r.value("result",Json::object()).value("deduplicated",false);});
                    planningResult(receipt.value("ok",false),batch->entries.empty()&&!privateChanged,now);}
                else if(batch->status!="cancelled"&&!budgetDeferred)planningResult(false,false,now);
            }
            if(batch->status!="ok"||!current){auto archived=get("ha.activity-generations",key);auto detail=archived.value;detail["application_status"]=!current?"discarded_stale":budgetDeferred?"deferred_budget":"model_rejected";detail["carry_forward_results"]=Json::array();commit({mutation("ha.activity-generations",key,archived,detail,key)},key);}
        }
        if(circadianAsleep()){
            if(!activityRestSuspended){activityRestSuspended=true;++activityGenerationEpoch;activityPromptHistory=Json::object();}
            activityGenerator.cancel();notifyEconomyFallback(false);return;
        }
        if(activityRestSuspended){activityRestSuspended=false;nextActivityFill=now;nextActivityTick=now;nextActivityBudgetCheck=0;planningStimulus(now,false);}
        pruneChangedIntentionActivities(now);
        if(!mode||runtimePaused){if(!activitySuspended)cancelActivities(mode?"paused":"mode_disabled");notifyEconomyFallback(false);activitySuspended=true;return;}
        if(activitySuspended){nextActivityFill=now;nextActivityTick=now;activitySuspended=false;}
        initializeActivityReuse();
        if(inputActive||busy()){activityGenerator.cancel();return;}if(activityEffect.valid())return;if(now<nextActivityTick)return;nextActivityTick=now+100;
        const auto snapshots=plugins->liveSnapshots();const auto widget=snapshots.value("widget",Json::object());
        if(!widget.value("visible",false)){cancelActivities("character_hidden");return;}
        if(widget.value("bubble_in_progress",false)||widget.value("waiting_speech_count",0)>0)return;
        auto plan=activityQueue();prefetchActivities(now,snapshots,plan);plan=activityQueue();
        const bool economy=callsFor(profile(get("ha.settings","model").value.at("id"))).deferPrefetch||localEconomyReuse();
        notifyEconomyFallback(economy&&!activityRoutineWaiting()&&plan.at("active").is_null()&&plan.at("entries").empty());
        if(!plan.at("active").is_null()){
            if(plan["active"].value("gather_wait",false)){
                const auto world=snapshots.value("habitat",Json::object()),last=world.value("last_gather",Json(nullptr)),current=world.value("gather",Json(nullptr));const auto gatherId=plan["active"].at("gather_id");
                if(last.is_object()&&last.at("id")==gatherId){plan["active"]["gather_result"]=last;finishActivity(plan,last.value("completed",false)?"completed":"partial_or_interrupted");saveActivities(plan);}
                else if(current.is_null()||now>=plan["active"].at("deadline_ms").get<std::int64_t>()){finishActivity(plan,"interrupted");saveActivities(plan);}return;
            }
            if(plan["active"].value("canteen_wait",false)){
                const auto kitchen=snapshots.value("canteen",Json::object()).value("current",Json::object());const std::string batch=plan["active"].at("canteen_batch");bool complete=false;
                if(plan["active"].at("kind")=="canteen_cook"){for(const auto* field:{"shelf","recent"})for(const auto& item:kitchen.value(field,Json::array()))if(item.at("id")==batch)complete=true;}
                else for(const auto& item:kitchen.value("recent",Json::array()))if(item.at("id")==batch&&item.value("ticket",std::string{})==plan["active"].value("canteen_ticket",std::string{}))complete=true;
                const bool waiting=plan["active"].at("kind")=="canteen_cook"?kitchen.value("job",Json(nullptr)).is_object():kitchen.value("delivery_pending",false);
                if(complete||!waiting||now>=plan["active"].at("deadline_ms").get<std::int64_t>()){finishActivity(plan,complete?"completed":"interrupted");saveActivities(plan);nextActivityTick=now+500;}return;
            }

            const auto active=plan.at("active");const auto kind=active.at("kind").get<std::string>();
            if(active.contains("intention")&&!currentIntention(active.at("intention"))){pruneChangedIntentionActivities(now);return;}
            if(kind=="pose"){
                const auto expected=active.at("state").get<std::string>();auto resolved=expected;for(const auto& pose:widget.value("animation_catalog",Json::array()))if(pose.at("state")==expected)resolved=pose.value("resolved",expected);
                const auto animation=widget.value("animation",Json::object());const bool shown=widget.value("state",std::string{})==expected&&(animation.value("backend",std::string{})=="procedural"||(animation.value("resolved",std::string{})==resolved&&animation.value("phase",std::string{})=="loop"));
                if(!active.contains("until_ms")&&shown){plan["active"]["until_ms"]=now+static_cast<std::int64_t>(active.at("seconds").get<double>()*1000);saveActivities(plan);return;}
                if(active.contains("until_ms")&&(now>=active.at("until_ms").get<std::int64_t>()||(!shown&&now+100>=active.at("until_ms").get<std::int64_t>()))){finishActivity(plan,"completed");saveActivities(plan);nextActivityTick=now+500;return;}
                if(widget.value("state",std::string{})!=expected&&widget.value("pending_pose",std::string{}).empty()){finishActivity(plan,"interrupted");saveActivities(plan);return;}
            }
            if(kind=="wait"&&now>=active.at("until_ms").get<std::int64_t>()){finishActivity(plan,"completed");saveActivities(plan);nextActivityTick=now+500;}
            else if(now>=active.at("deadline_ms").get<std::int64_t>()){finishActivity(plan,"timed_out");saveActivities(plan);plugins->event({{"kind","runtime.activity_cancel"},{"reason","timed_out"}});}return;
        }
        bool expired=false;for(auto it=plan["entries"].begin();it!=plan["entries"].end();)if(it->at("expires_ms").get<std::int64_t>()<=now){it=plan["entries"].erase(it);expired=true;}else ++it;if(expired)saveActivities(plan);
        if(plan.at("entries").empty())return;
        auto command=plan["entries"].front();plan["entries"].erase(0);
        if(command.contains("intention")&&!currentIntention(command.at("intention"))){plan["active"]=command;finishActivity(plan,"not_started_intention_changed");saveActivities(plan);return;}
        if(mindSettings().enabled){const auto key=mindObservationKey(command,snapshots);if(!key.empty())command["observation_key"]=key;}mindActivity(command,"running");
        command["from_state"]=widget.value("state",std::string("idle"));command["started_ms"]=now;command["deadline_ms"]=now+static_cast<std::int64_t>(command.at("seconds").get<double>()*1000)+125000;plan["active"]=command;saveActivities(plan);
        const auto kind=command.at("kind").get<std::string>();Json receipt;
        if(kind=="pursue_intention"){const auto result=schedulePersonalPursuit(command);plan["active"]["pursuit_receipt"]=result;finishActivity(plan,result.value("ok",false)?"scheduled":result.value("error",Json::object()).value("code",std::string("failed")));saveActivities(plan);return;}
        if(kind=="screen_glance"){const auto result=scheduleScreenGlance(command);plan["active"]["screen_observation_receipt"]=result;finishActivity(plan,result.value("ok",false)?"scheduled":result.value("error",Json::object()).value("code",std::string("failed")));saveActivities(plan);return;}
        if(kind=="gather"||kind=="gather_for_recipe"||kind=="store_supplies"||kind=="eat_inventory"||kind=="canteen_cook"||kind=="take_meal"||kind=="create_drop"||kind=="creative_note"||kind=="creative_continue"||kind=="creative_read"||kind=="reference_search"||kind=="reference_read"||kind=="puzzle_start"||kind=="puzzle_press"||kind=="puzzle_leave"){
            activityEffectId=command.at("id");activityEffectCancelled=std::make_shared<std::atomic<bool>>(false);const auto cancel=activityEffectCancelled;
            Json args;
            if(kind=="gather"||kind=="gather_for_recipe"){activityEffectTool="habitat.gather";args=Json::object();for(const auto* field:{"region","ingredient_id","count","recipe_id","destination"})if(command.contains(field))args[field]=command.at(field);if(kind=="gather_for_recipe"&&!args.contains("destination"))args["destination"]="pantry";}
            else if(kind=="store_supplies"){activityEffectTool="canteen.store";args=Json::object();}
            else if(kind=="eat_inventory"){activityEffectTool="canteen.eat_supply";args={{"item_id",command.at("item_id")}};}
            else if(kind=="canteen_cook"){activityEffectTool="canteen.cook";args={{"recipe_id",command.at("recipe_id")}};}
            else if(kind=="take_meal"){activityEffectTool="canteen.take";std::string target=command.value("batch_id",std::string{});const auto shelf=snapshots.value("canteen",Json::object()).value("current",Json::object()).value("shelf",Json::array());if(target.empty())for(auto it=shelf.rbegin();it!=shelf.rend();++it)if(it->at("status")=="ready"&&(!command.contains("recipe_id")||it->at("recipe_id")==command.at("recipe_id"))){target=it->at("id");break;}args={{"batch_id",target.empty()?"no-ready-meal":target}};}
            else if(kind=="create_drop"){activityEffectTool="habitat.create";args={{"kind",command.at("template")},{"definition",{{"name",command.at("name")},{"description",command.at("description")},{"tint",command.at("tint")}}}};}
            else if(kind=="puzzle_start"){activityEffectTool="pocket.start";args=Json::object();}
            else if(kind=="puzzle_press"){activityEffectTool="pocket.press";args={{"game_id",command.at("game_id")},{"expected_revision",command.at("expected_revision")},{"cell",command.at("cell")}};}
            else if(kind=="puzzle_leave"){activityEffectTool="pocket.leave";args={{"game_id",command.at("game_id")},{"expected_revision",command.at("expected_revision")}};}
            else if(kind=="creative_continue"){activityEffectTool="atelier.append";args={{"id",command.at("piece_id")},{"expected_revision",command.at("expected_revision")},{"content",command.at("content")},{"stage",command.at("stage")}};}
            else if(kind=="creative_read"){activityEffectTool="atelier.read";args={{"id",command.at("piece_id")}};}
            else if(kind=="reference_search"){activityEffectTool="reading.search";args={{"query",command.at("query")},{"language",command.at("language")}};}
            else if(kind=="reference_read"){activityEffectTool="reading.open";args={{"key",command.at("key")},{"language",command.at("language")}};for(const auto* field:{"detail","offset","length"})if(command.contains(field))args[field]=command.at(field);}
            else {activityEffectTool="atelier.write";args={{"id",id("piece")},{"expected_revision",0},{"title",command.at("title")},{"kind",command.at("piece_kind")},{"stage","draft"},{"content",command.at("content")}};}
            activityEffectArguments=args;const auto tool=activityEffectTool;auto* runtime=plugins.get();activityEffect=std::async(std::launch::async,[runtime,tool,args=std::move(args),cancel]{return runtime->invoke(tool,args,[cancel]{return cancel->load();});});return;
        }
        if(kind=="wait"){plan["active"]["until_ms"]=now+static_cast<std::int64_t>(command.at("seconds").get<double>()*1000);saveActivities(plan);return;}
        if(kind=="move_to")receipt=plugins->invoke("widget.move",{{"x",command.at("x")},{"y",command.at("y")}},{});
        else if(kind=="pose"){receipt=plugins->invoke("widget.animate",{{"state",command.at("state")},{"seconds",command.at("seconds")}},{});}
        else if(kind=="rest")receipt=plugins->invoke("rhythm.rest",{{"seconds",command.at("seconds")}},{});
        else if(kind=="arrange")receipt=plugins->invoke("habitat.arrange",{{"id",command.at("object_id")},{"x",command.at("x")},{"y",command.at("y")}},{});
        else if(kind=="interact")receipt=plugins->invoke("habitat.act",{{"id",command.at("object_id")},{"action",command.at("action")}},{});
        else {const auto world=snapshots.value("habitat",Json::object());std::string target;double best=1e10;
            for(const auto& item:world.value("objects",Json::array())){if(kind=="eat_nearest"&&!drops::edible(item.value("kind",std::string{})))continue;const double distance=std::hypot(item.at("x").get<double>()-widget.value("x",0.0),item.at("y").get<double>()-widget.value("y",0.0));if(distance<best){best=distance;target=item.at("id");}}
            if(target.empty())receipt=toolFailure("no_matching_object");else{receipt=plugins->invoke("habitat.act",{{"id",target},{"action",kind=="eat_nearest"?"eat":"look"}},{});if(receipt.value("ok",false)){const auto pending=plugins->liveSnapshots().value("habitat",Json::object()).value("pending",Json(nullptr));if(pending.is_object())receipt["result"]["token"]=pending.at("token");}}
        }
        if((kind=="interact"||kind=="arrange")&&receipt.value("ok",false)){const auto pending=plugins->liveSnapshots().value("habitat",Json::object()).value("pending",Json(nullptr));if(pending.is_object())receipt["result"]["token"]=pending.at("token");}
        if(receipt.value("ok",false)&&receipt.at("result").contains("token"))plan["active"]["token"]=receipt.at("result").at("token");else if(kind!="pose"||!receipt.value("ok",false))finishActivity(plan,receipt.value("ok",false)?"unconfirmed":receipt.at("error").value("code",std::string("failed")));
        saveActivities(plan);nextActivityTick=now+100;
    }
