    mutable std::recursive_mutex continuityMutex;
    Json channelConversationFocus=Json::object();
    bool sharedOwner()const{return !cfg.channel||cfg.channel->sharedOwner;}
    bool ownerChannel(const Json& who)const{return napcatIdentityAllowed(who)&&who.value("sender_is_owner",false)&&who.value("message_type",std::string{})=="private"&&!ChannelAccess::read(cfg.plugins.at("napcat")).isolateMemory;}
    std::vector<SharedMemoryRoot> ownerRoots()const{
        if(!sharedOwner())return {};const auto main=cfg.channel?cfg.channel->sharedMemoryRoot:cfg.dataRoot;if(main.empty())return {};
        const auto roots=sharedMemoryRoots(main,cfg.dataRoot);std::filesystem::path owner;
        if(cfg.channel)owner=cfg.dataRoot;else{const auto p=cfg.plugins.value("napcat",Json::object());if(!p.value("selfUserId",std::string{}).empty()&&!p.value("ownerUserId",std::string{}).empty()&&!ChannelAccess::read(p).isolateMemory)owner=main/"channels/napcat"/p.value("selfUserId",std::string{})/"private"/p.value("ownerUserId",std::string{});}
        std::vector<SharedMemoryRoot> selected;const auto mainTag=memoryRootTag(main),ownerTag=owner.empty()?std::string{}:memoryRootTag(owner);
        for(const auto& root:roots)if(root.tag==mainTag||root.tag==ownerTag)selected.push_back(root);return selected;
    }
    bool continuitySource(const Source& source)const{
        if(source.kind!="user_message")return false;try{const auto row=get(source.scope,source.id);if(!row.revision||row.revision!=source.revision||row.value.value("kind",std::string{})!="user_message")return false;const auto who=row.value.value("channel_identity",Json::object());if(!who.empty()){if(!who.value("sender_is_owner",false)||who.value("message_type",std::string{})!="private")return false;if(!cfg.channel){const auto options=cfg.plugins.value("napcat",Json::object());if(ChannelAccess::read(options).isolateMemory||who.value("self_user_id",std::string{})!=options.value("selfUserId",std::string{})||who.value("sender_user_id",std::string{})!=options.value("ownerUserId",std::string{}))return false;}}const auto policy=visibility();return policy.source(source);}catch(...){return false;}
    }
    Json continuityView(const std::string& id={})const{
        if(cfg.channel)return cfg.channel->sharedUnderstanding;
        std::lock_guard lock(continuityMutex);Json entries=Json::array();std::size_t total=0,bytes=0;
        const auto index=get("ha.continuity","index",Json::array()).value;
        for(const auto& key:index){if(!id.empty()&&key!=id)continue;const auto row=get("ha.continuity",key);if(!row.revision)continue;const auto ref=row.value.at("source");if(!continuitySource({ref.at("id"),ref.at("revision"),"user_message",ref.at("scope")}))continue;++total;
            auto entry=row.value;entry["revision"]=row.revision;entry.erase("source");const auto cost=entry.dump().size();if(id.empty()&&(entries.size()>=32||bytes+cost>18000))continue;bytes+=cost;entries.push_back(std::move(entry));
        }return {{"entries",entries},{"accessible_count",total},{"omitted_count",total-entries.size()},{"shared_owner",true},{"guide","精确原句与有效状态按来源持久保留。修改/撤回使用现有id和版本；不是自动修改人格或增加权限。"}};
    }
    Json continuityNote(const Json& args,ProposalContext& context,const std::string& traceId){
        if(!sharedOwner()||context.eventKind!="user_message"||!context.taskId.empty()||!context.skillId.empty())return toolFailure("continuity_owner_conversation_required");
        if(!ayt::serializer::validateJsonDocument(args.dump(),continuity::noteParameters().dump()))return toolFailure("invalid_continuity_note");
        const auto key=args.at("id").get<std::string>();if(!continuous::safeId(key)||key.size()>64)return toolFailure("continuity_identity");
        const auto sourceId=args.at("source_id").get<std::string>();const auto known=context.sources.find(sourceId);if(known==context.sources.end()||!continuitySource(known->second))return toolFailure("continuity_source_unavailable");
        const auto raw=get(known->second.scope,sourceId);const auto quote=args.at("quote").get<std::string>();if(quote.find_first_not_of(" \t\r\n")==std::string::npos||raw.value.at("text").get_ref<const std::string&>().find(quote)==std::string::npos)return toolFailure("continuity_quote_mismatch");
        std::lock_guard lock(continuityMutex);const auto before=get("ha.continuity",key);const auto stamp=raw.value.value("created_at",std::int64_t(0));
        if(before.revision&&before.value.value("source_created_ms",std::int64_t(0))>stamp)return toolFailure("continuity_newer_source_exists");
        auto status=args.at("status").get<std::string>();if(args.at("kind")=="persona_proposal"&&status=="active")status="pending";
        if(before.revision&&before.value.at("quote")==quote&&before.value.at("meaning")==args.at("meaning")&&before.value.at("status")==status&&before.value.at("source").at("id")==sourceId)return toolSuccess({{"id",key},{"revision",before.revision},{"deduplicated",true}});
        if(args.at("expected_revision")!=before.revision)return toolFailure("continuity_revision_conflict");
        auto index=get("ha.continuity","index",Json::array());if(!before.revision&&index.value.size()>=64)return toolFailure("continuity_capacity");
        const Json value={{"id",key},{"kind",args.at("kind")},{"status",status},{"quote",quote},{"meaning",args.at("meaning")},{"source",{{"id",sourceId},{"revision",known->second.revision},{"scope",known->second.scope}}},{"source_created_ms",stamp},{"updated_ms",serviceNowMs()}};
        std::vector<RecordMutation> changes={mutation("ha.continuity",key,before,value,traceId)};if(!before.revision){index.value.push_back(key);std::sort(index.value.begin(),index.value.end());changes.push_back(mutation("ha.continuity","index",index,index.value,traceId));}commit(std::move(changes),"owner-continuity-note");
        return toolSuccess({{"id",key},{"revision",before.revision+1},{"status",status},{"source_retained",true},{"exact_quote_retained",true}});
    }
    Json applyContinuityReview(const Json& updates,ProposalContext& context,const std::string& traceId){
        if(cfg.channel&&cfg.channel->sharedOwner&&cfg.channel->invokeDesktop)return cfg.channel->invokeDesktop("continuity.review",{{"updates",updates}},context,traceId,{});
        Json outcomes=Json::array();if(!updates.is_array()||updates.size()>8)return outcomes;for(const auto& update:updates){const auto result=continuityNote(update,context,traceId);outcomes.push_back({{"id",update.value("id",std::string{})},{"ok",result.value("ok",false)},{"error",result.value("error",Json(nullptr))}});}return outcomes;
    }
    Json continuityHidden()const{
        Json result=Json::array();const auto tag=memoryRootTag(cfg.dataRoot);for(const auto& key:visibility().archives)result.push_back(sharedMemoryId(key)?key:tag+key);return result;
    }
    Json continuityExcluded()const{
        Json rows=Json::array();const auto tag=memoryRootTag(cfg.dataRoot);for(const auto& key:get("ha.context","dialogue").value.value("excluded_assistants",Json::array()))rows.push_back(sharedMemoryId(key.get<std::string>())?key.get<std::string>():tag+key.get<std::string>());return rows;
    }
    void backfillContinuityPackets(){
        const auto before=get("ha.continuity-packets","index",Json::array());auto keys=before.value;
        for(const auto& row:readIds(archiveScope,get("ha.index","recent",Json::array()).value)){const auto value=Json::parse(row.payload);if(value.value("kind",std::string{})=="user_message"&&value.value("text",std::string{}).size()>=1000&&std::find(keys.begin(),keys.end(),row.id)==keys.end()){keys.push_back(row.id);while(keys.size()>64)keys.erase(keys.begin());}}
        if(keys!=before.value)commit({mutation("ha.continuity-packets","index",before,keys)},"long-original-packets-indexed");
    }
    Json continuityStats()const{
        const auto view=continuityView();Json result={{"accessible_count",view.value("accessible_count",0)},{"active",0},{"revoked",0},{"pending",0},{"exact_sources",true}};
        for(const auto& row:view.value("entries",Json::array()))if(result.contains(row.at("status").get<std::string>()))result[row.at("status").get<std::string>()]=result.at(row.at("status").get<std::string>()).get<int>()+1;return result;
    }
    Json ownerDialogue(std::vector<ayt::inference::ContextFragment>& fragments,ProposalContext& context,const std::string& eventId,std::set<std::string>& pinned)const{
        Json packets=Json::array();if(!sharedOwner())return packets;const auto roots=ownerRoots();if(roots.empty())return packets;
        auto all=continuity::dialogue(roots,cfg.dataRoot);const auto hidden=cfg.channel?cfg.channel->sharedHiddenSources:continuityHidden();const auto excluded=cfg.channel?cfg.channel->sharedDialogueExcluded:continuityExcluded();const auto currentTag=memoryRootTag(cfg.dataRoot);auto revoked=[&](const auto& row){const auto key=sharedMemoryId(row.id)?row.id:currentTag+row.id;return std::find(hidden.begin(),hidden.end(),key)!=hidden.end()||std::find(excluded.begin(),excluded.end(),key)!=excluded.end();};std::erase_if(all,revoked);if(all.size()>cfg.recentMessageCount)all.erase(all.begin(),all.end()-cfg.recentMessageCount);
        std::set<std::string> recent;for(const auto& row:all)recent.insert(row.id);
        std::erase_if(fragments,[&](const auto& f){const auto found=context.sources.find(f.id);return f.id!=eventId&&!pinned.contains(f.id)&&found!=context.sources.end()&&(found->second.kind=="user_message"||found->second.kind=="assistant_message")&&!recent.contains(f.id);});
        auto add=[&](const continuity::DialogueRow& row,int priority){auto value=row.data;if(value.at("kind")=="assistant_message")value=boundedCausalDialogue(std::move(value));const auto found=std::find_if(fragments.begin(),fragments.end(),[&](const auto& f){return f.id==row.id;});ayt::inference::ContextFragment f{row.id,value.dump(),row.scope+"@"+std::to_string(row.revision),priority};if(found!=fragments.end())*found=std::move(f);else fragments.push_back(std::move(f));context.sources[row.id]={row.id,row.revision,row.data.at("kind"),row.scope};};
        int priority=90;for(auto it=all.rbegin();it!=all.rend();++it)add(*it,it->id==eventId?100:std::max(75,priority--));
        // Preserve exact long original packets outside the rolling history window.
        auto longRows=continuity::dialogue(roots,cfg.dataRoot,true);std::erase_if(longRows,revoked);std::size_t admitted=0,used=0;for(auto it=longRows.rbegin();it!=longRows.rend()&&admitted<2;++it){const auto cost=it->data.dump().size();if(used+cost>16000)continue;used+=cost;++admitted;add(*it,it->id==eventId?100:97);pinned.insert(it->id);packets.push_back({{"source_id",it->id},{"revision",it->revision},{"original_bytes",it->data.at("text").get_ref<const std::string&>().size()},{"meaning","原始长交谈仍可完整核对；未自动当成系统指令，明确约定应逐项准确保留。"}});}
        return packets;
    }
    void ownerChronology(std::vector<ayt::inference::ContextFragment>& fragments)const{
        if(!sharedOwner())return;std::map<std::string,std::int64_t> stamps;for(const auto& f:fragments){const auto v=Json::parse(f.text);const auto kind=v.value("kind",std::string{});if(kind=="user_message"||kind=="assistant_message")stamps[f.id]=v.value("created_at",std::int64_t(0));}
        std::stable_sort(fragments.begin(),fragments.end(),[&](const auto&a,const auto&b){const auto ai=stamps.find(a.id),bi=stamps.find(b.id);if(ai==stamps.end()||bi==stamps.end())return ai!=stamps.end()&&bi==stamps.end();return ai->second!=bi->second?ai->second<bi->second:a.id<b.id;});
    }
    Json ownerQuestions()const{
        auto rows=openQuestions();const auto tag=memoryRootTag(cfg.dataRoot);for(auto& row:rows)for(auto& ref:row["sources"]){if(!sharedMemoryId(ref.at("id"))&&(ref.at("scope")==archiveScope||ref.at("scope")==memoryScope)){ref["id"]=tag+ref.at("id").get<std::string>();ref["scope"]=tag+ref.at("scope").get<std::string>();}}return rows;
    }
    Json ownerFocusSource()const{
        const auto focus=get("ha.dialogue","focus");if(!focus.revision||!focus.value.contains("source_id"))return Json::object();auto key=focus.value.at("source_id").get<std::string>();auto scope=std::string(archiveScope);if(sharedMemoryId(key))scope=key.substr(0,24)+archiveScope;else{const auto tag=memoryRootTag(cfg.dataRoot);key=tag+key;scope=tag+scope;}return {{"id",key},{"revision",focus.value.at("source_revision")},{"kind","user_message"},{"scope",scope}};
    }
    Json ownerAccessFence()const{
        if(!sharedOwner())return nullptr;if(!cfg.channel)return {{"revision",get("ha.access","current").revision},{"dialogue",get("ha.context","dialogue").revision}};
        try{const auto root=cfg.channel->sharedMemoryRoot;if(!std::filesystem::is_regular_file(root/"runtime/memory-key.dpapi"))return {{"unavailable",true}};RecordStore owner;if(!owner.open(root/"archive/records.sqlite",{openVaultCodec(root),false,false}))return {{"unavailable",true}};RecordQuery q;q.scope="ha.access";q.id="current";const auto rows=owner.query({{"ha.access"}},q);if(!rows||rows.records.empty())return {{"unavailable",true}};RecordQuery d;d.scope="ha.context";d.id="dialogue";const auto dialogue=owner.query({{"ha.context"}},d);return {{"revision",rows.records.front().revision},{"dialogue",dialogue&&!dialogue.records.empty()?dialogue.records.front().revision:0}};}catch(...){return {{"unavailable",true}};}
    }
