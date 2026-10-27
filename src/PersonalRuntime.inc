    mutable std::recursive_mutex personalMutex;
    std::int64_t nextPersonalTick=0;
    bool personalInitialized=false,personalWasPaused=false;
    Json personalContext(const std::string& query="",std::size_t limit=8,const std::string& status="active")const{
        std::lock_guard lock(personalMutex);if(cfg.channel)return {{"threads",Json::array()}};
        Json values=Json::array();const auto policy=visibility();std::vector<std::pair<int,Json>> matches;
        const auto index=get("ha.index","personal",Json::array()).value;
        for(auto it=index.rbegin();it!=index.rend();++it){
            const auto row=get("ha.personal",it->get<std::string>());if(!row.revision)continue;
            const bool active=row.value.value("status",std::string{})=="active";
            if((status=="active"&&!active)||(status=="closed"&&active))continue;
            bool allSources=true;for(const auto& ref:row.value.value("evidence_sources",row.value.value("sources",Json::array())))if(!policy.source(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")})){allSources=false;break;}if(!allSources)continue;
            for(const auto& ref:row.value.value("resolution_sources",Json::array()))if(!policy.source(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")})){allSources=false;break;}if(!allSources)continue;
            auto visible=row.value;Json episodes=Json::array();
            for(const auto& episode:row.value.at("episodes")){
                bool allowed=true;for(const auto& ref:episode.at("sources"))if(!policy.source(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")})){allowed=false;break;}
                if(allowed)episodes.push_back(episode);
            }
            if(episodes.empty())continue;
            // The latest unavailable episode must not leak through the current
            // narrative or through a derived preference score.
            const auto& latest=episodes.back();visible["note"]=latest.at("note");visible["stance"]=latest.at("stance");
            if(episodes.size()!=row.value.at("episodes").size()){visible["leaning"]=0;visible["familiarity"]="tentative";}
            auto value=personal::context(visible,row.value.value("edit_revision",row.revision));value["id"]=*it;value["source_ids"]=Json::array();for(const auto& ref:latest.at("sources"))value["source_ids"].push_back(ref.at("id"));
            if(!active){value["resolution"]=visible.value("resolution",std::string{});value["resolved_ms"]=visible.value("resolved_ms",std::int64_t(0));}
            const auto search=value.value("topic",std::string{})+" "+value.value("note",std::string{})+" "+value.value("resolution",std::string{});
            const auto score=query.empty()?1:memoryRelevance(query,search)+(search.find(query)!=std::string::npos?50:0);
            if(score>0)matches.emplace_back(score,std::move(value));
        }
        std::stable_sort(matches.begin(),matches.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        for(auto& match:matches){if(values.size()>=limit)break;values.push_back(std::move(match.second));}
        return {{"threads",values},{"matching_count",matches.size()},{"omitted_count",matches.size()-values.size()},{"guide","这些是有来源的近期兴趣、偏好和未完成意图，不是强制话题或固定台词。可改变主意，也可继续自己的事情；只有确实相关时才带入对话。数值由应用维护，不决定必须如何回复。self.observe可按query找较早的关注，status=closed可找已经做完或放下的事；放下的关注不会自动重新激活。"}};
    }
    Json personalTool(const std::string& name,const Json& args,ProposalContext& context,const std::string& traceId){
        std::lock_guard lock(personalMutex);
        if(name=="self.observe")return observedTool(name,toolSuccess(personalContext(args.value("query",std::string{}),args.value("limit",std::size_t(24)),args.value("status",std::string("active")))),context,traceId);
        const auto key=args.at("id").get<std::string>();if(!validRequestId(key))return toolFailure("personal_identity");
        auto before=get("ha.personal",key);const auto revision=before.value.value("edit_revision",before.revision);const auto expected=args.value("expected_revision",std::int64_t(0));if(expected!=revision)return toolFailure("personal_revision_conflict");
        const auto refs=checkedSources(args.at("source_ids"),context);if(refs.is_null()||refs.empty())return toolFailure("personal_source_not_available");
        const auto policy=visibility();if(before.revision)for(const auto& ref:before.value.value("evidence_sources",before.value.value("sources",Json::array())))if(!policy.source(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")}))return toolFailure("personal_previous_source_not_available");
        Json changed;
        if(name=="self.notice"){
            const auto kind=args.at("kind").get<std::string>(),stance=args.at("stance").get<std::string>();
            if((kind=="preference"&&stance!="like"&&stance!="avoid")||(kind=="interest"&&stance!="curious")||(kind=="intention"&&stance!="want"))return toolFailure("personal_stance_kind");
            if(before.revision&&(before.value.at("kind")!=kind||before.value.at("topic")!=args.at("topic")))return toolFailure("personal_topic_identity");
            if(before.revision&&before.value.at("status")!="active")return toolFailure("personal_thread_closed");
            if(before.revision&&before.value.value("evidence_count",0)>=128&&!personal::seenEvidence(before.value,refs))return toolFailure("personal_evidence_limit");
            auto index=get("ha.index","personal",Json::array());if(!before.revision&&index.value.size()>=128)return toolFailure("personal_thread_limit");
            std::size_t active=0;for(const auto& id:index.value)if(get("ha.personal",id.get<std::string>()).value.value("status",std::string{})=="active")++active;
            if(!before.revision&&active>=24)return toolFailure("personal_active_limit");
            changed=personal::notice(before.revision?before.value:Json(nullptr),args,refs,serviceNowMs());
            if(before.revision&&changed.value("evidence_count",0)==before.value.value("evidence_count",0))return toolSuccess({{"id",key},{"revision",revision},{"deduplicated",true},{"thread",personal::context(before.value,revision)}});
            changed["sources"]=refs;changed["edit_revision"]=revision+1;
            std::vector<ayt::storage::RecordMutation> changes={mutation("ha.personal",key,before,changed,traceId)};
            if(!before.revision){index.value.push_back(key);changes.push_back(mutation("ha.index","personal",index,index.value,traceId));}
            commit(std::move(changes),traceId);
        }else{
            if(!before.revision||before.value.at("status")!="active")return toolFailure("personal_thread_not_active");
            const auto outcome=args.at("outcome").get<std::string>();
            if(outcome=="completed"){
                if(before.value.at("kind")!="intention")return toolFailure("personal_completion_requires_intention");
                bool actual=false;for(const auto& ref:refs){
                    const auto evidence=get(ref.at("scope"),ref.at("id"));const auto kind=ref.at("kind").get<std::string>();
                    if(kind=="task_complete")actual=true;
                    if(kind=="outer_event"){
                        const auto event=evidence.value.value("observation",Json::object()).value("kind",std::string{});
                        if(event=="widget.move_finished"||event=="rhythm.rest_finished"||event=="habitat.consumed"||event=="habitat.inspected"||event=="habitat.arranged"||event=="habitat.planted"||event=="habitat.appeared"||event=="atelier.created"||event=="atelier.revised"||event=="pocket.solved")actual=true;
                    }
                    if(kind=="tool_observation"){
                        const auto tool=evidence.value.value("tool",std::string{});const auto result=evidence.value.value("result",Json::object());
                        if(result.value("completed",false)||((tool=="workspace.write"||tool=="atelier.write")&&result.value("written",false)))actual=true;
                    }
                }
                if(!actual)return toolFailure("personal_completion_requires_result");
            }
            changed=personal::projected(before.value,serviceNowMs());changed["edit_revision"]=revision+1;changed["status"]=outcome;changed["resolution"]=args.at("note");changed["resolution_sources"]=refs;changed["resolved_ms"]=serviceNowMs();
            commit({mutation("ha.personal",key,before,changed,traceId)},traceId);
        }
        return toolSuccess({{"id",key},{"revision",revision+1},{"thread",personal::context(changed,revision+1)}});
    }
    void personalTick(std::int64_t now){
        std::lock_guard lock(personalMutex);if(cfg.channel)return;
        const bool rebase=!personalInitialized||personalWasPaused!=runtimePaused;personalInitialized=true;personalWasPaused=runtimePaused;
        if(!rebase&&now<nextPersonalTick)return;nextPersonalTick=now+30000;if(runtimePaused&&!rebase)return;
        for(const auto& key:get("ha.index","personal",Json::array()).value){
            auto before=get("ha.personal",key.get<std::string>());if(before.value.value("status",std::string{})!="active")continue;
            auto changed=before.value;if(rebase)changed["updated_ms"]=now;else changed=personal::projected(before.value,now);
            if(rebase||changed.at("leaning")!=before.value.at("leaning"))commit({mutation("ha.personal",key.get<std::string>(),before,changed)},"personal-active-time");
        }
    }
