    mutable std::recursive_mutex personalMutex;
    std::int64_t nextPersonalTick=0;
    bool personalInitialized=false,personalWasPaused=false;
    Json personalEvidenceAnchors(const Json& refs)const{
        Json values=Json::array();std::set<std::string> visited;
        const auto policy=visibility();
        const std::function<void(const Json&,unsigned)> visit=[&](const Json& ref,unsigned depth){
            if(depth>8||visited.size()>=64)return;
            const auto scope=ref.at("scope").get<std::string>(),key=ref.at("id").get<std::string>();
            if((scope!=archiveScope&&scope!=memoryScope)||!visited.insert(scope+":"+key).second)return;
            const Source source{key,ref.at("revision"),ref.at("kind"),scope};if(!policy.source(source))return;
            const auto row=get(scope,key);if(!row.revision||row.revision!=source.revision)return;
            if(scope==memoryScope){if(!policy.allows(row.value))return;for(const auto& parent:row.value.at("sources"))visit(parent,depth+1);return;}
            if(row.value.value("kind",std::string{})=="assistant_message"){
                const auto trace=get("ha.trace",row.value.value("trace_id",std::string{}));
                if(trace.revision){
                    const auto event=trace.value.value("source_event_id",std::string{});if(!event.empty()){const auto original=get(archiveScope,event);if(original.revision)visit(Json{{"scope",archiveScope},{"id",event},{"revision",original.revision},{"kind",original.value.at("kind")}},depth+1);}
                    else {const auto task=trace.value.value("task_context",Json::object());if(task.is_object())for(const auto& parent:task.value("references",Json::array()))visit(parent,depth+1);}
                }return;
            }
            const auto candidate=personal::terminalAnchor(row.value,ref);if(candidate.is_null())return;
            const auto found=std::find_if(values.begin(),values.end(),[&](const auto& old){return old.at("key")==candidate.at("key");});
            if(found==values.end())values.push_back(candidate);else if(found->at("weight")<candidate.at("weight"))*found=candidate;
        };
        for(const auto& ref:refs)visit(ref,0);return values;
    }
    bool currentIntention(const Json& link)const{
        std::lock_guard lock(personalMutex);const auto row=get("ha.personal",link.at("id"));
        if(!row.revision||row.value.value("kind",std::string{})!="intention"||row.value.value("status",std::string{})!="active"||row.value.value("edit_revision",row.revision)!=link.at("revision"))return false;
        const auto policy=visibility();for(const auto& ref:row.value.value("evidence_sources",row.value.value("sources",Json::array())))if(!policy.source(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")}))return false;
        return true;
    }
    std::string recordIntentionAttempt(const Json& result){
        if(!result.contains("intention"))return {};
        std::lock_guard lock(personalMutex);const auto key="intent-attempt-"+result.at("id").get<std::string>()+(result.contains("effect_outcome")?"-effect":"");if(get(archiveScope,key).revision)return key;
        Json sources=Json::array();const auto effectId=result.value("source_id",std::string{});if(!effectId.empty()){const auto effect=get(archiveScope,effectId);if(effect.revision)sources.push_back({{"id",effectId},{"revision",effect.revision},{"kind",effect.value.at("kind")},{"scope",archiveScope}});}
        Json attempt={{"intention",result.at("intention")},{"activity_id",result.at("id")},{"action",result.at("kind")},{"status",result.at("status")},{"started",result.contains("started_ms")},{"ended_ms",result.at("ended_ms")},{"result_sources",sources}};
        for(const auto* field:{"purpose","started_ms","effect_outcome","effect_ended_ms"})if(result.contains(field))attempt[field]=result.at(field);
        auto index=get("ha.personal-attempts",result.at("intention").at("id"),Json::array());index.value.push_back(key);while(index.value.size()>6)index.value.erase(0);
        // Private derived working history, not an independently experienced
        // world event and not searchable through the raw dialogue library.
        const Json archived={{"kind","personal_attempt"},{"observation",{{"kind","intention.activity"},{"attempt",attempt}}},{"text","一次与自己打算有关的动作记录；只确认该动作的实际进度，不宣告整个打算完成。"},{"sources",sources},{"created_at",result.at("ended_ms")}};
        commit({{archiveScope,key,0,1,archived.dump(),result.at("id")},mutation("ha.personal-attempts",result.at("intention").at("id"),index,index.value,result.at("id"))},result.at("id"));return key;
    }
    void authorizePersonalAttempts(const Json& view,ProposalContext& context)const{
        const auto policy=visibility();for(const auto& thread:view.value("threads",Json::array()))for(const auto& attempt:thread.value("attempts",Json::array())){
            const auto key=attempt.at("source_id").get<std::string>();const auto row=get(archiveScope,key);const Source source{key,attempt.at("source_revision"),"personal_attempt",archiveScope};
            if(row.revision==source.revision&&policy.source(source)&&policy.allows(row.value)){
                // A private progress view is not a new experience. Authorize
                // only actual original tool results, never the derived view as
                // standalone proof for a new memory or developmental stage.
                for(const auto& ref:row.value.at("sources")){
                    const Source actual{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};const auto evidence=get(actual.scope,actual.id);
                    if(evidence.revision==actual.revision&&policy.source(actual)&&(!evidence.value.contains("sources")||policy.allows(evidence.value)))context.sources[actual.id]=actual;
                }
            }
        }
    }
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
            if(episodes.size()!=row.value.at("episodes").size()){visible["leaning"]=0;visible["familiarity"]="tentative";visible.erase("basis");}
            visible["episodes"]=episodes;
            auto value=personal::context(visible,row.value.value("edit_revision",row.revision));value["id"]=*it;value["source_ids"]=Json::array();for(const auto& ref:latest.at("sources"))value["source_ids"].push_back(ref.at("id"));
            if(!active){value["resolution"]=visible.value("resolution",std::string{});value["resolved_ms"]=visible.value("resolved_ms",std::int64_t(0));}
            if(visible.value("kind",std::string{})=="intention"){
                value["attempts"]=Json::array();for(const auto& key:get("ha.personal-attempts",it->get<std::string>(),Json::array()).value){
                    const auto attempt=get(archiveScope,key);if(!attempt.revision||!policy.source(Source{key,attempt.revision,"personal_attempt",archiveScope})||!policy.allows(attempt.value))continue;
                    auto fact=attempt.value.at("observation").at("attempt");fact["source_id"]=key;fact["source_revision"]=attempt.revision;value["attempts"].push_back(std::move(fact));
                }
            }
            const auto search=value.value("topic",std::string{})+" "+value.value("note",std::string{})+" "+value.value("resolution",std::string{});
            const auto score=query.empty()?1:memoryRelevance(query,search)+(search.find(query)!=std::string::npos?50:0);
            if(score>0)matches.emplace_back(score,std::move(value));
        }
        std::stable_sort(matches.begin(),matches.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        for(auto& match:matches){if(values.size()>=limit)break;values.push_back(std::move(match.second));}
        return {{"threads",values},{"matching_count",matches.size()},{"omitted_count",matches.size()-values.size()},{"guide","这里是有来源的兴趣、偏好和未完打算，不是待完成的作业。stance/note是最近感受，leaning/basis是缓慢积累的倾向；recent_changes保留不同看法。本次喜欢可以自然表达，也可以有新判断，相关时再接续。self.observe可按query找旧关注，status=closed能找做完或放下的缘由，查询不会恢复旧任务。"}};
    }
    Json personalDialogueContext(const std::string& input,bool privateTime)const{
        auto view=personalContext();if(privateTime)return view;
        for(auto& thread:view["threads"]){
            const auto text=thread.value("topic",std::string{})+" "+thread.value("note",std::string{});
            if(thread.value("kind",std::string{})=="intention"||memoryRelevance(input,text)>0){thread["context_detail"]="relevant";continue;}
            // Keep the disposition available without putting the entire same
            // old episode at the front of every unrelated everyday exchange.
            for(const auto* field:{"note","recent_changes","source_ids","created_ms","last_noticed_ms"})thread.erase(field);
            thread["context_detail"]="background_index";thread["details_tool"]="self.observe";
        }return view;
    }
    Json personalTool(const std::string& name,const Json& args,ProposalContext& context,const std::string& traceId){
        std::lock_guard lock(personalMutex);
        if(name=="self.observe"){auto view=personalContext(args.value("query",std::string{}),args.value("limit",std::size_t(24)),args.value("status",std::string("active")));authorizePersonalAttempts(view,context);return toolSuccess(view);}
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
            const bool newSources=!before.revision||personal::hasNewSources(before.value,refs),newMeaning=!before.revision||!personal::sameMeaning(before.value,args);
            if(before.revision&&!newSources&&!newMeaning)return toolSuccess({{"id",key},{"revision",revision},{"deduplicated",true},{"thread",personal::context(before.value,revision)}});
            if(before.revision&&before.value.value("evidence_count",0)>=128&&newSources)return toolFailure("personal_evidence_limit");
            if(before.revision&&before.value.value("change_count",0)>=256)return toolFailure("personal_change_limit");
            auto index=get("ha.index","personal",Json::array());if(!before.revision&&index.value.size()>=128)return toolFailure("personal_thread_limit");
            std::size_t active=0;for(const auto& id:index.value)if(get("ha.personal",id.get<std::string>()).value.value("status",std::string{})=="active")++active;
            if(!before.revision&&active>=24)return toolFailure("personal_active_limit");
            auto working=before.revision?before.value:Json(nullptr);
            if(before.revision&&!working.contains("experience_evidence"))for(auto& episode:working["episodes"])episode["experience_anchors"]=personalEvidenceAnchors(episode.at("sources"));
            changed=personal::notice(std::move(working),args,refs,serviceNowMs(),personalEvidenceAnchors(refs));
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
