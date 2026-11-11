    Json desktopReplyRoute()const{return {{"kind","desktop"}};}
    Json qqReplyRoute(const Json& identity)const{
        Json who=Json::object();for(const auto* field:{"channel","transport","self_user_id","sender_user_id","sender_is_owner","message_type","group_id","message_id"})if(identity.contains(field))who[field]=identity.at(field);
        return {{"kind","qq"},{"identity",who}};
    }
    Json contextReplyRoute(const ProposalContext& context)const{return context.channelIdentity.empty()?desktopReplyRoute():qqReplyRoute(context.channelIdentity);}
    bool qqRouted(const Json& value)const{return value.is_object()&&value.value("kind",std::string{})=="qq";}
    bool desktopDialogueRecord(const Json& value)const{return cfg.channel||!qqRouted(value.value("reply_route",Json::object()));}
    bool routedToolAllowed(const ProposalContext& context,const std::string& name)const{
        if(cfg.channel||context.channelIdentity.empty())return true;if(!napcatIdentityAllowed(context.channelIdentity))return false;
        if(ownerChannel(context.channelIdentity))return true;
        if(name.starts_with("memory.")||continuity::roleTool(name)||name.starts_with("development.")||name=="capabilities.list"||name.starts_with("skills."))return false;
        return napcatCanControl(context.channelIdentity);
    }
    Json routedPublicState()const{Json state=Json::object();const auto body=life.snapshot();for(const auto* field:{"mood","energy","openness","willingness","social_load"})if(body.contains(field))state[field]=body.at(field);return state;}
    std::string routedExcerpt(std::string text,std::size_t limit)const{if(text.size()<=limit)return text;text.resize(limit);while(!text.empty()&&(static_cast<unsigned char>(text.back())&0xc0)==0x80)text.pop_back();if(!text.empty()&&static_cast<unsigned char>(text.back())>=0xc0)text.pop_back();return text;}
    void restrictRoutedReview(ayt::inference::Request& request,const ProposalContext& context)const{
        if(cfg.channel||context.channelIdentity.empty()||ownerChannel(context.channelIdentity))return;auto input=Json::parse(request.messages.at(1).text);
        input["authoritative_state"]=routedPublicState();input["open_questions"]=Json::array();input["shared_understanding"]=Json::object();input["current_capabilities"]={{"tools",scopedTools(context)}};input["conversation_delivery"]={{"channel","qq"},{"identity",context.channelIdentity}};request.messages[1].text=cacheOrderedReview(input);
    }
    void restrictRoutedContext(ayt::inference::Request& request,const Json& payload,const Json& route,ProposalContext& context)const{
        if(cfg.channel||!qqRouted(route)||ownerChannel(route.at("identity")))return;
        const auto same=[&](const Json& candidate){if(!qqRouted(candidate))return false;for(const auto* field:{"self_user_id","sender_user_id","message_type","group_id"})if(candidate.at("identity").value(field,std::string{})!=route.at("identity").value(field,std::string{}))return false;return true;};
        Json evidence=Json::array();std::set<std::string> admitted;std::size_t bytes=0;const auto budget=std::min<std::size_t>(12000,cfg.contextBytes/2);
        for(const auto& ref:payload.value("references",Json::array())){const auto row=get(ref.value("scope",std::string(archiveScope)),ref.at("id"));if(!row.revision)continue;const auto from=row.value.contains("reply_route")?row.value.at("reply_route"):row.value.contains("channel_identity")?qqReplyRoute(row.value.at("channel_identity")):desktopReplyRoute();if(!same(from))continue;
            Json data=Json::object();for(const auto* field:{"kind","text","tool","title","status","created_at"})if(row.value.contains(field)&&row.value.at(field).is_primitive())data[field]=row.value.at(field).is_string()?Json(routedExcerpt(row.value.at(field).get<std::string>(),field==std::string("text")?2400:256)):row.value.at(field);
            const auto result=row.value.value("result",Json::object());if(result.is_object())for(const auto* field:{"ok","status","state","accepted","completed","sent","title","duration_seconds","frame_time_seconds"})if(result.contains(field)&&result.at(field).is_primitive())data["result"][field]=result.at(field).is_string()?Json(routedExcerpt(result.at(field).get<std::string>(),256)):result.at(field);
            Json item={{"id",ref.at("id")},{"data",data}};const auto size=item.dump().size();if(bytes+size>budget)continue;bytes+=size;evidence.push_back(item);admitted.insert(ref.at("id").get<std::string>());
        }
        // Non-owner/group callbacks retain their original conversation rights.
        // No desktop history, personal state, images or retained session prefix.
        Json event={{"kind","solitude"},{"input",evidence.empty()?Json("一次接续机会。没有本会话可用资料时可以保持安静。"):Json(payload.value("topic",std::string{}))},{"context",evidence},{"authoritative_state",routedPublicState()},{"reply_destination",{{"channel","qq"},{"identity",route.at("identity")}}},{"privacy_note","仅本QQ会话的接续资料。群聊对群成员公开；不引用其他对话或桌面的私人内容。"}};
        request.messages={request.messages.front(),ayt::inference::Message{"user",cacheOrderedEvent(event)}};
        std::erase_if(context.sources,[&](const auto& entry){return !admitted.contains(entry.first);});std::erase_if(context.narrativeSources,[&](const auto& entry){return !admitted.contains(entry.second.id);});
    }
    void rememberReplyContact(const Json& route){
        if(cfg.channel)return;const auto before=get("ha.delivery","last-contact");commit({mutation("ha.delivery","last-contact",before,{{"route",route},{"updated_ms",serviceNowMs()}},"conversation-contact")},"conversation-contact");
    }
    Json recentReplyRoute()const{
        const auto last=get("ha.delivery","last-contact");if(last.revision&&serviceNowMs()-last.value.value("updated_ms",std::int64_t(0))<86400000)return last.value.at("route");return desktopReplyRoute();
    }
    Json sourceReplyRoute(const Json& references)const{
        for(const auto& ref:references){const auto row=get(ref.value("scope",std::string(archiveScope)),ref.at("id"));if(!row.revision)continue;if(row.value.contains("reply_route"))return row.value.at("reply_route");if(row.value.contains("channel_identity"))return qqReplyRoute(row.value.at("channel_identity"));if(row.value.value("kind",std::string{})=="user_message")return desktopReplyRoute();}
        return recentReplyRoute();
    }
    Json jobReplyRoute(const Json& payload,bool alone)const{
        if(cfg.channel)return qqReplyRoute(cfg.channel->identity);
        if(!alone)return desktopReplyRoute();
        if(payload.contains("reply_route"))return payload.at("reply_route");
        if(payload.contains("remembered_schedule")){const auto row=get("ha.schedules",payload.at("remembered_schedule"));if(row.revision){const auto pending=row.value.value("pending",Json::object());if(pending.is_object()&&pending.contains("reply_route"))return pending.at("reply_route");return row.value.value("reply_route",sourceReplyRoute(row.value.at("references")));}}
        if(payload.contains("continuous_task")){const auto row=get("ha.tasks",payload.at("continuous_task"));if(row.revision){const auto who=row.value.value("channel_identity",Json::object());return who.empty()?desktopReplyRoute():qqReplyRoute(who);}}
        return sourceReplyRoute(payload.value("references",Json::array()));
    }
    void appendRoutedReply(std::vector<RecordMutation>& updates,const Json& route,const Json& messages,const std::string& reply,const std::string& trace){
        if(cfg.channel||!qqRouted(route))return;
        auto index=get("ha.outgoing","index",Json::array());Json retained=Json::array();std::size_t pending=0;
        for(const auto& id:index.value){const auto row=get("ha.outgoing",id);if(row.revision&&row.value.value("status",std::string{})=="pending"){retained.push_back(id);++pending;}}
        if(pending>=32)throw std::runtime_error("channel_outbox_full");
        retained.push_back(reply);updates.push_back({"ha.outgoing",reply,0,1,Json{{"status","pending"},{"route",route},{"messages",messages},{"trace_id",trace},{"created_ms",serviceNowMs()},{"expires_ms",serviceNowMs()+86400000},{"next_ms",0}}.dump(),trace});
        updates.push_back(mutation("ha.outgoing","index",index,retained,trace));
    }
    std::int64_t nextRoutedDelivery=0;
    void cancelRoutedReplies(const char* reason){
        if(cfg.channel)return;for(const auto& key:get("ha.outgoing","index",Json::array()).value){const auto before=get("ha.outgoing",key);if(before.revision&&before.value.value("status",std::string{})=="pending"){auto value=before.value;value["status"]="cancelled";value["issue"]=reason;value["updated_ms"]=serviceNowMs();commit({mutation("ha.outgoing",key,before,value,"routed-reply-cancelled")},"routed-reply-cancelled");}}
    }
    void routedDeliveryTick(std::int64_t now){
        if(cfg.channel)return;if(runtimePaused||runtimeOffline){cancelRoutedReplies(runtimeOffline?"offline":"paused");return;}if(now<nextRoutedDelivery||!plugins)return;nextRoutedDelivery=now+1000;
        for(const auto& key:get("ha.outgoing","index",Json::array()).value){auto before=get("ha.outgoing",key);if(!before.revision||before.value.value("status",std::string{})!="pending"||now<before.value.value("next_ms",std::int64_t(0)))continue;auto value=before.value;const auto route=value.at("route");
            if(now>value.at("expires_ms").get<std::int64_t>()){value["status"]="expired";value["issue"]="delivery_expired";}
            else if(!qqRouted(route)||!napcatIdentityAllowed(route.at("identity"))){value["status"]="blocked";value["issue"]="channel_recipient_revoked";}
            else{const auto receipt=plugins->enqueueChannelReply({{"id",key},{"identity",route.at("identity")},{"messages",value.at("messages")}});value["receipt"]=receipt;const bool accepted=receipt.value("ok",false)&&receipt.at("result").value("queued",false);value["status"]=accepted?"queued":"pending";value["next_ms"]=now+30000;}
            value["updated_ms"]=now;commit({mutation("ha.outgoing",key,before,value,"routed-reply-delivery")},"routed-reply-delivery");
        }
    }
