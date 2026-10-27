    mutable std::mutex balanceSignalMutex;
    Json balanceSignals=Json::array(),balanceLast=Json::object();
    std::atomic<bool> balanceTrip=false,balanceStopped=false;
    std::future<Json> balanceFetch;
    std::int64_t nextBalanceCheck=0,lastBalanceAttempt=0;
    unsigned balanceGeneration=0;bool balanceForced=false;
    std::vector<Profile> balanceProfiles()const{
        std::vector<Profile> values{profile(get("ha.settings","model").value.at("id"))};
        if(values.front().model.protocol!=ayt::inference::Protocol::Mock&&cfg.reviewEnabled&&!cfg.reviewProfileId.empty()){const auto p=profile(cfg.reviewProfileId);if(p.id!=values.front().id)values.push_back(p);}
        return values;
    }
    void providerResult(const Profile& p,int status){
        if(status!=402)return;
        if(cfg.channel){if(cfg.channel->providerFailure)cfg.channel->providerFailure(p.id,balance::signature(p),status);return;}
        std::lock_guard lock(balanceSignalMutex);balanceTrip=true;
        const Json item={{"profile_id",p.id},{"signature",balance::signature(p)}};
        if(std::find(balanceSignals.begin(),balanceSignals.end(),item)==balanceSignals.end())balanceSignals.push_back(item);
    }
    void resetBalanceCheck(){++balanceGeneration;nextBalanceCheck=0;balanceLast=Json::object();}
    Json balanceStatus()const{
        const auto control=get("ha.control","current").value;
        return {{"automatic",true},{"manual_offline",balance::manual(control)},{"balance_offline",control.value("balance_offline",false)},
            {"blocked",control.value("balance_blocks",Json::object())},{"checking",balanceFetch.valid()},{"last_attempt_ms",lastBalanceAttempt},{"next_check_ms",nextBalanceCheck},{"interval_seconds",60},{"connections",balanceLast}};
    }
    void balanceTick(std::int64_t now){
        if(cfg.channel)return;
        const auto required=balanceProfiles();auto old=get("ha.control","current");auto blocks=old.value.value("balance_blocks",Json::object());
        for(auto it=blocks.begin();it!=blocks.end();){const auto found=std::find_if(required.begin(),required.end(),[&](const auto& p){return p.id==it.key()&&balance::signature(p)==it.value().value("signature",std::string{});});if(found==required.end())it=blocks.erase(it);else ++it;}
        Json incoming;{std::lock_guard lock(balanceSignalMutex);incoming=std::exchange(balanceSignals,Json::array());}
        std::set<std::string> freshFailures;
        for(const auto& signal:incoming)for(const auto& p:required)if(signal.at("profile_id")==p.id&&signal.at("signature")==balance::signature(p)){
            freshFailures.insert(p.id);blocks[p.id]={{"signature",balance::signature(p)},{"detected_ms",now},{"code","insufficient_balance"}};nextBalanceCheck=std::max(now,lastBalanceAttempt+60000);
        }
        if(balanceFetch.valid()&&balanceFetch.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            const auto batch=balanceFetch.get();if(batch.at("generation")==balanceGeneration){
                balanceLast=batch.at("connections");
                for(const auto& p:required){const auto row=balanceLast.value(p.id,Json::object());if(row.value("signature",std::string{})!=balance::signature(p))continue;
                    if(row.value("status",std::string{})=="available"&&!freshFailures.contains(p.id)&&(!blocks.contains(p.id)||blocks.at(p.id).value("detected_ms",std::int64_t(0))<=row.value("checked_ms",std::int64_t(0))))blocks.erase(p.id);
                    else if(row.value("status",std::string{})=="insufficient_balance")blocks[p.id]={{"signature",balance::signature(p)},{"detected_ms",now},{"code","insufficient_balance"}};
                }
            }
        }
        const bool automatic=!blocks.empty();
        if(blocks!=old.value.value("balance_blocks",Json::object())||automatic!=old.value.value("balance_offline",false))controlState({{"balance_blocks",blocks},{"balance_offline",automatic}},now);
        {std::lock_guard lock(balanceSignalMutex);balanceTrip=!balanceSignals.empty();}
        // This monitor deliberately runs outside the suspended model work queue.
        // A manual-only offline does not need polling; auto-offline keeps polling
        // even when the operator additionally chooses to stay offline.
        if(balanceFetch.valid()||balanceStopped||now<nextBalanceCheck||(balance::manual(get("ha.control","current").value)&&!automatic&&!balanceForced))return;
        balanceForced=false;lastBalanceAttempt=now;nextBalanceCheck=now+60000;const auto generation=balanceGeneration;
        balanceFetch=std::async(std::launch::async,[this,required,generation]{
            Json connections=Json::object();
            for(const auto& p:required){Json row={{"signature",balance::signature(p)},{"status","unsupported"},{"checked_ms",serviceNowMs()}};
                if(p.model.protocol==ayt::inference::Protocol::Mock){row["status"]="mock";connections[p.id]=row;continue;}
                if(balance::supported(p))try{
                    const auto key=ayt::net::http::environmentCredential(p.model.credentialEnvironment);
                    if(key.empty()||key.size()>8192||key.find_first_of("\r\n")!=std::string::npos)row["status"]="credential_unavailable";
                    else{
                        ayt::net::http::Request request;request.origin=p.model.origin;request.path="/user/balance";request.method="GET";request.timeout=std::chrono::seconds(8);request.maxResponseBytes=32768;request.useSystemProxy=p.model.useSystemProxy;request.proxyOrigin=p.model.proxyOrigin;request.cancelled=[this]{return balanceStopped.load();};request.headers={{"Authorization","Bearer "+key},{"Accept","application/json"}};
                        const auto result=ayt::net::http::perform(request);row["http_status"]=result.httpStatus;row["status"]="query_unavailable";
                        if(result&&result.httpStatus==200){const auto availability=balance::available(result.body);row["status"]=availability?(*availability?"available":"insufficient_balance"):"invalid_response";}
                        else if(result.httpStatus==402)row["status"]="insufficient_balance";
                    }
                }catch(...){row["status"]="query_unavailable";}
                connections[p.id]=std::move(row);
            }
            return Json{{"generation",generation},{"connections",connections}};
        });
    }
    void stopBalance(){balanceStopped=true;if(balanceFetch.valid()){try{balanceFetch.get();}catch(...){}}}
