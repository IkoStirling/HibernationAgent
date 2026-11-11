    Json peopleIdentity(const ProposalContext* context=nullptr)const{return cfg.channel?cfg.channel->identity:context?context->channelIdentity:Json::object();}
    std::string peopleSubject(const Json& who)const{return people::subject(who,cfg.channel?cfg.channel->sharedOwner:who.empty()||ownerChannel(who));}
    bool peopleSource(const Json& ref,const std::string& person)const{
        try{const auto row=get(ref.at("scope"),ref.at("id"));if(!row.revision||row.revision!=ref.at("revision")||row.value.value("kind",std::string{})!="user_message")return false;
            Source source{ref.at("id"),ref.at("revision"),"user_message",ref.at("scope")};if(!visibility().source(source))return false;
            auto who=row.value.value("channel_identity",Json::object());if(cfg.channel&&who.empty())return false;
            return peopleSubject(who)==person;
        }catch(...){return false;}
    }
    Json peopleView(const std::string& query,const Json& who=Json::object())const{
        if(cfg.channel&&cfg.channel->sharedOwner)return cfg.channel->sharedPeople;
        const auto person=peopleSubject(cfg.channel?cfg.channel->identity:who);return people::read(cfg.dataRoot,person,{{"query",query}},[&](const Json& ref){return peopleSource(ref,person);},serviceNowMs(),true);
    }
    Json peopleTool(const std::string& name,const Json& args,ProposalContext& context,const std::string& trace){
        const auto person=peopleSubject(peopleIdentity(&context));if(person.empty())return toolFailure("people_identity_unavailable");
        if(name!="people.note")return toolSuccess(people::read(cfg.dataRoot,person,args,[&](const Json& ref){return peopleSource(ref,person);},serviceNowMs(),name=="people.current"));
        if(context.eventKind!="user_message"||!context.taskId.empty()||!context.skillId.empty())return toolFailure("people_conversation_source_required");
        const auto id=args.at("source_id").get<std::string>();const auto found=context.sources.find(id);if(found==context.sources.end()||found->second.kind!="user_message")return toolFailure("people_source_unavailable");
        const auto& s=found->second;Json ref={{"id",s.id},{"revision",s.revision},{"scope",s.scope}};if(!peopleSource(ref,person))return toolFailure("people_source_wrong_person_or_hidden");
        const auto raw=get(s.scope,s.id);const auto quote=args.at("quote").get<std::string>();if(quote.find_first_not_of(" \t\r\n")==quote.npos||raw.value.value("text",std::string{}).find(quote)==std::string::npos)return toolFailure("people_quote_mismatch");
        ref["created_ms"]=raw.value.value("created_at",std::int64_t(0));return people::note(cfg.dataRoot,person,args,ref,trace,serviceNowMs());
    }
