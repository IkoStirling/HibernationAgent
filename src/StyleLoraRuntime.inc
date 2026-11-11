    Json styleDialogue(Json& candidate,const char* field,const ProposalContext& context,const Json& facts,const std::function<bool()>& cancelled,const Json& expression=Json::object()){
        if(context.eventKind!="user_message"||!candidate.contains(field)||!candidate.at(field).is_string()||candidate.at(field).get_ref<const std::string&>().empty())return Json(nullptr);
        const auto settings=cfg.plugins.value("style-lora",Json::object());const bool channel=cfg.channel&&static_cast<bool>(cfg.channel->editStyle);
        if(!channel&&(!plugins||!plugins->enabled("style-lora")||!settings.value("applyEnabled",false)||settings.value("adapterScale",1.0)==0))return Json(nullptr);
        if(!cfg.reviewEnabled)return {{"applied",false},{"reason","independent_review_required"}};
        const auto original=candidate.at(field).get<std::string>();if(original.size()>12000||facts.dump().size()>6000)return {{"applied",false},{"reason","style_context_limit"}};
        const auto event=get(archiveScope,context.eventId);Json dialogue=Json::array();
        if(event.revision&&event.value.value("kind",std::string{})=="user_message")dialogue.push_back({{"role","user"},{"text",excerpt(event.value.value("text",std::string{}),3000)}});
        const Json styleFacts=expression.value("enabled",false)?Json{{"program_executed_operations",facts},{"expression_preference",expression}}:facts;
        if(styleFacts.dump().size()>6000)return {{"applied",false},{"reason","style_context_limit"}};
        const Json arguments={{"text",original},{"context",dialogue},{"facts",styleFacts.dump()}};
        auto receipt=channel?cfg.channel->editStyle(arguments,cancelled):plugins->invoke("style_lora.preview",arguments,cancelled);
        if(!receipt.value("ok",false))return {{"applied",false},{"error",receipt.value("error",Json::object())}};
        const auto value=receipt.value("result",Json::object());if(!value.contains("text")||!value.at("text").is_string())return {{"applied",false},{"reason","invalid_style_candidate"}};
        const auto proposed=value.at("text").get<std::string>();if(!styleLiteralIntegrity(original,proposed))return {{"applied",false},{"reason","protected_literals_changed"}};
        if(proposed==original)return {{"applied",false},{"reason","unchanged"}};
        if(candidate.contains("messages")){
            Json messages=Json::array();std::size_t begin=0;
            for(;;){const auto end=proposed.find("\n\n",begin);messages.push_back(proposed.substr(begin,end==std::string::npos?end:end-begin));if(end==std::string::npos)break;begin=end+2;}
            if(messages.size()!=candidate.at("messages").size())return {{"applied",false},{"reason","reply_boundaries_changed"}};
            auto changed=candidate;changed[field]=proposed;changed["messages"]=std::move(messages);auto checked=normalizeReplyMessages(std::move(changed),field);
            if(!checked.ok)return {{"applied",false},{"reason","invalid_style_message_bundle"}};
            candidate=std::move(checked.value);
        }else candidate[field]=proposed;
        return {{"applied",true},{"original_text",original},{"proposed_text",proposed},{"adapter_id",value.value("adapter_id",settings.value("adapterId",std::string{}))},{"requires_semantic_review",true}};
    }
