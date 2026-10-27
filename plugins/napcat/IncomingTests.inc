void incomingTests(const std::filesystem::path& scratch){
    check(!imageAddress("http://127.0.0.1:9988/secret","http://127.0.0.1:3000"),"image URL cannot point to another local service");
    check(!imageAddress("https://qpic.cn.evil.invalid/image","http://127.0.0.1:3000"),"CDN host suffix is exact");
    check(static_cast<bool>(imageAddress("https://multimedia.nt.qq.com.cn/download?fileid=fixture","http://127.0.0.1:3000")),"QQ NT multimedia download host accepted");
    check(!imageAddress("https://multimedia.nt.qq.com.cn.evil.invalid/image","http://127.0.0.1:3000"),"lookalike QQ NT host rejected");
    check(!imageAddress("file:///C:/private.png","http://127.0.0.1:3000"),"received image cannot read arbitrary disk paths");
    check(static_cast<bool>(imageAddress("https://gchat.qpic.cn/image?key=fixture","http://127.0.0.1:3000")),"official QQ image URL accepted");
    // Synthetic two-frame GIF. The OneBot filename below deliberately ends in
    // .jpg: the actual decoder must identify content rather than its extension.
    const std::string png64="R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQADAAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBDAABACwAAAAAAgACAIEAAP8AAAAAAAAAAAAIBgABCAQQEAA7";
    DWORD size=0;CryptStringToBinaryA(png64.c_str(),0,CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr);std::string png(size,'\0');CryptStringToBinaryA(png64.c_str(),0,CRYPT_STRING_BASE64,reinterpret_cast<BYTE*>(png.data()),&size,nullptr,nullptr);
    httplib::Server server;std::mutex mutex;Json captured=Json::array(),sent=Json::array();int imageGets=0,refreshes=0;std::string origin;
    server.Post("/get_login_info",[](const auto&,auto& res){res.set_content(Json{{"status","ok"},{"retcode",0},{"data",{{"user_id","10001"}}}}.dump(),"application/json");});
    server.Get("/fixture-image",[&](const auto& req,auto& res){std::lock_guard lock(mutex);check(!req.has_header("Authorization"),"NapCat credential never sent on image GET");++imageGets;res.set_content(png,"image/gif");});
    server.Post("/get_image",[&](const auto&,auto& res){std::lock_guard lock(mutex);++refreshes;res.set_content(Json{{"status","ok"},{"retcode",0},{"data",{{"url",origin+"/fixture-image"},{"file","C:/do-not-open-fixture.png"}}}}.dump(),"application/json");});
    server.Post("/send_private_msg",[&](const auto& req,auto& res){std::lock_guard lock(mutex);sent.push_back(Json::parse(req.body));res.set_content(Json{{"status","ok"},{"retcode",0},{"data",{{"message_id",sent.size()}}}}.dump(),"application/json");});
    server.Post("/v1/chat/completions",[&](const auto& req,auto& res){const auto body=Json::parse(req.body);Json output;{std::lock_guard lock(mutex);captured.push_back(body);}
        if(body.at("model")=="reviewer")output={{"decision","approve"},{"reason","ok"},{"guidance",""}};
        else if(body.at("messages").size()==2){const auto input=Json::parse(body.at("messages")[1].at("content").get<std::string>());const auto images=input.at("channel_identity").at("received_images");
            if(!images.empty())output={{"version",1},{"kind","call"},{"text",nullptr},{"calls",Json::array({{{"id","read-image"},{"tool","napcat.view_image"},{"args",{{"id",images[0].at("id")}}}},{{"id","forged-image"},{"tool","napcat.view_image"},{"args",{{"id","qqimage-not-authorized"}}}},{{"id","local-state"},{"tool","state.observe"},{"args",Json::object()}}})}};
            else output={{"version",1},{"kind","reply"},{"text","没有本会话图片。"},{"calls",Json::array()}};
        }else output={{"version",1},{"kind","reply"},{"text","收到图片和文字。"},{"calls",Json::array()}};
        res.set_content(Json{{"choices",Json::array({{{"message",{{"content",output.dump()}}},{"finish_reason","stop"}}})}}.dump(),"application/json");
    });
    const auto port=server.bind_to_any_port("127.0.0.1");origin="http://127.0.0.1:"+std::to_string(port);std::thread listener([&]{server.listen_after_bind();});server.wait_until_ready();
    struct Cleanup{httplib::Server& server;std::thread& thread;~Cleanup(){server.stop();thread.join();}} cleanup{server,listener};
    _putenv_s("HA_NAPCAT_TEST_API","fixture-api-token-not-real");_putenv_s("HA_NAPCAT_TEST_EVENT","fixture-event-secret-not-real");
    const auto callback=freePort();const Json plugin={{"enabled",true},{"selfUserId","10001"},{"ownerUserId","20001"},{"allowedUsers","20002"},{"apiOrigin",origin},{"apiTokenEnvironment","HA_NAPCAT_TEST_API"},{"eventSecretEnvironment","HA_NAPCAT_TEST_EVENT"},{"callbackPort",callback},{"cooldownSeconds",2},{"mergeWindowMs",250},{"stricterSecurity",true}};
    auto cfg=coreConfig(scratch);cfg.pluginRoot=HA_PLUGIN_ROOT;cfg.plugins={{"napcat",plugin}};Profile actor;actor.id="actor";actor.model.protocol=ayt::inference::Protocol::ChatCompletions;actor.model.origin=origin;actor.model.model="actor";actor.model.supportsStreaming=false;actor.model.supportsVision=true;cfg.profiles.push_back(actor);auto reviewer=actor;reviewer.id="reviewer";reviewer.model.model="reviewer";cfg.profiles.push_back(reviewer);cfg.reviewEnabled=true;cfg.reviewProfileId="reviewer";
    auto app=std::make_unique<Application>(cfg);check(app->initialize(),"incoming media host initialized");call(*app,"POST","/api/model",{{"id","actor"}});
    auto post=[&](const Json& e){httplib::Client client("127.0.0.1",callback);const auto body=e.dump();auto response=client.Post("/onebot/events",{{"X-Signature",signature("fixture-event-secret-not-real",body)}},body,"application/json");check(response&&response->status==200,"burst event retained without cooldown rejection");};
    auto count=[&]{std::lock_guard lock(mutex);return sent.size();};
    auto picture=event("20001","700","QQ_IMAGE_MESSAGE");picture["message"].push_back({{"type","image"},{"data",{{"file","sticker.jpg"},{"url",origin+"/expired"},{"summary","fixture sticker"}}}});picture["message"].push_back({{"type","face"},{"data",{{"id","14"}}}});
    post(picture);post(event("20001","701","QQ_FOLLOWUP_TEXT_4812"));
    pump(*app,[&]{return count()==1&&!call(*app,"GET","/api/chat").value("busy",true);});
    {std::lock_guard lock(mutex);check(imageGets==1&&refreshes==1,"expired URL refreshed and actual image downloaded once");bool combined=false,actorImage=false,reviewImage=false,denied=false,state=false,animated=false;
        for(const auto& request:captured){if(request.at("messages").size()==2&&request.at("model")=="actor"){const auto input=Json::parse(request.at("messages")[1].at("content").get<std::string>());combined=input.at("input").get<std::string>().find("QQ_FOLLOWUP_TEXT_4812")!=std::string::npos&&input.at("input").get<std::string>().find("QQ表情 id=14")!=std::string::npos&&input.at("channel_identity").at("input_message_ids").size()==2;}
            for(const auto& m:request.at("messages"))if(m.at("content").is_array()){for(const auto& part:m.at("content"))if(part.value("type",std::string{})=="image_url"){if(request.at("model")=="reviewer")reviewImage=true;else actorImage=true;}if(request.at("model")=="actor"){const auto feedback=Json::parse(m.at("content")[0].at("text").get<std::string>());for(const auto& r:feedback.value("tool_results",Json::array())){if(r.value("result",Json::object()).value("source_frame_count",0)==2)animated=true;if(r.value("error",Json::object()).value("code",std::string{})=="qq_image_not_in_current_conversation")denied=true;if(r.value("call_id",std::string{})=="local-state"&&r.value("ok",false))state=true;}}}
        }check(combined&&actorImage&&reviewImage&&animated,"image, following text and face reach actor and reviewer as one batch");check(denied&&state,"image reader cannot cross identity; strict mode retains local state tools");
    }
    post(picture);post(event("20001","702","再看刚才的图"));pump(*app,[&]{return count()==2;});
    {std::lock_guard lock(mutex);check(imageGets==1,"following separate turn reuses encrypted image bytes");}
    post(event("20002","703","QQ_OTHER_SENDER_7391"));pump(*app,[&]{return count()==3;});
    {std::lock_guard lock(mutex);check(sent[2].at("message")[0].at("data").at("text")=="没有本会话图片。","another sender does not inherit recent image pointers");}
    app->stop();app.reset();
    const auto base=scratch/"channels/napcat/10001";bool rawImage=false,rawText=false,batched=false;
    for(const auto& row:rows(base/"transport","napcat")){const auto v=Json::parse(row.payload);if(v.value("message_id",std::string{})=="700"&&v.contains("attachments"))rawImage=true;if(v.value("message_id",std::string{})=="701"){rawText=v.at("text")=="QQ_FOLLOWUP_TEXT_4812";batched=v.value("status",std::string{})=="batched";}}
    check(rawImage&&rawText&&batched,"batching preserves both immutable original messages in encrypted ledger");
    bool source=false;for(const auto& row:rows(base/"private/20001","ha.archive")){const auto v=Json::parse(row.payload);if(v.value("tool",std::string{})=="napcat.view_image"&&v.value("result",Json::object()).contains("image"))source=true;}
    check(source,"actual incoming image has encrypted observation provenance");
    app=std::make_unique<Application>(cfg);check(app->initialize(),"incoming media host restart");post(event("20001","704","重启后继续看刚才的图"));pump(*app,[&]{return count()==4;});app->stop();
    {std::lock_guard lock(mutex);check(imageGets==1,"recent image lookup and cached pixels survive restart");}
    std::cout<<"PASS QQ image+text batching, sticker/face parsing, actual vision/review, refresh, authority, encrypted cache, restart\n";
}
