    bool pluginAttentionInitialized=false,pluginAttentionBusy=false,pluginAttentionInput=false;
    std::int64_t nextPluginAttention=0;
    std::uint64_t pluginAttentionEpoch=0,lastPluginAttentionEpoch=0;
    bool worldJournalInitialized=false;std::vector<std::string> worldJournalPending;
    std::int64_t nextWorldJournal=0;
    static bool worldExperienceKind(const Json& event){const auto kind=event.value("kind",std::string{});return event.value("plugin",std::string{})=="habitat"&&(kind=="habitat.consumed"||kind=="habitat.inspected"||kind=="habitat.planted");}
    void journalWorldEvents(std::int64_t now){
        if(now<nextWorldJournal)return;
        if(!worldJournalInitialized&&worldJournalPending.empty()){RecordQuery q;q.scope="ha.plugin-events";q.contains="\"journaled\":false";q.limit=256;const auto rows=store.query({{"ha.plugin-events"}},q);if(!rows)throw std::runtime_error("world_outbox_read");for(const auto& row:rows.records)worldJournalPending.push_back(row.id);worldJournalInitialized=rows.records.size()<q.limit;}
        for(int i=0;i<4&&!worldJournalPending.empty();++i){const auto key=worldJournalPending.front();auto raw=get("ha.plugin-events",key);if(!raw.revision||raw.value.value("journaled",true)){worldJournalPending.erase(worldJournalPending.begin());continue;}
            const auto kind=raw.value.at("kind").get<std::string>();if(!fact("world:"+key,"world."+kind,{{"kind",kind},{"action_id",key}},raw.value.at("created_at"))){nextWorldJournal=now+1000;return;}
            auto value=raw.value;value["journaled"]=true;commit({mutation("ha.plugin-events",key,raw,value,key)},key);worldJournalPending.erase(worldJournalPending.begin());
        }
    }
    void notifyPluginAttention(std::int64_t now){
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
        const auto kind=event.value("kind",std::string{});if(event.value("plugin",std::string{})!="habitat"||(kind!="habitat.consumed"&&kind!="habitat.inspected"&&kind!="habitat.planted"))return;
        const auto item=event.value("item",Json::object());if(!item.is_object()||!item.contains("kind")||!item.at("kind").is_string())return;
        const std::map<std::string,std::string> names={{"berry","一颗虚拟浆果"},{"leaf","一片虚拟叶子"},{"seed","一颗虚拟种子"},{"paper_star","一颗纸星"},{"sprout","一株芽苗"}};
        const auto found=names.find(item.at("kind").get<std::string>());if(found==names.end())return;
        const auto text=kind=="habitat.consumed"?"角色在桌面上走近并吃掉了"+found->second+"。":kind=="habitat.planted"?"角色在桌面上种下了一颗虚拟种子，留下了一株芽苗。":"角色在桌面上走近并观察了"+found->second+"。";
        const auto key=id("experience");auto recent=get("ha.world","recent",Json::array());pushIndex(recent.value,key,8);
        auto factual=event;factual.erase("journaled");factual.erase("wake_model");Json observation={{"kind","outer_event"},{"text",text},{"plugin_event_id",source},{"observation",factual},{"created_at",now}};
        changes.push_back({archiveScope,key,0,1,observation.dump(),source});changes.push_back(mutation("ha.world","recent",recent,recent.value,source));
    }
