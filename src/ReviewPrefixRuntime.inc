    std::mutex reviewPrefixMutex;
    Json reviewPrefixFence()const{return {{"authority",projectionFence()},{"configuration",contentDigest(Json::array({cfg.plugins,cfg.computer,cfg.contextControl}).dump())}};}
    bool reviewPrefixSourcesAllowed(const Json& refs)const{return review_prefix::allowed(refs,[this](const Json& ref){return projectionSourceAllowed(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")});});}
    review_prefix::Prepared prepareReviewPrefix(const Profile& reviewer,ayt::inference::Request& request,const Json& refs,const std::string& category){
        std::lock_guard lock(reviewPrefixMutex);
        auto identity=cfg.channel?cfg.channel->identity:Json::object();for(const auto* field:{"received_images","current_input_images","input_message_ids","message_id"})identity.erase(field);
        const auto key="prefix-"+contentDigest(Json::array({reviewer.id,reviewer.model.origin,reviewer.model.basePath,reviewer.model.model,static_cast<int>(reviewer.model.protocol),reviewer.model.reasoningEffort,category,identity,contentDigest(request.messages.front().text),contentDigest(request.outputSchema)}).dump());
        auto index=get("ha.review-prefixes","active",Json::array());
        const auto before=get("ha.review-prefixes",key);
        const bool indexed=std::find(index.value.begin(),index.value.end(),key)!=index.value.end();
        auto prepared=review_prefix::prepare(request,indexed?before.value:Json::object(),reviewPrefixFence(),refs,[this](const Json& ref){return projectionSourceAllowed(Source{ref.at("id"),ref.at("revision"),ref.at("kind"),ref.at("scope")});},serviceNowMs());
        if(prepared.persist){std::erase(index.value.get_ref<Json::array_t&>(),Json(key));index.value.push_back(key);while(index.value.size()>16)index.value.erase(index.value.begin());commit({mutation("ha.review-prefixes",key,before,prepared.state,"review-prefix"),mutation("ha.review-prefixes","active",index,index.value,"review-prefix")},"review-prefix");}
        return prepared;
    }
    bool reviewPrefixStillAllowed(const review_prefix::Prepared& prepared)const{return prepared.fence==reviewPrefixFence()&&reviewPrefixSourcesAllowed(prepared.sources);}
    bool reviewPrefixEnabled(const Profile& actor)const{const auto policy=ContextControl::read(callsFor(actor).context);return policy.enabled&&policy.reviewLayout=="prefix";}
