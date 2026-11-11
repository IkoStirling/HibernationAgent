    static std::string chatTaskPhase(const Json& task,const std::string& gate){
        const auto status=task.at("status").get<std::string>();
        if(status!="ready"&&status!="running")return status;
        if(!gate.empty())return gate;
        if(continuous::budgetDeferred(task.value("deferred_reason",std::string{})))return "budget_wait";
        return status;
    }

    LocalReply chatTasks(){
        if(cfg.channel)return error(403,"chat_tasks_desktop_only");
        std::lock_guard guard(continuousTaskMutex);
        const auto index=get("ha.tasks","index",Json::array()).value;
        const bool enabled=cfg.continuousTasks.value("enabled",true);
        std::string gate;
        if(!enabled||runtimePaused)gate="runtime_paused";
        else if(runtimeOffline.load())gate="offline";
        else if(!index.empty()&&circadianAsleep())gate="resting";
        Json cards=Json::array(),recent=Json::array();
        for(auto it=index.rbegin();it!=index.rend();++it){
            const auto row=get("ha.tasks",*it);
            if(!row.revision)continue;
            const auto& task=row.value;
            Json card={{"id",task.at("id")},{"revision",row.revision},{"title",task.at("goal")},
                       {"status",task.at("status")},{"phase",chatTaskPhase(task,gate)},
                       {"steps_started",task.value("steps",0)}};
            if(task.at("status")=="blocked"){
                const auto issue=task.value("last_issue",std::string{});
                if(issue=="task_source_or_workspace_changed")card["notice"]="原任务的资料权限或工作区已变化。";
                else if(issue=="task_requires_two_tool_rounds")card["notice"]="当前工具轮数不足以执行并保存进度。";
                else if(issue=="task_no_progress")card["notice"]="连续工作段没有新的实际结果，自动执行已停止。";
                else if(task.value("steps",0)>=task.value("max_steps",24))card["notice"]="已达到这项目标的工作段上限。";
                else if(task.value("deadline_ms",std::int64_t(0))<=serviceNowMs())card["notice"]="已超过这项目标的执行期限。";
                else card["notice"]="这一段无法继续，可在对话中调整目标后再试。";
            }
            if(continuous::terminal(task.at("status").get<std::string>())){
                if(recent.size()<4)recent.push_back(std::move(card));
            }else cards.push_back(std::move(card));
        }
        const auto active=cards.size();
        for(auto& card:recent)cards.push_back(std::move(card));
        return reply(200,{{"enabled",enabled},{"paused",runtimePaused},
                          {"active_count",active},{"tasks",cards}});
    }

    LocalReply chatTaskDetails(const Json& body){
        if(cfg.channel)return error(403,"chat_tasks_desktop_only");
        if(!body.is_object()||!body.contains("id")||!body.at("id").is_string()||
           (body.size()!=1&&!(body.size()==2&&body.contains("artifact_id")&&body.at("artifact_id").is_string())))
            return error(400,"chat_task_arguments");
        const auto key=body.at("id").get<std::string>();
        const auto selected=body.value("artifact_id",std::string{});
        if(!continuous::safeId(key)||(body.contains("artifact_id")&&!continuous::safeId(selected)))
            return error(400,"chat_task_arguments");

        std::lock_guard guard(continuousTaskMutex);
        const auto task=get("ha.tasks",key);
        if(!task.revision)return error(404,"task_missing");
        if(task.value.at("bindings")!=taskBindings()||!taskAccessible(task.value))
            return error(409,"chat_task_sources_changed");
        const auto evidence=taskEvidence(key);
        const auto policy=visibility();
        Json artifacts=Json::array();
        std::set<std::string> paths;
        for(auto it=evidence.rbegin();it!=evidence.rend();++it){
            const auto source=taskSourceValue(*it);
            if(source.kind!="tool_observation"||source.scope!=archiveScope)continue;
            if(!selected.empty()&&source.id!=selected)continue;
            const auto row=get(source.scope,source.id);
            if(row.revision!=source.revision||!policy.source(source))continue;
            const auto tool=row.value.value("tool",std::string{});
            if(tool!="workspace.write"&&tool!="workspace.edit")continue;
            const auto result=row.value.value("result",Json::object());
            const auto path=result.value("path",std::string{});
            if(!result.value("written",false)||!safeRelative(path)||
               !(path.ends_with(".md")||path.ends_with(".txt")||path.ends_with(".json")))continue;
            if(!selected.empty()){
                std::string content,issue;
                if(!workspace.read("operator","drafts",path,content,issue)||!utf8WorkspaceText(content))
                    return error(404,"chat_task_artifact_unavailable");
                return reply(200,{{"path",path},{"content",content}});
            }
            if(paths.insert(path).second)artifacts.push_back({{"id",source.id},{"path",path}});
        }
        if(!selected.empty())return error(404,"chat_task_artifact_missing");
        return reply(200,{{"artifacts",artifacts}});
    }
