    Json developmentRequestTool()const{return {{"name","development.request"},{"description","号主提出小爱程序的新功能、缺陷或改进并要求转交时，保存需求并转发到小爱现有的导师会话。填写title和details，程序绑定当前消息、固定导师目标和去重编号。回执delivered=true才表示导师会话已收到；等待或失败只代表本地保留，不宣称已送达。导师整理分析，默认不自动修改代码；完成情况可development.status查询。"},{"parameters",{{"type","object"},{"additionalProperties",false},{"required",Json::array({"title","details"})},{"properties",{{"title",{{"type","string"},{"minLength",1},{"maxLength",80}}},{"details",{{"type","string"},{"minLength",1},{"maxLength",4000}}}}}}}};}
    Json developmentStatusTool()const{return {{"name","development.status"},{"description","查询已登记开发需求转给导师的实际进度与回应，不调用模型。id来自development.request，不将已送达当成已开发完成。"},{"parameters",{{"type","object"},{"additionalProperties",false},{"required",Json::array({"id"})},{"properties",{{"id",{{"type","string"},{"minLength",44},{"maxLength",44}}}}}}}};}
    bool developmentOwner(const ProposalContext& context)const{return !cfg.channel&&context.eventKind=="user_message"&&context.skillId.empty()&&context.taskId.empty()&&(context.channelIdentity.empty()||napcatIdentityAllowed(context.channelIdentity)&&context.channelIdentity.value("sender_is_owner",false)&&context.channelIdentity.value("message_type",std::string{})=="private");}
    Json developmentReceipt(const Json& value,bool duplicate=false)const{
        Json result={{"id",value.at("id")},{"recorded",true},{"duplicate",duplicate},{"status",value.at("status")},{"target","mentor"},{"delivered",value.value("developer_notified",false)},{"developer_notified",value.value("developer_notified",false)},{"automatic_execution",false}};
        if(value.contains("forward_receipt"))result["mentor"]=value.at("forward_receipt");if(value.contains("last_error"))result["last_error"]=value.at("last_error");return result;
    }
    Json persistDevelopmentForward(const std::string& key,const Json& receipt,const std::string& trace){
        const auto row=get("ha.development",key);auto value=row.value;
        if(receipt.value("ok",false)){
            const auto& forwarded=receipt.at("result");value["forward_receipt"]=forwarded;value["status"]=forwarded.value("status",std::string("uncertain"));value["developer_notified"]=forwarded.value("delivered",false);value.erase("last_error");
        }else{value["last_error"]=receipt.value("error",Json::object()).value("code",std::string("mentor_unavailable"));if(!value.value("developer_notified",false))value["status"]="pending";}
        commit({mutation("ha.development",key,row,value,trace)},"development-mentor-receipt");return toolSuccess(developmentReceipt(value));
    }
    Json developmentRequestStatus(const Json& args,const ProposalContext& context,const std::string& trace){
        if(!developmentOwner(context))return toolFailure("development_request_owner_required");const auto key=args.at("id").get<std::string>();if(key.size()!=44||!key.starts_with("dev-")||!std::all_of(key.begin()+4,key.end(),[](char c){return c>='0'&&c<='9'||c>='a'&&c<='f';}))return toolFailure("development_request_invalid_id");const auto row=get("ha.development",key);if(!row.revision)return toolFailure("development_request_not_found");
        if(!plugins||!plugins->enabled("mentor"))return toolSuccess(developmentReceipt(row.value));
        const auto status=plugins->invoke("mentor.forward_status",{{"request_id",key}},{});return persistDevelopmentForward(key,status,trace);
    }
    Json recordDevelopmentRequest(const Json& args,const ProposalContext& context,const std::string& trace){
        if(!developmentOwner(context))return toolFailure("development_request_owner_required");
        const auto sourceId=context.channelInputId.empty()?context.eventId:context.channelInputId;const auto source=context.sources.find(sourceId);
        if(source==context.sources.end()||source->second.kind!="user_message"||!visibility().source(source->second))return toolFailure("development_request_source_required");
        const auto key="dev-"+contentDigest(sourceId+args.dump()).substr(0,40);const auto existing=get("ha.development",key);
        if(!existing.revision){
            auto index=get("ha.development","index",Json::array());if(index.value.size()>=128)return toolFailure("development_request_capacity");
            const auto& s=source->second;Json value={{"id",key},{"title",args.at("title")},{"details",args.at("details")},{"status","pending"},{"target","mentor"},{"created_ms",serviceNowMs()},{"reply_route",contextReplyRoute(context)},{"sources",Json::array({{{"id",s.id},{"scope",s.scope},{"revision",s.revision},{"kind",s.kind}}})},{"trace_id",trace},{"developer_notified",false}};
            index.value.push_back(key);commit({{"ha.development",key,0,1,value.dump(),trace},mutation("ha.development","index",index,index.value,trace)},"development-request-recorded");
        }else if(existing.value.value("developer_notified",false)||existing.value.contains("forward_receipt"))return developmentRequestStatus({{"id",key}},context,trace);
        if(!plugins||!plugins->enabled("mentor"))return persistDevelopmentForward(key,toolFailure("mentor_not_enabled"),trace);
        plugins->event({{"kind","mentor.context"},{"turn_id",key},{"tools",Json::array()}});
        const auto result=plugins->invoke("mentor.forward",{{"goal",std::string("转交号主的小爱开发需求：")+args.at("title").get<std::string>()},{"context",Json{{"request_id",key},{"details",args.at("details")},{"source",{{"id",sourceId},{"channel",context.channelIdentity.empty()?"desktop":"qq"}}},{"instruction","请在现有导师会话中接收、整理并简短回应。此处仅转交需求，execute=false，不实施代码或电脑操作；不声称已经开发或部署。"}}.dump()}},{});
        return persistDevelopmentForward(key,result,trace);
    }
    Json developmentRequests()const{
        Json items=Json::array();for(const auto& key:get("ha.development","index",Json::array()).value){const auto row=get("ha.development",key);if(row.revision)items.push_back(row.value);}return {{"items",items},{"count",items.size()},{"storage","encrypted_archive"},{"target","mentor"},{"automatic_execution",false}};
    }
