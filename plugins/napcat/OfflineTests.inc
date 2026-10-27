void offlineTests(const std::filesystem::path& scratch){
    FakeBot bot;_putenv_s("HA_NAPCAT_TEST_API","fixture-api-token-not-real");_putenv_s("HA_NAPCAT_TEST_EVENT","fixture-event-secret-not-real");
    const int port=freePort();auto cfg=coreConfig(scratch);cfg.pluginRoot=HA_PLUGIN_ROOT;
    cfg.plugins={{"napcat",{{"enabled",true},{"selfUserId","10001"},{"ownerUserId","20001"},{"apiOrigin","http://127.0.0.1:"+std::to_string(bot.port)},{"apiTokenEnvironment","HA_NAPCAT_TEST_API"},{"eventSecretEnvironment","HA_NAPCAT_TEST_EVENT"},{"callbackPort",port},{"cooldownSeconds",1},{"mergeWindowMs",0}}}};
    Profile actor;actor.id="actor";actor.model.protocol=ayt::inference::Protocol::ChatCompletions;actor.model.origin="http://127.0.0.1:"+std::to_string(bot.port);actor.model.model="actor";actor.model.supportsStreaming=false;cfg.profiles.push_back(actor);
    auto app=std::make_unique<Application>(cfg);check(app->initialize(),"offline host initialized");call(*app,"POST","/api/model",{{"id","actor"}});
    auto post=[&](const char* mid){httplib::Client client("127.0.0.1",port);const auto body=event("20001",mid,"OFFLINE_FIXTURE_INPUT").dump();const auto response=client.Post("/onebot/events",{{"X-Signature",signature("fixture-event-secret-not-real",body)}},body,"application/json");check(response&&response->status==200,"offline callback acknowledged");};
    auto transport=[&]{const auto plugins=call(*app,"GET","/api/capabilities").at("plugins");for(const auto& plugin:plugins)if(plugin.at("id")=="napcat")return plugin.at("current");throw std::runtime_error("napcat missing");};
    auto settle=[&]{pump(*app,[&]{return !transport().at("busy").get<bool>()&&transport().at("transport").at("queued")==0;});};
    call(*app,"POST","/api/control",{{"offline",true}});post("8101");settle();
    check(bot.size()==0&&call(*app,"GET","/api/dashboard").at("model_budget").at("used")==0,"offline receives without model calls or QQ sends");
    check(transport().at("offline")==true,"QQ reports offline");
    app->stop();app=std::make_unique<Application>(cfg);check(app->initialize(),"offline restart initialized");
    check(call(*app,"GET","/api/chat/status").at("offline")==true&&transport().at("offline")==true,"offline persists to QQ and desktop across restart");
    call(*app,"POST","/api/control",{{"paused",true}});call(*app,"POST","/api/control",{{"offline",false}});
    check(call(*app,"GET","/api/chat/status").at("can_send")==false,"online does not clear manual pause");
    call(*app,"POST","/api/control",{{"paused",false}});post("8101");settle();check(bot.size()==0,"offline receipt is not replayed or retried on reconnect");
    bot.httpFailure=402;post("8102");settle();
    check(bot.size()==0&&transport().at("issue")=="insufficient_balance","balance failure stays private and QQ remains silent");
    const auto base=scratch/"channels/napcat/10001/transport";bool retained=false;for(const auto& row:rows(base,"napcat")){const auto value=Json::parse(row.payload);if((value.value("failure_code",std::string{})=="insufficient_balance"&&value.value("status",std::string{})=="silent")||(value.value("offline_reason",std::string{})=="insufficient_balance"&&value.value("status",std::string{})=="offline"))retained=true;}
    check(retained,"balance failure diagnostic retained in encrypted ledger");
    pump(*app,[&]{return call(*app,"GET","/api/dashboard").at("balance_control").at("balance_offline").get<bool>();});
    call(*app,"POST","/api/balance/retry");bot.httpFailure=0;post("8103");pump(*app,[&]{return bot.size()==1;});settle();
    check(bot.size()==1,"next online input works after balance restored");
    const auto started=bot.requestsStarted.load();bot.delayMs=700;post("8104");pump(*app,[&]{return bot.requestsStarted>started;});
    call(*app,"POST","/api/control",{{"offline",true}});call(*app,"POST","/api/control",{{"offline",false}});settle();
    check(bot.size()==1,"rapid offline/online cancels existing QQ session without delayed send");
    bot.delayMs=0;post("8105");pump(*app,[&]{return bot.size()==2;});settle();check(bot.size()==2,"fresh QQ input succeeds after cancelling in-flight model turn");
    app->stop();std::cout<<"PASS offline persistence, independent pause, silent balance diagnostics, dedup, reconnect and session cancellation\n";
}
