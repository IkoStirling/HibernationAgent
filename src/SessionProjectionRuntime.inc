    bool projectionSourceAllowed(const Source& source,const MemoryVisibility& policy,const Json& dialogue,bool retained=true)const{
        if(!policy.source(source))return false;
        const auto row=get(source.scope,source.id);if(!row.revision||(retained&&row.revision!=source.revision))return false;
        if(row.value.contains("sources")&&!policy.allows(row.value))return false;
        if(!retained)return true;
        if(isLegacyMetaReply(row.value))return false;
        return !(source.scope==archiveScope&&dialogueExcluded(dialogue,source.id))&&!dialogueDigestExcluded(dialogue,row.value);
    }
    bool projectionSourceAllowed(const Source& source)const{return projectionSourceAllowed(source,visibility(),get("ha.context","dialogue").value);}
    Json projectionSources(const ProposalContext& context)const{
        Json refs=Json::array();std::set<std::string> seen;
        const auto add=[&](const Source& s,const char* access){const auto key=s.scope+":"+s.id;if(seen.insert(key).second)refs.push_back({{"scope",s.scope},{"id",s.id},{"revision",s.revision},{"kind",s.kind},{"access",access}});};
        for(const auto& [key,s]:context.sources)add(s,"content");for(const auto& [key,s]:context.narrativeSources)add(s,"narrative");return refs;
    }
    Json projectionFence()const{return {{"owner_access",ownerAccessFence()},{"access",get("ha.access","current").revision},{"dialogue",get("ha.context","dialogue").revision},{"stage",get("ha.stage","current").revision}};}
    bool projectionSourcesAllowed(const Json& refs,bool retained=true)const{
        if(!refs.is_array()||refs.size()>256)return false;
        const auto policy=visibility();const auto dialogue=get("ha.context","dialogue").value;
        for(const auto& r:refs)if(!projectionSourceAllowed(Source{r.at("id"),r.at("revision"),r.at("kind"),r.at("scope")},policy,dialogue,retained))return false;return true;
    }
    session::Prepared prepareSessionProjection(const Profile& selected,ayt::inference::Request& request,ProposalContext& context,Json& trace,const ContextControl& control){
        session::Prepared run;run.maxBytes=control.sessionBytes;run.maxTurns=control.sessionMaxTurns;
        if(!control.sessionProjection||!context.taskId.empty()||context.eventKind!="user_message"||session::images(request.messages))return run;
        const auto catalog=scopedTools(context);const auto overhead=2*(request.messages.front().text.size()+catalog.dump().size()+turnSchema().size())+8192;
        if(selected.model.maxRequestBytes<=overhead+32768)return run;
        run.maxBytes=std::min(run.maxBytes,(selected.model.maxRequestBytes-overhead)/2);
        run.enabled=true;run.epoch=id("epoch");run.fullFrame=Json::parse(request.messages.at(1).text);
        auto identity=cfg.channel?cfg.channel->identity:Json::object();
        for(const auto* field:{"received_images","current_input_images","input_message_ids","message_id"})identity.erase(field);
        run.key="session-"+contentDigest(Json{{"profile",selected.id},{"origin",selected.model.origin},{"base_path",selected.model.basePath},{"model",selected.model.model},{"identity",identity}}.dump());
        run.compatibility=contentDigest(Json{{"system",request.messages.front().text},{"tools",catalog},{"protocol",static_cast<int>(selected.model.protocol)},{"schema",selected.model.supportsJsonSchema},{"object",selected.model.supportsJsonObject},{"native",selected.model.supportsToolCalls},{"vision",selected.model.supportsVision},{"effort",selected.model.reasoningEffort},{"control",control.describe()}}.dump());
        const auto fresh=run.fullFrame;const auto initialSources=context.sources;const auto initialNarrative=context.narrativeSources;
        try{
            const auto stored=get("ha.prompt-sessions",run.key);run.head=stored.value;run.reason="new_session";
            if(stored.revision){
                if(run.head.at("version")!=2)throw std::runtime_error("format_changed");
                if(run.head.at("compatibility")!=run.compatibility)throw std::runtime_error("configuration_changed");
                if(run.head.at("fence")!=projectionFence())throw std::runtime_error("authority_changed");
                if(run.head.at("segments").size()>=run.maxTurns)throw std::runtime_error("turn_boundary");
                if(!projectionSourcesAllowed(run.head.at("sources")))throw std::runtime_error("source_changed");
                std::vector<ayt::inference::Message> history;Json previous;
                for(const auto& key:run.head.at("segments")){
                    const auto segment=get("ha.prompt-segments",key);if(!segment.revision||segment.value.at("epoch")!=run.head.at("epoch"))throw std::runtime_error("segment_missing");
                    auto messages=session::decode(segment.value.at("messages"),run.maxBytes);history.insert(history.end(),messages.begin(),messages.end());previous=segment.value.at("frame");
                    if(session::bytes(history)>run.maxBytes)throw std::runtime_error("capacity");
                }
                session::append(run,request.messages,history,previous,run.head.at("epoch"));
                for(const auto& ref:run.head.at("sources")){
                    const auto key=ref.at("id").get<std::string>();Source source{key,ref.at("revision"),ref.at("kind"),ref.at("scope")};
                    if(!session::referenceCanRead(ref)){context.narrativeSources.try_emplace(source.scope+":"+key,source);continue;}
                    if(context.sources.contains(key)&&(context.sources.at(key).scope!=source.scope||context.sources.at(key).revision!=source.revision))throw std::runtime_error("source_changed");
                    if(context.sources.emplace(key,source).second)run.inheritedSourceIds.insert(key);
                }
            }
        }catch(const std::exception& e){
            const std::set<std::string> reasons={"format_changed","configuration_changed","authority_changed","turn_boundary","source_changed","segment_missing","capacity"};
            run.reason=reasons.contains(e.what())?e.what():"invalid_projection";run.reused=false;run.eventIndex=1;run.retainedBytes=0;run.head=Json::object();run.fullFrame=fresh;run.inheritedSourceIds.clear();context.sources=initialSources;context.narrativeSources=initialNarrative;
            request.messages={request.messages.front(),ayt::inference::Message{"user",cacheOrderedEvent(fresh)}};
        }
        if(!run.reused){auto wire=run.fullFrame;wire["projection"]={{"version",1},{"mode","full"},{"epoch",run.epoch}};request.messages[1].text=cacheOrderedEvent(wire);}
        trace["session_projection"]={{"enabled",true},{"mode",run.reused?"append":"baseline"},{"reason",run.reason},{"epoch",run.epoch},{"retained_bytes",run.retainedBytes},{"retained_sources",run.reused?run.head.at("sources"):Json::array()},{"head_key",run.key},{"current_event_index",run.eventIndex},{"max_bytes",run.maxBytes},{"max_turns",run.maxTurns},{"fence",projectionFence()}};
        return run;
    }
    bool sessionStillAuthorized(const session::Prepared& run,const ProposalContext& context,const Json& trace)const{
        if(!run.enabled)return true;
        return trace.at("session_projection").at("fence")==projectionFence()&&(!run.reused||projectionSourcesAllowed(run.head.at("sources")))&&projectionSourcesAllowed(projectionSources(context),false);
    }
    void appendSessionProjection(session::Prepared& run,const ProposalContext& context,const Json& output,const std::string& answerId,const std::string& traceId,std::vector<RecordMutation>& updates){
        if(!run.enabled||run.aborted||!run.captured||!context.taskId.empty()||!context.createdTaskId.empty()||!context.skillId.empty()||session::images(run.capturedMessages))return;
        const auto refs=projectionSources(context);if(!projectionSourcesAllowed(refs))return;
        std::vector<ayt::inference::Message> added(run.capturedMessages.begin()+run.eventIndex,run.capturedMessages.end());
        added.push_back({"assistant",Json{{"version",1},{"kind",output.at("visible_reply")==""?"silence":"reply"},{"text",output.at("visible_reply")==""?Json(nullptr):output.at("visible_reply")},{"calls",Json::array()}}.dump()});
        if(run.retainedBytes+session::bytes(added)>run.maxBytes)return;
        auto head=run.reused?run.head:Json{{"version",2},{"epoch",run.epoch},{"segments",Json::array()}};
        auto sources=refs;if(!answerId.empty())sources.push_back({{"scope",archiveScope},{"id",answerId},{"revision",1},{"kind","assistant_message"},{"access","narrative"}});if(sources.size()>256)return;
        const auto key="segment-"+traceId;auto frame=session::expand(run.fullFrame,run.capturedMessages.at(run.eventIndex).text);frame.erase("authorized_context");
        Json segment={{"epoch",run.epoch},{"messages",session::encode(added)},{"frame",frame}};
        head["segments"].push_back(key);head["sources"]=sources;head["compatibility"]=run.compatibility;head["fence"]=projectionFence();head["bytes"]=run.retainedBytes+session::bytes(added);head["updated_ms"]=serviceNowMs();
        const auto before=get("ha.prompt-sessions",run.key);updates.push_back(mutation("ha.prompt-sessions",run.key,before,head,traceId));updates.push_back({"ha.prompt-segments",key,0,1,segment.dump(),traceId});
    }
