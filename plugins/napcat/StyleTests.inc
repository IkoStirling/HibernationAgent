void channelStyleTests(const std::filesystem::path& scratch){
    httplib::Server server;std::mutex gate;Json reviewData;int actorCalls=0,reviewCalls=0;bool rejectFirst=false;
    server.Post("/v1/chat/completions",[&](const auto& req,auto& res){
        std::lock_guard lock(gate);const auto body=Json::parse(req.body);Json output;
        if(body.at("model")=="style-review"){
            ++reviewCalls;reviewData=Json::parse(body.at("messages")[1].at("content").get<std::string>());
            output={{"decision",rejectFirst&&reviewCalls==1?"rewrite":"approve"},{"reason",rejectFirst&&reviewCalls==1?"other":"ok"},{"guidance",rejectFirst&&reviewCalls==1?"保留原意，再说一次。":""}};
        }else{++actorCalls;output={{"version",1},{"kind","reply"},{"text",actorCalls>1?"还没有核对兼容性。":"我不清楚，不敢瞎说。"},{"calls",Json::array()}};}
        res.set_content(Json{{"choices",Json::array({{{"message",{{"content",output.dump()}}},{"finish_reason","stop"}}})}}.dump(),"application/json");
    });
    const auto port=server.bind_to_any_port("127.0.0.1");std::thread thread([&]{server.listen_after_bind();});server.wait_until_ready();
    struct Cleanup{httplib::Server& server;std::thread& thread;~Cleanup(){server.stop();thread.join();}}cleanup{server,thread};
    const auto run=[&](const char* name,bool review,bool reject){
        actorCalls=reviewCalls=0;rejectFirst=reject;reviewData=nullptr;int edits=0;
        auto cfg=coreConfig(scratch/name);Profile actor;actor.id="actor";actor.model.model="style-actor";actor.model.protocol=ayt::inference::Protocol::ChatCompletions;actor.model.origin="http://127.0.0.1:"+std::to_string(port);actor.model.supportsStreaming=false;cfg.profiles.push_back(actor);
        auto reviewer=actor;reviewer.id="review";reviewer.model.model="style-review";cfg.profiles.push_back(reviewer);cfg.reviewEnabled=review;cfg.reviewProfileId="review";cfg.maxReviewRewrites=1;
        auto policy=std::make_shared<ChannelPolicy>();policy->selectedProfile="actor";policy->identity={{"channel","qq"},{"sender_user_id","20002"},{"sender_is_owner",false}};policy->stage={{"id",Json::parse(readFile(cfg.characterRoot/"stages.json")).at("initial")}};policy->roleState={{"energy",.42},{"activity","idle"}};policy->reserveModelCalls=[](int){return true;};
        policy->editStyle=[&](const Json& value,const std::function<bool()>&){++edits;check(value.at("text")=="我不清楚，不敢瞎说。","style callback gets this channel draft only");return toolSuccess({{"text","我不清楚。"},{"adapter_id","style-channel-fixture"}});};cfg.channel=policy;
        Application app(cfg);check(app.initialize(),"style channel initializes without desktop tools");call(app,"POST","/api/message",{{"request_id",name},{"text","它兼容吗？"}});Json result;
        pump(app,[&]{result=call(app,"POST","/api/channel/result",{{"request_id",name}});return !result.value("busy",true)&&(result.value("status",std::string{})=="completed"||result.value("status",std::string{})=="rejected");});
        if(result.at("status")!="completed"){std::ofstream evidence(scratch/(std::string(name)+"-failure.json"));evidence<<Json{{"result",result},{"review",reviewData},{"actor_calls",actorCalls},{"review_calls",reviewCalls},{"edits",edits}}.dump(2);std::cerr<<"STYLE DIAGNOSTIC "<<result.dump()<<'\n';}
        check(result.at("status")=="completed","style channel finishes");
        if(!review)check(edits==0&&result.at("text")=="我不清楚，不敢瞎说。","channel edit also requires independent review");
        else if(reject)check(edits==1&&reviewCalls==2&&result.at("text")=="还没有核对兼容性。","review rejection does not loop the style callback");
        else{check(edits==1&&result.at("text")=="我不清楚。","native channel callback edits final dialogue without computer authority");check(reviewData.at("style_edit").at("original_text")=="我不清楚，不敢瞎说。","QQ review sees original wording");}
        app.stop();
    };
    run("reviewed",true,false);run("no-review",false,false);run("rejected",true,true);
    std::cout<<"PASS native QQ style callback, required review, preserved original, bounded rewrite\n";
}
