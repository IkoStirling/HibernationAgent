    void appendPersonalEffect(std::vector<RecordMutation>& changes,const std::string& tool,const Json& result,const Source& source,const ProposalContext& context,std::int64_t now){
        if(context.personalPursuitId.empty()||tool!="canteen.cook"||!result.value("accepted",false))return;
        const auto batch=result.value("job",Json::object()).value("batch_id",std::string{});if(batch.empty())return;
        const auto key="kitchen-"+contentDigest(batch).substr(0,32);if(get("ha.personal-effects",key).revision)return;
        const auto thread=get("ha.personal",context.personalPursuitId);if(!thread.revision)return;
        Json origins=thread.value.value("evidence_sources",thread.value.value("sources",Json::array()));for(const auto& ref:thread.value.value("decision_sources",Json::array()))if(std::find(origins.begin(),origins.end(),ref)==origins.end())origins.push_back(ref);
        Json ref={{"id",source.id},{"revision",source.revision},{"kind",source.kind},{"scope",source.scope}};
        Json effect={{"batch_id",batch},{"intention",{{"id",context.personalPursuitId},{"revision",context.personalPursuitRevision}}},{"domain","kitchen"},{"phase","cooking"},{"accepted_ms",now},{"updated_ms",now},{"sources",Json::array({ref})},{"narrative_sources",origins}};
        auto index=get("ha.personal-effects","index",Json::array());pushIndex(index.value,key,32);
        changes.push_back({"ha.personal-effects",key,0,1,effect.dump(),source.id});changes.push_back(mutation("ha.personal-effects","index",index,index.value,source.id));
    }
    void appendPersonalEffectOutcome(std::vector<RecordMutation>& changes,const Json& event,const std::string& experience,std::int64_t now){
        const auto batch=intention_life::effectBatch(event),phase=intention_life::effectPhase(event);if(batch.empty()||phase.empty())return;
        const auto key="kitchen-"+contentDigest(batch).substr(0,32);const auto before=get("ha.personal-effects",key);if(!before.revision||before.value.value("batch_id",std::string{})!=batch)return;
        // Only an actual plugin outcome advances this small causal link. It
        // never completes, resumes or rewrites the character's intention.
        auto value=before.value;const Json ref={{"id",experience},{"revision",1},{"kind","outer_event"},{"scope",archiveScope}};
        if(!intention_life::advanceEffect(value.value("phase",std::string{}),phase))return;
        value["phase"]=phase;value["updated_ms"]=now;value["result_sources"]=Json::array({ref});
        changes.push_back(mutation("ha.personal-effects",key,before,value,experience));
        const auto attemptId="intent-effect-"+contentDigest(key+":"+phase).substr(0,32);if(get(archiveScope,attemptId).revision)return;
        Json attempt={{"intention",value.at("intention")},{"activity_id",key},{"action","kitchen_result"},{"domain","kitchen"},{"status",phase},{"started",true},{"ended_ms",now},{"result_sources",Json::array({ref})},{"batch_id",batch}};
        Json causalSources=value.at("sources");causalSources.push_back(ref);
        Json record={{"kind","personal_attempt"},{"observation",{{"kind","intention.activity"},{"attempt",attempt}}},{"text","自己先前安排的料理发生了实际变化；仅记录该批料理，不表示整个愿望完成。"},{"sources",causalSources},{"narrative_sources",value.at("narrative_sources")},{"created_at",now}};
        auto index=get("ha.personal-attempts",value.at("intention").at("id"),Json::array());pushIndex(index.value,attemptId,6);
        changes.push_back({archiveScope,attemptId,0,1,record.dump(),experience});changes.push_back(mutation("ha.personal-attempts",value.at("intention").at("id"),index,index.value,experience));
    }
    Json personalPendingEffectBody(const Json& snapshots)const{
        const auto index=get("ha.personal-effects","index",Json::array()).value;if(index.empty())return nullptr;
        const auto canteen=snapshots.value("canteen",Json::object());const auto current=canteen.is_object()?canteen.value("current",Json(nullptr)):Json(nullptr);
        const auto kitchen=current.is_object()?current:Json::object();const auto habitat=snapshots.value("habitat",Json::object());
        const auto objects=habitat.is_object()?habitat.value("objects",Json::array()):Json::array();
        std::optional<MemoryVisibility> policy;
        for(auto it=index.rbegin();it!=index.rend();++it){
            const auto row=get("ha.personal-effects",it->get<std::string>());if(!row.revision)continue;
            const auto& value=row.value;const auto phase=value.at("phase").get<std::string>();if(phase!="cooking"&&phase!="ready"&&phase!="on_desktop")continue;
            const auto batch=value.at("batch_id").get<std::string>();
            if(phase=="cooking"&&(!kitchen.value("job",Json()).is_object()||kitchen.at("job").value("batch_id",std::string{})!=batch))continue;
            if(phase=="ready"){const auto shelf=kitchen.value("shelf",Json::array());if(!shelf.is_array()||std::none_of(shelf.begin(),shelf.end(),[&](const auto& item){return item.is_object()&&item.value("id",std::string{})==batch;}))continue;}
            if(phase=="on_desktop"&&(!objects.is_array()||std::none_of(objects.begin(),objects.end(),[&](const auto& item){if(!item.is_object())return false;const auto origin=item.value("provenance",Json(nullptr));return origin.is_object()&&origin.value("batch_id",std::string{})==batch;})))continue;
            // Finished history or an absent plugin has no current body detail.
            // Build the full rights view only when a live match might be shown.
            if(!policy)policy=visibility();if(!currentIntention(value.at("intention"),&*policy))continue;
            bool allowed=true;for(const auto* field:{"sources","narrative_sources","result_sources"})for(const auto& ref:value.value(field,Json::array()))if(!activityNarrativeSourceAllowed(ref,*policy)){allowed=false;break;}if(!allowed)continue;
            const auto thread=get("ha.personal",value.at("intention").at("id"));return {{"intention",value.at("intention")},{"topic",thread.value.at("topic")},{"domain","kitchen"},{"phase",phase=="cooking"?"waiting_for_result":phase=="ready"?"result_available":"on_desktop"},{"batch_id",value.at("batch_id")},{"goal_completed",false}};
        }return nullptr;
    }
