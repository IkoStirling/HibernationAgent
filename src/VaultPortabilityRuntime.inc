    // Transient owner-only operations: no password/request is stored in the
    // archive, recovery queue or logs. An interrupted operation is not resumed.
    std::future<Json> portableFuture;
    struct PortablePassword {
        std::string value;
        explicit PortablePassword(const std::string& input):value(input){}
        ~PortablePassword(){portableVault::clearSensitiveBuffer(value);}
    };
    portableVault::Cancellation portableCancel;
    Json portableStatus={{"state","idle"},{"operation",""},{"operation_id",""},{"result",nullptr},{"error",""}};
    bool portableBusy()const{return portableFuture.valid();}
    void harvestPortableVault(){
        if(!portableFuture.valid()||portableFuture.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try{const auto outcome=portableFuture.get();portableStatus["state"]=outcome.value("ok",false)?"completed":"failed";portableStatus["result"]=outcome.value("result",Json(nullptr));portableStatus["error"]=outcome.value("error",std::string{});}catch(...){portableStatus["state"]="failed";portableStatus["result"]=nullptr;portableStatus["error"]="portable_operation_failed";}portableCancel.reset();
    }
    void stopPortableVault(){if(portableCancel)portableCancel->store(true);if(portableFuture.valid()){portableFuture.wait();harvestPortableVault();}}
    LocalReply portableVaultRequest(const LocalRequest& req,const Json& body){
        if(cfg.channel)return error(403,"portable_owner_only");harvestPortableVault();
        if(req.method=="GET"&&req.path=="/api/vault-portability/status")return reply(200,portableStatus);
        if(req.method!="POST")return error(405,"method_not_allowed");
        const std::string prefix="/api/vault-portability/";const auto operation=req.path.starts_with(prefix)?req.path.substr(prefix.size()):std::string{};
        if(operation!="export"&&operation!="verify"&&operation!="restore")return error(404,"route_missing");
        if(portableBusy()||workshopBusy()||busy()||activityGenerator.running()||reactionGenerator.running())return error(409,"portable_busy");
        if(!runtimePaused||!runtimeOffline.load())return error(409,"portable_pause_offline_required");
        if(!body.is_object()||body.dump().size()>12*1024*1024)return error(400,"portable_bundle_invalid");
        for(auto it=body.begin();it!=body.end();++it)if(it.key()!="password"&&(operation=="export"||(it.key()!="bundle"&&it.key()!="backupRoot"&&(operation!="restore"||it.key()!="targetRoot"))))return error(400,"portable_bundle_invalid");
        if(!body.contains("password")||!body.at("password").is_string())return error(400,"portable_password");
        const auto& inputPassword=body.at("password").get_ref<const std::string&>();if(inputPassword.size()<12||inputPassword.size()>1024||inputPassword.find('\0')!=std::string::npos)return error(400,"portable_password");const auto password=std::make_shared<PortablePassword>(inputPassword);
        Json bundle;std::filesystem::path backup,target;try{if(operation!="export"){bundle=body.at("bundle");backup=std::filesystem::u8path(body.at("backupRoot").get<std::string>());}if(operation=="restore")target=std::filesystem::u8path(body.at("targetRoot").get<std::string>());}catch(...){return error(400,"portable_bundle_invalid");}
        const auto dataRoot=cfg.dataRoot;std::vector<std::filesystem::path> protectedRoots={cfg.dataRoot,cfg.characterRoot,cfg.projectRoot,cfg.uiRoot,cfg.pluginRoot,cfg.draftsRoot,cfg.materialsRoot,cfg.skillsRoot};for(const auto& row:cfg.computer.value("readRoots",Json::array()))protectedRoots.push_back(std::filesystem::u8path(row.at("path").get<std::string>()));
        portableCancel=std::make_shared<std::atomic<bool>>(false);const auto cancel=portableCancel;const auto operationId=id("portable-vault");portableStatus={{"state","running"},{"operation",operation},{"operation_id",operationId},{"result",nullptr},{"error",""}};
        // A shared scoped buffer avoids small-string password copies when the
        // async closure is moved by the standard library.
        portableFuture=std::async(std::launch::async,[operation,password,bundle=std::move(bundle),backup,target,dataRoot,protectedRoots,cancel]()mutable->Json{
            try{Json result;if(operation=="export")result=portableVault::exportBundle(dataRoot,password->value,cancel);else if(operation=="verify")result=portableVault::verifyBackup(bundle,password->value,backup,cancel);else result=portableVault::restoreBackup(bundle,password->value,backup,target,protectedRoots,cancel);portableVault::clearSensitiveBuffer(password->value);return {{"ok",true},{"result",std::move(result)}};}
            catch(const std::exception& e){portableVault::clearSensitiveBuffer(password->value);const std::set<std::string> codes={"portable_password","portable_bundle_invalid","portable_unlock_failed","portable_key_unavailable","portable_backup_invalid","portable_backup_mismatch","portable_target_exists","portable_path_rejected","portable_crypto_unavailable","portable_cancelled"};const std::string code=e.what();return {{"ok",false},{"error",codes.contains(code)?code:"portable_operation_failed"}};}
            catch(...){portableVault::clearSensitiveBuffer(password->value);return {{"ok",false},{"error","portable_operation_failed"}};}
        });return reply(202,{{"operation_id",operationId},{"state","running"}});
    }
