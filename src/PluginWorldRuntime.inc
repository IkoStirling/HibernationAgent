    bool pluginAttentionInitialized=false,pluginAttentionBusy=false,pluginAttentionInput=false;
    std::int64_t nextPluginAttention=0,nextPluginCharacter=0;
    std::uint64_t pluginAttentionEpoch=0,lastPluginAttentionEpoch=0;
    bool worldJournalInitialized=false;std::vector<std::string> worldJournalPending;
    std::int64_t nextWorldJournal=0;
    static bool rhythmLifecycle(const Json& event){const auto kind=event.value("kind",std::string{});return event.value("plugin",std::string{})=="rhythm"&&(kind=="rhythm.rest_started"||kind=="rhythm.rest_finished"||kind=="rhythm.rest_interrupted"||kind=="rhythm.rest_cancelled");}
    static bool worldExperienceKind(const Json& event){const auto kind=event.value("kind",std::string{});return (event.value("plugin",std::string{})=="reading"&&kind=="reading.observed")||(event.value("plugin",std::string{})=="pocket"&&(kind=="pocket.solved"||kind=="pocket.left"))||(event.value("plugin",std::string{})=="atelier"&&(kind=="atelier.created"||kind=="atelier.revised"))||(event.value("plugin",std::string{})=="widget"&&kind=="widget.move_finished")||(event.value("plugin",std::string{})=="habitat"&&(kind=="habitat.consumed"||kind=="habitat.inspected"||kind=="habitat.arranged"||(kind=="habitat.action_cancelled"&&event.value("partial_change",false))||kind=="habitat.planted"||(kind=="habitat.appeared"&&event.value("item",Json::object()).value("creator",std::string{})=="model")))||(rhythmLifecycle(event)&&(kind=="rhythm.rest_started"||kind=="rhythm.rest_finished"||(kind=="rhythm.rest_interrupted"&&event.value("elapsed_seconds",0.0)>=1)));}
    static bool worldStateChangeKind(const Json& event){const auto kind=event.value("kind",std::string{});return worldExperienceKind(event)||rhythmLifecycle(event)||(event.value("plugin",std::string{})=="pocket"&&(kind=="pocket.started"||kind=="pocket.changed"))||(event.value("plugin",std::string{})=="habitat"&&(kind=="habitat.appeared"||kind=="habitat.disappeared"));}
    void journalWorldEvents(std::int64_t now){
        if(now<nextWorldJournal)return;
        if(!worldJournalInitialized&&worldJournalPending.empty()){RecordQuery q;q.scope="ha.plugin-events";q.contains="\"journaled\":false";q.limit=256;const auto rows=store.query({{"ha.plugin-events"}},q);if(!rows)throw std::runtime_error("world_outbox_read");for(const auto& row:rows.records)worldJournalPending.push_back(row.id);worldJournalInitialized=rows.records.size()<q.limit;}
        for(int i=0;i<4&&!worldJournalPending.empty();++i){const auto key=worldJournalPending.front();auto raw=get("ha.plugin-events",key);if(!raw.revision||raw.value.value("journaled",true)){worldJournalPending.erase(worldJournalPending.begin());continue;}
            const auto kind=raw.value.at("kind").get<std::string>();if(!fact("world:"+key,"world."+kind,{{"kind",kind},{"action_id",key}},raw.value.at("created_at"))){nextWorldJournal=now+1000;return;}
            auto value=raw.value;value["journaled"]=true;commit({mutation("ha.plugin-events",key,raw,value,key)},key);worldJournalPending.erase(worldJournalPending.begin());
        }
    }
    void notifyPluginAttention(std::int64_t now){
        if(now>=nextPluginCharacter){nextPluginCharacter=now+1000;const auto current=life.snapshot();plugins->event({{"kind","runtime.character"},{"energy",current.at("energy")},{"activity",current.at("activity")}});}
        if(now<nextPluginAttention&&pluginAttentionEpoch==lastPluginAttentionEpoch)return;nextPluginAttention=now+100;bool active=!journalHealthy;
        for(const auto& job:jobs())if(job.spec.kind!="operator.tool"){
            const bool immediate=job.spec.id=="chat"||job.spec.id=="event"||job.spec.id=="solitude.once";
            const auto receipt="job:"+job.spec.id+":"+std::to_string(job.runToken);
            if(job.state==JobState::Running||(job.state==JobState::Pending&&(immediate||job.spec.dueAtMs<=now))||(!job.result.empty()&&!projected.contains(receipt)&&job.state!=JobState::Pending))active=true;
        }
        for(const auto& [id,playback]:speechPresentation)if(playback.value("state",std::string{})=="waiting"||playback.value("state",std::string{})=="speaking")active=true;
        if(pluginAttentionInitialized&&active==pluginAttentionBusy&&inputActive==pluginAttentionInput&&pluginAttentionEpoch==lastPluginAttentionEpoch)return;
        pluginAttentionInitialized=true;pluginAttentionBusy=active;pluginAttentionInput=inputActive;
        lastPluginAttentionEpoch=pluginAttentionEpoch;plugins->event({{"kind","runtime.attention"},{"busy",active},{"input_active",inputActive},{"epoch",pluginAttentionEpoch}});
    }
    void appendWorldExperience(std::vector<RecordMutation>& changes,const Json& event,const std::string& source,std::int64_t now){
        if(event.value("plugin",std::string{})=="reading"){
            const auto reference=event.value("reference",Json::object());const auto title=reference.value("title",std::string{});if(title.empty()||event.value("kind",std::string{})!="reading.observed")return;
            const auto scope=reference.value("scope",std::string{});if(scope!="lead_excerpt"&&scope!="article_source_chunk")return;
            appendObservedExperience(changes,event,source,"我看到了《"+excerpt(title,240)+"》"+(scope=="article_source_chunk"?"正文源文本":"导言")+"中的一小段内容。",now);return;
        }
        if(event.value("plugin",std::string{})=="atelier"){
            const auto title=event.value("piece",Json::object()).value("title",std::string{});if(title.empty())return;
            appendObservedExperience(changes,event,source,"我"+std::string(event.value("kind",std::string{})=="atelier.created"?"留下":"续写")+"了一段题为《"+excerpt(title,240)+"》的创作。",now);return;
        }
        if(event.value("plugin",std::string{})=="habitat"&&event.value("kind",std::string{})=="habitat.appeared"){
            const auto item=event.value("item",Json::object());if(item.value("creator",std::string{})!="model")return;
            const auto name=item.value("definition",Json::object()).value("name",std::string("小物"));appendObservedExperience(changes,event,source,"我在桌面上放下了一件名为「"+excerpt(name,120)+"」的小物。",now);return;
        }
        if(event.value("plugin",std::string{})=="pocket"){
            const auto kind=event.value("kind",std::string{});if(kind!="pocket.solved"&&kind!="pocket.left")return;
            const auto game=event.value("game",Json::object());const int moves=game.value("moves",-1);if(moves<0||moves>64)return;
            const auto text=kind=="pocket.solved"?"我解开了一局九盏灯小谜题，用了"+std::to_string(moves)+"步。":game.value("status",std::string{})=="exhausted"?"我试到这局九盏灯的步数上限，还没有全部熄灭，先放下了。":"我把还没有解开的这局九盏灯先放下了。";
            appendObservedExperience(changes,event,source,text,now);return;
        }
        if(event.value("plugin",std::string{})=="widget"&&event.value("kind",std::string{})=="widget.move_finished"){appendObservedExperience(changes,event,source,"我走到了桌面上的另一个位置。",now);return;}
        if(rhythmLifecycle(event)){
            if(!worldExperienceKind(event))return;const auto kind=event.at("kind").get<std::string>();const double elapsed=event.value("elapsed_seconds",0.0);if(!std::isfinite(elapsed)||elapsed<0||elapsed>1800)return;
            const auto text=kind=="rhythm.rest_started"?std::string("角色在桌面上开始了一段安静休息。"):"角色结束了这一段安静休息，实际经过约"+std::to_string(static_cast<int>(std::round(elapsed)))+"秒。";
            appendObservedExperience(changes,event,source,text,now);return;
        }
        const auto kind=event.value("kind",std::string{});if(event.value("plugin",std::string{})!="habitat"||(kind!="habitat.consumed"&&kind!="habitat.inspected"&&kind!="habitat.arranged"&&!(kind=="habitat.action_cancelled"&&event.value("partial_change",false))&&kind!="habitat.planted"))return;
        const auto item=event.value("item",Json::object());if(!item.is_object()||!item.contains("kind")||!item.at("kind").is_string())return;
        const auto itemKind=item.at("kind").get<std::string>();if(!drops::known(itemKind))return;
        auto objectName="虚拟小物「"+item.value("definition",Json::object()).value("name",drops::name(itemKind))+"」";if(item.at("kind")=="sprout"){const int growth=item.value("growth",0);if(growth>=3)objectName="一株已经开花的虚拟植物";else if(growth==2)objectName="一株带着花苞的虚拟植物";else if(growth==1)objectName="一株展开叶子的虚拟植物";}
        const auto text=kind=="habitat.consumed"?"角色在桌面上走近并吃掉了"+objectName+"。":kind=="habitat.planted"?"角色在桌面上种下了一颗虚拟种子，留下了一株芽苗。":kind=="habitat.arranged"?"角色在桌面上把"+objectName+"挪到了新的位置。":kind=="habitat.action_cancelled"?"角色挪动"+objectName+"的过程被打断，物件停在了中途的位置。":"角色在桌面上走近并观察了"+objectName+"。";
        appendObservedExperience(changes,event,source,text,now);
    }
    void appendObservedExperience(std::vector<RecordMutation>& changes,const Json& event,const std::string& source,const std::string& text,std::int64_t now){
        const auto key=id("experience");auto recent=get("ha.world","recent",Json::array());pushIndex(recent.value,key,8);
        auto factual=event;factual.erase("journaled");factual.erase("wake_model");Json observation={{"kind","outer_event"},{"text",text},{"plugin_event_id",source},{"observation",factual},{"created_at",now}};
        changes.push_back({archiveScope,key,0,1,observation.dump(),source});changes.push_back(mutation("ha.world","recent",recent,recent.value,source));
    }
    void applyRhythmState(const Json& event,const std::string& source,std::int64_t now){
        if(!rhythmLifecycle(event))return;const auto kind=event.at("kind").get<std::string>();std::string issue;
        if(kind=="rhythm.rest_started"){if(life.propose("resting",std::nullopt,source,now,issue))saveLife();return;}
        if(kind=="rhythm.rest_finished"||kind=="rhythm.rest_interrupted"){
            const auto current=life.snapshot();const auto evidence=current.value("evidence",std::string{});if(evidence.empty()||current.at("desired_activity")!="resting")return;
            const auto prior=get("ha.plugin-events",evidence);if(prior.revision&&prior.value.value("plugin",std::string{})=="rhythm"&&prior.value.value("token",std::string{})==event.value("token",std::string{}))if(life.propose("idle",std::nullopt,source,now,issue))saveLife();
        }
    }
