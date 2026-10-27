    std::unique_ptr<SkillRegistry> skillRegistry;
    static bool skillControl(const std::string& name){return name.starts_with("skills.")||name=="capabilities.list";}
    bool skillAllows(const ProposalContext& context,const std::string& name)const{return context.skillId.empty()||skillControl(name)||context.skillTools.contains(name);}
    Json scopedTools(const ProposalContext& context)const{auto value=tools();for(auto it=value.begin();it!=value.end();)if(!skillAllows(context,it->at("name").template get<std::string>()))it=value.erase(it);else ++it;return value;}
    Json skillTool(const std::string& name,const Json& args,ProposalContext& context){
        if(!skillRegistry)return toolFailure("skills_unavailable");
        if(name=="skills.list")return toolSuccess({{"skills",skillRegistry->catalog(tools())},{"active",context.skillId},{"scripts_executable",false}});
        if(name=="skills.deactivate"){context.skillId.clear();context.skillDigest.clear();context.skillTools.clear();return toolSuccess({{"active",nullptr}});}
        if(name=="skills.read")return skillRegistry->read(args.at("id"),args.value("path",std::string("SKILL.md")));
        std::set<std::string> granted;const auto key=args.at("id").get<std::string>(),digest=args.at("digest").get<std::string>();
        if(!skillRegistry->authorize(key,digest,granted))return toolFailure("skill_not_approved_or_changed");
        context.skillId=key;context.skillDigest=digest;context.skillTools=std::move(granted);
        auto guide=skillRegistry->read(key);guide["result"]["active"]=key;Json effective=Json::array();for(const auto& tool:scopedTools(context))effective.push_back(tool.at("name"));guide["result"]["effective_tools"]=effective;
        guide["result"]["scope"]="this_turn_only";return guide;
    }
    bool restoreSkill(const std::string& name,const Json& args,ProposalContext& context){
        if(name=="skills.deactivate"){context.skillId.clear();context.skillDigest.clear();context.skillTools.clear();return true;}
        if(name!="skills.activate")return true;
        std::set<std::string> tools;if(!skillRegistry||!skillRegistry->authorize(args.at("id"),args.at("digest"),tools))return false;
        context.skillId=args.at("id");context.skillDigest=args.at("digest");context.skillTools=std::move(tools);return true;
    }
