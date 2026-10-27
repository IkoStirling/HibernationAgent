    std::vector<SharedMemoryRoot> sharedRoots()const{
        if(cfg.channel)return sharedMemoryRoots(cfg.channel->sharedMemoryRoot,cfg.dataRoot);
        const auto options=cfg.plugins.value("napcat",Json::object());
        if(!options.value("enabled",false)||ChannelAccess::read(options).isolateMemory)return {};
        return sharedMemoryRoots(cfg.dataRoot,cfg.dataRoot);
    }
    void sharedSources(const Json& rows,ProposalContext& context)const{
        for(const auto& row:rows)context.sources[row.at("id").get<std::string>()]={row.at("id"),row.at("revision"),row.at("kind"),row.at("scope")};
    }
    Json sharedLibraryTool(const std::string& name,const Json& args,ProposalContext& context,const Json& local,const std::function<bool()>& cancelled)const{
        if(name!="memory.search"&&name!="memory.archive_search"&&name!="memory.read"&&name!="memory.archive_read")return toolFailure("shared_memory_read_only");
        const bool archive=name=="memory.archive_search"||name=="memory.archive_read",search=name=="memory.search"||name=="memory.archive_search";
        const auto key=args.value("id",std::string{});if(!search&&!sharedMemoryId(key))return local;
        const auto roots=sharedRoots();if(roots.empty())return search?local:toolFailure("shared_memory_not_accessible");
        const auto external=querySharedMemory(roots,archive,args.value("query",std::string{}),key,args.value("limit",8),args.value("offset",std::size_t(0)),args.value("length",std::size_t(2400)),cancelled);
        const auto field=archive?"records":"memories";
        if(!search){if(external.rows.empty())return toolFailure("shared_memory_not_accessible");sharedSources(external.rows,context);return toolSuccess({{field,external.rows},{"storage","encrypted_sqlite"},{"separate_archives",true},{"read_only_originals",true}});}
        auto output=local;auto& values=output["result"][field];for(const auto& row:external.rows)values.push_back(row);
        const auto query=args.value("query",std::string{});std::stable_sort(values.begin(),values.end(),[&](const auto& a,const auto& b){const auto score=[&](const auto& v){const auto content=memorySearchText(v);return memoryRelevance(query,content)+(query.empty()?0:content.find(query)!=std::string::npos?50:0);};return score(a)>score(b);});
        const auto limit=static_cast<std::size_t>(args.value("limit",8));while(values.size()>limit)values.erase(values.end()-1);
        for(const auto& row:values)if(sharedMemoryId(row.at("id")))sharedSources(Json::array({row}),context);
        output["result"]["shared_archives"]=true;output["result"]["shared_query_limited"]=external.limited;return output;
    }
    void addSharedContext(std::vector<ayt::inference::ContextFragment>& fragments,ProposalContext& context,const std::string& query)const{
        const auto roots=sharedRoots();if(roots.empty())return;
        auto memories=querySharedMemory(roots,false,query,{},6);auto recent=querySharedMemory(roots,true,query,{},4);
        // Shared recall follows the current topic. Originals remain available
        // through explicit authorized search, including empty-query browsing.
        for(const auto* rows:{&memories.rows,&recent.rows})for(const auto& row:*rows){const auto key=row.at("id").get<std::string>();if(context.sources.contains(key))continue;
            fragments.push_back({key,row.dump(),row.at("scope").get<std::string>()+"@"+std::to_string(row.at("revision").get<std::int64_t>()),rows==&memories.rows?65:62});}
        sharedSources(memories.rows,context);sharedSources(recent.rows,context);
    }
