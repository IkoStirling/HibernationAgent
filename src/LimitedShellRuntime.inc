    Json limitedShellTool()const{return {{"name","shell.run"},{"description","受限命令行，只运行help、roots、pwd、list/ls/dir、cd、cat/type/read。沿用已授权目录，默认drafts:/；cd会保存本会话工作目录，重启可延续。路径含空格请加引号；根目录可用materials:/、drafts:/或roots返回的ID。只读文件和目录，不执行脚本、外部程序、管道、重定向或环境变量；真实能力以help及回执为准。"},{"parameters",{{"type","object"},{"additionalProperties",false},{"required",Json::array({"command"})},{"properties",{{"command",{{"type","string"},{"minLength",1},{"maxLength",1024}}}}}}}};}
    Json runLimitedShell(const Json& args,const ProposalContext& context,const std::string& trace,const std::function<bool()>& cancelled){
        if(!cfg.computer.value("enabled",true)||!cfg.computer.value("limitedShellEnabled",false)||!desktop)return toolFailure("limited_shell_disabled");
        const auto command=limited_shell::parse(args.at("command"));if(!command.ok)return toolFailure(command.error.c_str());
        const auto roots=desktop->capabilities().at("read_roots");const auto key="cwd-"+contentDigest(limited_shell::sessionRoute(contextReplyRoute(context)).dump()).substr(0,40);auto stored=get("ha.shell",key);
        auto cwd=stored.revision?stored.value:Json{{"root","drafts"},{"path",""}};const auto currentRoot=cwd.value("root",std::string{});
        if(!currentRoot.empty()&&std::none_of(roots.begin(),roots.end(),[&](const auto& row){return row.at("id")==currentRoot;}))cwd={{"root","drafts"},{"path",""}};
        if(command.name=="help")return toolSuccess({{"commands",Json::array({"help","roots","pwd","list [path]","cd [path]","cat path"})},{"cwd",limited_shell::display(cwd)},{"roots",roots},{"arbitrary_shell",false},{"filesystem_writes",false},{"text_read_limit_bytes",32768}});
        if(command.name=="roots")return toolSuccess({{"roots",roots},{"cwd",limited_shell::display(cwd)}});
        if(command.name=="pwd")return toolSuccess({{"cwd",limited_shell::display(cwd)},{"persisted",stored.revision>0}});
        auto target=limited_shell::resolve(command.path,cwd,roots);if(!target.ok)return toolFailure(target.error.c_str());
        if(command.name=="cd"&&command.path.empty())target={true,"drafts","",{}};
        Json destination={{"root",target.root},{"path",target.path}};
        if(target.root.empty()){
            if(command.name=="cat")return toolFailure("shell_file_required");
            if(command.name=="cd")commit({mutation("ha.shell",key,stored,destination,trace)},"limited-shell-cwd");return toolSuccess({{"cwd","/"},{"roots",roots},{"filesystem_writes",false}});
        }
        const auto name=command.name=="cat"?"computer.shell_read":"computer.list_directory";
        auto receipt=desktop->invoke(name,{{"root",target.root},{"path",target.path},{"limit",128}},cancelled);if(!receipt.value("ok",false))return receipt;
        if(command.name=="cd"){commit({mutation("ha.shell",key,stored,destination,trace)},"limited-shell-cwd");return toolSuccess({{"cwd",limited_shell::display(destination)},{"persisted",true},{"filesystem_writes",false}});}
        receipt["result"]["cwd"]=limited_shell::display(cwd);receipt["result"]["target"]=limited_shell::display(destination);receipt["result"]["command"]=command.name;receipt["result"]["filesystem_writes"]=false;return receipt;
    }
