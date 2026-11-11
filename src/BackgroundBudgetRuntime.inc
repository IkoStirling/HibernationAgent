    mutable std::mutex backgroundBudgetMutex;
    void upgradeBackgroundBudget(){
        const auto now=serviceNowMs();const auto before=get("ha.budget","background");if(!before.revision||before.value.value("version",0)!=1)return;
        auto value=background::normalize(before.value,now);bool recovered=false;Json breakdown=Json::object();std::int64_t start=(value.at("day_index").get<std::int64_t>())*background::dayMs-background::offsetMs;
        // Read only bounded accounting histories. No dialogue or model payloads.
        try{
            RecordQuery q;q.scope="ha.budget";q.id="background";q.history=true;q.limit=4096;q.maxPayloadBytes=8*1024*1024;
            auto history=store.query({{"ha.budget"}},q);if(history){std::sort(history.records.begin(),history.records.end(),[](const auto& a,const auto& b){return a.revision<b.revision;});std::optional<Json> previous;
                for(const auto& row:history.records){const auto state=Json::parse(row.payload);if(state.at("day_index")!=value.at("day_index"))continue;
                    if((state.at("calls")==0&&state.at("input_tokens")==0)||(previous&&(state.at("calls")<previous->at("calls")||state.at("input_tokens")<previous->at("input_tokens"))))start=row.occurredAtMs;
                    previous=state;
                }
            }
            q.scope="ha.model-cache";q.id="usage";q.maxPayloadBytes=64*1024*1024;
            auto usage=store.query({{"ha.model-cache"}},q);if(usage&&usage.records.size()<4096){Json baseline={{"connections",Json::object()}},current=baseline;std::int64_t baselineAt=-1,currentAt=-1;
                for(const auto& row:usage.records){if(row.occurredAtMs<start&&row.occurredAtMs>=baselineAt){baseline=Json::parse(row.payload);baselineAt=row.occurredAtMs;}if(row.occurredAtMs>=currentAt){current=Json::parse(row.payload);currentAt=row.occurredAtMs;}}
                recovered=background::recoverLegacyCacheSplit(value,baseline,current);
                if(recovered){
                    const auto grouped=[](const Json& snapshot){Json out=Json::object();for(const auto& c:snapshot.value("connections",Json::object())){const auto groups=c.value("purposes",Json::object());for(auto it=groups.begin();it!=groups.end();++it)if(background::purpose(it.key())){auto& group=out[it.key()];if(!group.is_object())group=Json::object();for(const auto* field:{"calls","input_tokens","cached_input_tokens","uncached_input_tokens"})group[field]=background::sum(group.value(field,std::uint64_t(0)),it.value().value(field,std::uint64_t(0)));}}return out;};
                    const auto old=grouped(baseline),latest=grouped(current);for(auto it=latest.begin();it!=latest.end();++it)for(const auto* field:{"calls","input_tokens","cached_input_tokens","uncached_input_tokens"}){const auto n=it.value().at(field).get<std::uint64_t>(),p=old.value(it.key(),Json::object()).value(field,std::uint64_t(0));breakdown[it.key()][field]=n>=p?n-p:0;}
                }
            }
        }catch(...){/* Unknown history stays conservatively charged, never free. */}
        const auto receipt="uncached-budget-v2-"+std::to_string(before.revision);
        const Json note={{"window_start_ms",start},{"previous_total_input_tokens",before.value.at("input_tokens")},{"charged_uncached_input_tokens",value.at("input_tokens")},{"cached_input_tokens",value.at("cached_input_tokens")},{"cache_split_recovered",recovered},{"calls_preserved",value.at("calls")==before.value.at("calls")},{"usage_by_purpose_snapshot",breakdown},{"created_at",now}};value["accounting_migration"]=note;
        commit({mutation("ha.budget","background",before,value,receipt),{"ha.maintenance",receipt,0,1,note.dump(),receipt}},receipt);
    }
    Json backgroundBudgetStatus()const{
        std::lock_guard guard(backgroundBudgetMutex);const auto now=serviceNowMs();const auto state=background::normalize(get("ha.budget","background",background::fresh(now)).value,now);return background::status(background::Policy::read(cfg.backgroundBudget),state,now);
    }
    bool reserveBackgroundAndHourly(const ayt::storage::RecordMutation& hourly,std::int64_t now,int amount){
        std::lock_guard guard(backgroundBudgetMutex);const auto before=get("ha.budget","background",background::fresh(now));auto state=background::normalize(before.value,now);const auto policy=background::Policy::read(cfg.backgroundBudget);
        if(!background::canReserve(policy,state,static_cast<std::uint64_t>(amount)))return false;
        state=background::reserve(std::move(state),static_cast<std::uint64_t>(amount));commit({hourly,mutation("ha.budget","background",before,state,"background-call-budget")},"background-call-budget");return true;
    }
    void observeBackgroundUsage(const std::string& purpose,const Json& usage){
        if(!background::purpose(purpose))return;
        std::lock_guard guard(backgroundBudgetMutex);const auto now=serviceNowMs();const auto before=get("ha.budget","background",background::fresh(now));const auto state=background::observe(background::normalize(before.value,now),usage);
        if(state!=before.value)commit({mutation("ha.budget","background",before,state,"background-usage")},"background-usage");
    }
    Json backgroundBudgetFailure()const{
        const auto budget=backgroundBudgetStatus();if(!budget.at("blocked").get<bool>())return Json::object();
        const bool tokens=budget.at("blocked_reason")=="daily_input_tokens";return {{"code",tokens?"background_daily_input_budget":"background_daily_call_budget"},{"message",tokens?"后台活动已达到今日未命中缓存输入预算，等待次日额度；用户交谈仍可使用每小时额度。":"后台活动已达到今日调用上限，等待次日额度；用户交谈仍可使用每小时额度。"},{"http_status",0},{"budget",budget}};
    }
    Json runtimeStatus()const{
        const auto background=backgroundBudgetStatus();const auto hourly=modelBudgetStatus();const bool hourlyAvailable=hourly.at("remaining").get<int>()>hourly.at("background_reserve").get<int>();const auto plan=activityQueue();const auto queued=plan.at("entries").size();const bool active=!plan.at("active").is_null();const bool sleeping=circadianAsleep(),routineWaiting=activityRoutineWaiting();
        std::string speech="idle";for(const auto& [id,value]:speechPresentation){const auto state=value.value("state",std::string{});if(state=="speaking"){speech="speaking";break;}if(state=="waiting")speech="waiting";}
        Json result={{"code","idle"},{"label","等待输入或活动"},{"detail","当前没有正在执行的交谈；可等待输入或下一次活动检查。"},{"background_allowed",!routineWaiting&&!runtimePaused&&!runtimeOffline&&!workshopBusy()&&hourlyAvailable&&!background.at("blocked").get<bool>()},{"activity_queued",queued},{"activity_active",active},{"planning",activityGenerator.running()},{"speech_state",speech},{"last_error",lastChatFailure()},{"next_planning_ms",nextActivityFill},{"last_turn_silent",false}};
        result["background_defer_reason"]=sleeping?Json("circadian_sleep"):routineWaiting?Json("circadian_window"):background.at("blocked").get<bool>()?background.at("blocked_reason"):!hourlyAvailable?Json("hourly_reserve"):Json("");
        if(result.at("last_error").is_null()){const auto code=!activityError.empty()?activityError:reactionError;if(!code.empty())result["last_error"]={{"code",code},{"message","最近一次后台准备未完成；可以等待重试或检查设置。"},{"source",!activityError.empty()?"activity":"reaction"}};}
        const auto set=[&](const char* code,const char* label,const char* detail){result["code"]=code;result["label"]=label;result["detail"]=detail;};
        for(const auto& job:jobs())if(job.spec.id=="chat"&&job.state==JobState::Succeeded&&!job.result.empty()){const auto out=Json::parse(job.result,nullptr,false);if(out.is_object()&&out.contains("trace_id")){const auto trace=get("ha.trace",out.at("trace_id")).value;result["last_turn_silent"]=trace.value("silent",false)&&trace.value("model_called",true);}}
        if(portableBusy())set("storage_busy","正在验证或恢复存档","跨机密钥或存档操作正在执行；当前保持暂停和离线。");
        else if(workshopBusy())set("storage_busy","正在处理存档","当前存档步骤正在执行；完成当前安全步骤后可以暂停。");
        else if(runtimeOffline)set("offline","已离线","模型交谈和后台调用暂停；联网恢复策略以管理设置为准。");
        else if(runtimePaused)set("manual_pause","后台已暂停","后台任务与插件活动暂停；恢复不会重放已经取消的动作。");
        else if(sleeping)set("circadian_sleep","正在按作息休息","未唤醒的消息仍会保存，后台规划及到期工作暂缓；管理员可直接唤醒。");
        else if(speech=="speaking")set("speaking","正在朗读","文字呈现正在跟随实际语音播放。");
        else if(speech=="waiting")set("speech_wait","等待语音准备","回复正在等待语音就绪；这与模型继续生成是不同阶段。");
        else if(activityGenerator.running())set("preparing_activity","正在准备活动","后台正在选择下一批活动；尚未完成的计划不会作为经历。");
        else if(reactionGenerator.running())set("preparing_reaction","正在准备互动反应","后台正在补充短反应候选；不会因此主动发言。");
        else if(busy())set("model_busy","正在处理交谈或操作","当前有交谈、审核或工具操作正在处理；内部资料不在这里展示。");
        else if(active)set("activity_running","正在执行活动","已有一个本地活动正在执行，实际结果以完成回执为准。");
        else if(queued)set("queued_activity","活动已排队","活动将在交谈、语音与当前动作让出时机后执行。");
        else if(routineWaiting)set("circadian_window","准备按作息休息","停止补充新活动，等待已安排活动结束及实际睡姿确认；尚未确认的时间不计作睡眠。");
        else if(background.at("blocked").get<bool>())set("background_budget","后台等待每日额度","今日后台预算已用完，前台交谈仍受原有每小时额度管理。");
        else if(!hourlyAvailable)set("background_budget","后台等待每小时额度","后台暂缓调用，为前台交谈保留每小时剩余额度，现有本地活动仍可继续。");
        else if(!result.at("last_error").is_null())set("error","最近调用未完成","可查看管理诊断中的失败原因；不把调用失败当成角色主动沉默。");
        if(result.at("code")=="idle"&&result.at("last_turn_silent")==true)set("quiet","最近交谈选择沉默","最近完成的交谈明确返回沉默；当前没有执行中的操作。这不是对角色此刻意图的推测。");
        return result;
    }
