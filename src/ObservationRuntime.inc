    void recordImageDelivery(const Json& trace,const std::vector<std::size_t>& attempted,const std::string& traceId){
        if(attempted.empty())return;std::lock_guard guard(observationMutex);std::map<std::string,std::pair<std::string,std::size_t>> sources;
        for(const auto i:attempted){const auto& receipt=trace.at("visual_inputs").at(i);const auto source=receipt.value("source_id",std::string{});if(source.empty())continue;auto& entry=sources[source];entry.first=receipt.at("delivery_state");++entry.second;}
        std::vector<RecordMutation> updates;auto recent=get("ha.observations","recent",Json::array());const auto now=serviceNowMs();
        for(const auto& [key,attempt]:sources){
            const auto before=get("ha.image-delivery",key);if(before.revision&&before.value.value("version",0)!=1)continue;
            const auto acknowledged=attempt.first=="acknowledged"||before.value.value("ever_acknowledged",false);
            updates.push_back(mutation("ha.image-delivery",key,before,Json{{"version",1},{"ever_acknowledged",acknowledged},{"last_state",attempt.first},{"last_attempt_ms",now},{"trace_id",traceId},{"attached_images",attempt.second}},traceId));
            if(!cfg.channel){for(auto it=recent.value.begin();it!=recent.value.end();)if(*it==key)it=recent.value.erase(it);else ++it;pushIndex(recent.value,key,24);}
        }
        if(!updates.empty()){if(!cfg.channel)updates.push_back(mutation("ha.observations","recent",recent,recent.value,traceId));commit(updates,traceId);}
    }
    void addRecentObservations(const Json& recentIds,std::vector<ayt::inference::ContextFragment>& fragments,ProposalContext& context){
        if(cfg.channel)return;const auto policy=visibility();const auto rows=readIds(archiveScope,recentIds);
        struct Candidate {std::string id;Doc source;std::string delivery;std::int64_t recentMs=0;};std::vector<Candidate> candidates;std::set<std::string> discovered;std::map<std::string,Doc> receiptTraces;
        const auto add=[&](const std::string& sourceId,std::string delivery,std::int64_t recentMs=0){
            if(sourceId.empty()||context.sources.contains(sourceId))return;
            if(discovered.contains(sourceId)){for(auto& candidate:candidates)if(candidate.id==sourceId){if(delivery=="acknowledged")candidate.delivery=delivery;candidate.recentMs=std::max(candidate.recentMs,recentMs);}return;}
            if(candidates.size()>=32||discovered.size()>=64)return;discovered.insert(sourceId);
            const auto source=get(archiveScope,sourceId);if(!source.revision)return;
            const auto kind=source.value.value("kind",std::string{});if((kind!="tool_observation"&&kind!="tool_intent"&&kind!="tool_attempt")||!policy.source(Source{sourceId,source.revision,kind,archiveScope}))return;
            const auto traceId=source.value.value("trace_id",std::string{});
            if(delivery=="no_saved_receipt"&&!traceId.empty()){
                auto found=receiptTraces.find(traceId);if(found==receiptTraces.end())found=receiptTraces.emplace(traceId,get("ha.trace",traceId)).first;
                if(found->second.revision)delivery=observedImageDelivery(found->second.value,sourceId);
            }
            const auto& value=source.value.at("result");
            if(value.contains("image")||value.contains("frames")){const auto last=get("ha.image-delivery",sourceId);if(last.revision&&last.value.value("version",0)==1){delivery=last.value.value("ever_acknowledged",false)?"acknowledged":last.value.value("last_state",std::string("unknown"));recentMs=std::max(recentMs,last.value.value("last_attempt_ms",std::int64_t(0)));}}
            recentMs=std::max(recentMs,source.value.value("created_at",std::int64_t(0)));candidates.push_back({sourceId,source,std::move(delivery),recentMs});
        };
        const auto ownRecent=get("ha.observations","recent",Json::array()).value;
        for(auto observation=ownRecent.rbegin();observation!=ownRecent.rend();++observation)add(observation->get<std::string>(),"no_saved_receipt");
        for(const auto& activity:activityQueue().value("recent",Json::array()))add(activity.value("source_id",std::string{}),"no_saved_receipt");
        std::size_t traces=0;for(auto row=rows.rbegin();row!=rows.rend()&&candidates.size()<32&&traces<8;++row){
            if(!policy.source(Source{row->id,row->revision,"assistant_message",archiveScope}))continue;
            const auto dialogue=Json::parse(row->payload);if(dialogue.value("kind",std::string{})!="assistant_message")continue;
            const auto traceId=dialogue.value("trace_id",std::string{});if(traceId.empty())continue;const auto trace=get("ha.trace",traceId);if(!trace.revision)continue;
            ++traces;
            const auto calls=trace.value.value("tool_results",Json::array());
            for(auto call=calls.rbegin();call!=calls.rend();++call){
                if(!call->value("ok",false))continue;auto sourceId=call->value("source_id",std::string{});
                if(sourceId.empty()){
                    const auto identity=trace.value.value("turn_identity",dialogue.value("request_id",std::string{}));
                    if(!identity.empty()){const auto previous=get("ha.calls",identity+"."+call->at("call_id").get<std::string>());if(previous.revision)sourceId=previous.value.value("output",Json::object()).value("source_id",std::string{});}
                }
                add(sourceId,observedImageDelivery(trace.value,sourceId),trace.value.value("created_at",std::int64_t(0)));
            }
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.recentMs==b.recentMs?a.id<b.id:a.recentMs>b.recentMs;});
        if(candidates.size()>4)candidates.resize(4);
        for(const auto& candidate:candidates){const auto& source=candidate.source;const auto compact=compactObservation(source.value,candidate.delivery=="acknowledged",candidate.delivery);fragments.push_back({candidate.id,compact.dump(),"archive@"+std::to_string(source.revision),72});context.sources[candidate.id]={candidate.id,source.revision,source.value.at("kind"),archiveScope};}
    }
