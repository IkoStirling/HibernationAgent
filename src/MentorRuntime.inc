    Json executeMentor(const Json& args,ProposalContext& context,const std::string& traceId,const std::function<bool()>& cancelled){
        if(!plugins||!plugins->enabled("mentor")||!cfg.plugins.value("mentor",Json::object()).value("allowExecution",true))return toolFailure("mentor_execution_disabled");
        // Persist intent before asking/executing. An interrupted operation is
        // uncertain and must never be automatically executed again on restart.
        NativeExecutionIds identity;const auto digest=identity.next("mentor.execute",args);if(!digest)return toolFailure("mentor_request_identity");
        const auto key=(context.eventId.empty()?traceId:context.eventId)+"."+*digest;auto saved=get("ha.mentor-execution",key);
        if(saved.revision){
            if(saved.value.contains("final")){auto replay=saved.value.at("final");replay["replayed_execution"]=true;return replay;}
            return {{"ok",false},{"error",{{"code","mentor_execution_interrupted"},{"retryable",false}}},{"execution",saved.value.value("receipts",Json::array())},{"operations_may_have_executed",saved.value.value("phase",std::string{})=="executing"},{"guide","上次委托被中断。已返回保存的真实回执，不重放可能已发生的操作；先观察当前情况再决定。"}};
        }
        auto state=Json{{"version",1},{"phase","consulting"},{"receipts",Json::array()},{"started_ms",serviceNowMs()},{"trace_id",traceId}};
        commit({mutation("ha.mentor-execution",key,saved,state,traceId)},traceId);
        Json access=Json::object();if(desktop){const auto capabilities=desktop->capabilities();for(const auto* field:{"applications","read_roots","screen_capture","application_capture","shell_access"})if(capabilities.contains(field))access[field]=capabilities.at(field);}
        plugins->event({{"kind","mentor.context"},{"turn_id",context.eventId.empty()?traceId:context.eventId},{"tools",tools()},{"execution_authorized",true},{"computer_access",access}});
        auto plan=plugins->invoke("mentor.execute",args,cancelled);
        const auto persist=[&](const Json& final){auto before=get("ha.mentor-execution",key);state["phase"]="finished";state["finished_ms"]=serviceNowMs();state["final"]=final;commit({mutation("ha.mentor-execution",key,before,state,traceId)},traceId);return final;};
        if(!plan.value("ok",false))return persist(plan);
        const auto& proposal=plan.at("result");state["plan"]=proposal;const auto steps=proposal.value("proposals",Json::array());
        for(const auto& step:steps)if(!mentor::operational(step.at("tool"))||directPresentationTool(step.at("tool")))return persist(toolFailure("mentor_operation_not_allowed"));
        bool pending=false,interrupted=false,uncertain=false;std::size_t attempted=0,succeeded=0,immediate=0;
        for(const auto& step:steps){
            if(cancelled&&cancelled()){interrupted=true;break;}
            const auto name=step.at("tool").get<std::string>();if(!mentor::operational(name)||directPresentationTool(name))return persist(toolFailure("mentor_operation_not_allowed"));
            state["phase"]="executing";state["next_step"]=attempted;auto before=get("ha.mentor-execution",key);commit({mutation("ha.mentor-execution",key,before,state,traceId)},traceId);
            Json receipt;try{receipt=performTool(name,step.at("args"),context,traceId,cancelled);}catch(...){receipt=toolFailure("mentor_operation_outcome_unknown");uncertain=true;}++attempted;const bool ok=receipt.value("ok",false);if(ok)++succeeded;
            const bool deferred=ok&&mentor::pendingEffect(receipt.value("result",Json::object()));pending=pending||deferred;if(ok&&!deferred)++immediate;
            if(ok&&receipt.contains("result"))receipt["result"]=referencedObservationContext(Json{{"kind","tool_observation"},{"result",receipt.at("result")}}).at("result");
            state["receipts"].push_back({{"step",attempted},{"tool",name},{"receipt",receipt}});state["phase"]="step_recorded";before=get("ha.mentor-execution",key);commit({mutation("ha.mentor-execution",key,before,state,traceId)},traceId);
            if(!ok)break;
        }
        const bool all=attempted==steps.size()&&succeeded==steps.size()&&!interrupted;
        auto result=Json{{"delegated",true},{"advisory_only",false},{"operations_executed",immediate>0},{"operations_dispatched",attempted>0},{"operations_may_have_executed",uncertain},{"steps_attempted",attempted},{"steps_succeeded",succeeded},{"all_steps_succeeded",all&&!steps.empty()},{"has_pending_effects",pending},{"cancelled",interrupted},{"receipts",state.at("receipts")},{"mentor_status",proposal.at("status")},{"mentor_summary",proposal.at("summary")},{"missing_capability",proposal.at("missing_capability")},{"task_completion","judge_from_actual_receipts"},{"guide","导师已通过应用在本机尝试所列操作，执行事实以每步receipt为准。导师总结不是完成证据；排队/播放/启动请求不代表整个任务完成。截图像素仍在各步原始source_id，需要observation.revisit实际附图才能声称看见。你仍决定怎样回应，不照抄内部步骤或编号。"}};
        return persist(toolSuccess(std::move(result)));
    }
