    Json modelLifeState(Json value,ProposalContext* context=nullptr)const{
        if(cfg.channel){for(const auto* field:{"focus","appraisal_source_id","appraisal_valence","appraisal_age_ms"})value.erase(field);return value;}
        const auto policy=visibility();const auto remember=[&](const Json& refs){if(context)for(const auto& ref:refs){const Source source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")};context->narrativeSources[source.scope+":"+source.id]=source;}};
        if(!value.value("focus",std::string{}).empty()){
            const auto focus=get("ha.focus","current").value;const auto refs=focus.value("narrative_sources",Json::array());
            const bool allowed=!cfg.channel&&focus.value("text",std::string{})==value.value("focus",std::string{})&&!refs.empty()&&std::all_of(refs.begin(),refs.end(),[&](const Json& ref){return activityNarrativeSourceAllowed(ref,policy);});
            if(allowed)remember(refs);else value.erase("focus");
        }
        const auto key=value.value("appraisal_source_id",std::string{});bool known=false;
        if(!cfg.channel&&!key.empty()){
            const auto receipt=get("ha.appraisals","appraisal-"+contentDigest(std::string(archiveScope)+":"+key));
            if(receipt.revision&&receipt.value.at("valence")==value.value("appraisal_valence",0.0)){
                const auto ref=receipt.value.at("source");known=activityNarrativeSourceAllowed(ref,policy);if(known)remember(Json::array({ref}));
            }
        }
        if(!known)for(const auto* field:{"appraisal_source_id","appraisal_valence","appraisal_age_ms"})value.erase(field);
        return value;
    }
    void saveLifeAndFocus(const std::string& focus,const ProposalContext& context,const std::string& traceId){
        std::lock_guard guard(lifeSaveMutex);const auto state=life.snapshot();const auto priorLife=get("ha.life","current");std::vector<RecordMutation> changes={mutation("ha.life","current",priorLife,state,traceId)};
        if(!focus.empty()&&state.value("focus",std::string{})==focus){const auto before=get("ha.focus","current");changes.push_back(mutation("ha.focus","current",before,{{"text",focus},{"narrative_sources",activityNarrativeSources(context)},{"created_at",serviceNowMs()}},traceId));}
        commit(std::move(changes),traceId);
    }
    Json appraiseExperience(const Json& args,ProposalContext& context,const std::string& traceId){
        const auto key=args.at("source_id").get<std::string>();const auto found=context.sources.find(key);
        if(found==context.sources.end()||found->second.scope!=archiveScope)return toolFailure("appraisal_source_not_available");
        const auto& source=found->second;const auto policy=visibility();const auto original=get(source.scope,key);
        if(!policy.source(source)||original.revision!=source.revision||(original.value.contains("sources")&&!policy.allows(original.value)))return toolFailure("appraisal_source_not_available");
        const auto kind=original.value.value("kind",std::string{});
        if(!appraisalExperienceRecord(original.value))return toolFailure("appraisal_requires_experience");
        const Json ref={{"id",key},{"scope",source.scope},{"revision",source.revision},{"kind",kind}};const double valence=args.at("valence");
        const auto identity="appraisal-"+contentDigest(source.scope+":"+key);std::lock_guard guard(lifeSaveMutex);const auto before=get("ha.appraisals",identity);
        if(before.revision&&before.value.at("source")==ref&&before.value.at("valence")==valence)return toolSuccess({{"accepted",true},{"changed",false},{"deduplicated",true},{"revision",before.revision},{"state",modelLifeState(life.snapshot(),&context)}});
        std::string issue;if(!life.appraise(valence,key,serviceNowMs(),issue))return toolFailure(issue.c_str());
        const auto priorLife=get("ha.life","current");const auto state=life.snapshot();const Json value={{"source",ref},{"valence",valence},{"created_at",before.revision?before.value.at("created_at"):Json(serviceNowMs())},{"updated_at",serviceNowMs()}};
        commit({mutation("ha.appraisals",identity,before,value,traceId),mutation("ha.life","current",priorLife,state,traceId)},traceId);
        return toolSuccess({{"accepted",true},{"changed",true},{"deduplicated",false},{"revision",before.revision+1},{"state",modelLifeState(state,&context)}});
    }
