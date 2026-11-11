    std::atomic<int> inputIdleBand{0};std::atomic<std::int64_t> inputIdleSeconds{-1};std::int64_t nextInputIdlePoll=0;
    bool idleEconomyEnabled()const{return background::Policy::read(cfg.backgroundBudget).autoEconomy;}
    int idleEconomyBand()const{return idleEconomyEnabled()?inputIdleBand.load():0;}
    bool discretionaryBudgetAvailable()const{return !idleEconomyEnabled()||idle_economy::reserveAvailable(backgroundBudgetStatus(),inputIdleSeconds.load()<0?2:idleEconomyBand());}
    bool localEconomyReuse()const{return idleEconomyEnabled()&&(idleEconomyBand()>0||!discretionaryBudgetAvailable());}
    void inputIdleTick(std::int64_t now){
        if(now<nextInputIdlePoll)return;nextInputIdlePoll=now+1000;if(!idleEconomyEnabled()||!desktop){inputIdleSeconds=-1;inputIdleBand=0;return;}
        const auto idle=desktop->inputIdleSeconds();const auto seconds=idle?static_cast<std::int64_t>(*idle):-1;const auto previous=inputIdleBand.exchange(idle_economy::band(seconds));inputIdleSeconds=seconds;
        if(previous>0&&inputIdleBand.load()==0){nextActivityFill=now+2000;nextReactionFill=now+2000;}
    }
    bool idlePrefetchAllowed(std::int64_t now,bool reaction=false)const{
        if(!idleEconomyEnabled())return true;if(!discretionaryBudgetAvailable())return false;const auto mode=idleEconomyBand();if(reaction&&mode>0)return false;
        const auto cadence=get("ha.autonomy","cadence",emptyPlanningCadence()).value;return idle_economy::due(mode,validPlanningCadence(cadence)?cadence.value("last_ms",std::int64_t(0)):0,now);
    }
    bool idleSolitudeAllowed(std::int64_t now){
        if(!idleEconomyEnabled())return true;if(!discretionaryBudgetAvailable())return false;const auto mode=idleEconomyBand();const auto before=get("ha.budget","idle-solitude",{{"last_ms",0}});
        if(!idle_economy::due(mode,before.value.at("last_ms"),now))return false;
        commit({mutation("ha.budget","idle-solitude",before,{{"last_ms",now}},"idle-economy")},"idle-economy");return true;
    }
    Json idleEconomyStatus()const{
        const auto enabled=idleEconomyEnabled(),available=discretionaryBudgetAvailable();const auto mode=idleEconomyBand();const auto idle=inputIdleSeconds.load();
        return {{"enabled",enabled},{"source","windows_session_last_input"},{"input_idle_seconds",idle<0?Json(nullptr):Json(idle)},{"mode",!enabled?"disabled":idle<0?"unknown":mode==2?"deep_idle":mode==1?"idle":"active"},{"minimum_planning_interval_ms",idle_economy::interval(mode)},{"optional_model_work_allowed",available},{"local_activity_reuse",enabled&&(mode>0||!available)},{"return_reserve",enabled},{"reads_input_content",false}};
    }
