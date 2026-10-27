    Json appraiseExperience(const Json& args,const ProposalContext& context,const std::string& traceId){
        const auto key=args.at("source_id").get<std::string>();const auto found=context.sources.find(key);
        if(found==context.sources.end()||found->second.scope!=archiveScope)return toolFailure("appraisal_source_not_available");
        const auto& source=found->second;const auto policy=visibility();const auto original=get(source.scope,key);
        if(!policy.source(source)||original.revision!=source.revision||(original.value.contains("sources")&&!policy.allows(original.value)))return toolFailure("appraisal_source_not_available");
        const auto kind=original.value.value("kind",std::string{});
        if(!appraisalExperienceRecord(original.value))return toolFailure("appraisal_requires_experience");
        const Json ref={{"id",key},{"scope",source.scope},{"revision",source.revision},{"kind",kind}};const double valence=args.at("valence");
        const auto identity="appraisal-"+contentDigest(source.scope+":"+key);std::lock_guard guard(lifeSaveMutex);const auto before=get("ha.appraisals",identity);
        if(before.revision&&before.value.at("source")==ref&&before.value.at("valence")==valence)return toolSuccess({{"accepted",true},{"changed",false},{"deduplicated",true},{"revision",before.revision},{"state",life.snapshot()}});
        std::string issue;if(!life.appraise(valence,key,serviceNowMs(),issue))return toolFailure(issue.c_str());
        const auto priorLife=get("ha.life","current");const auto state=life.snapshot();const Json value={{"source",ref},{"valence",valence},{"created_at",before.revision?before.value.at("created_at"):Json(serviceNowMs())},{"updated_at",serviceNowMs()}};
        commit({mutation("ha.appraisals",identity,before,value,traceId),mutation("ha.life","current",priorLife,state,traceId)},traceId);
        return toolSuccess({{"accepted",true},{"changed",true},{"deduplicated",false},{"revision",before.revision+1},{"state",state}});
    }
