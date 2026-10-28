#include "Ledger.h"
#include "Media.h"
#include "IncomingMedia.h"
#include <Hibernation/Application.h>
#include <Hibernation/ContextControl.h>
#include <Hibernation/ChannelProfiles.h>
#include <Hibernation/SharedMemory.h>
#include <httplib.h>
#include <condition_variable>
#include <thread>
using namespace hibernation;
using namespace hibernation::plugins;
using namespace hibernation::napcat;
namespace {
class Napcat final:public BasePlugin {
    PluginHostContext host_;PluginJson config_=PluginJson::object();Settings settings_;
    std::unique_ptr<Ledger> ledger_;std::unique_ptr<httplib::Server> server_;
    std::thread listener_,worker_;std::atomic<bool> stopped_=true,paused_=true,offline_=false,working_=false;
    std::atomic<std::uint64_t> offlineEpoch_=0,workerEpoch_=0;
    std::atomic<int> port_=0,lastHttpError_=0;std::atomic<std::uint64_t> callbacks_=0,signatureFailures_=0,httpRequests_=0;mutable std::mutex mutex_;std::condition_variable wake_;
    std::map<std::string,PluginJson> replies_;std::map<std::string,std::int64_t> admitted_;std::uint64_t token_=0;
    std::string issue_,activeRequest_,lastControlKind_,lastControlStatus_;double lastControlMs_=0;std::filesystem::path root_;std::uint64_t observationSequence_=0;
    bool cancelled()const{return stopped_.load()||paused_.load()||offline_.load()||offlineEpoch_.load()!=workerEpoch_.load();}
    void issue(std::string code){std::lock_guard lock(mutex_);issue_=std::move(code);}
    PluginJson mailbox(const char* kind,PluginJson args){
        const auto started=std::chrono::steady_clock::now();
        std::string token;{std::lock_guard lock(mutex_);token=std::to_string(++token_);replies_[token]=nullptr;}
        args["kind"]=kind;args["plugin"]="napcat";args["token"]=token;args["request_id"]=activeRequest_;host_.emit(args);
        std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::seconds(30),[&]{return cancelled()||!replies_.at(token).is_null();});
        auto value=replies_.at(token);replies_.erase(token);lastControlKind_=kind;lastControlMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();lastControlStatus_=cancelled()?"cancelled":value.is_null()?"timeout":"received";
        if(value.is_null())return {{"mailbox_timeout",!cancelled()}};return value;
    }
    ayt::net::http::Response api(const std::string& action,const PluginJson& args,int seconds=5){
        ayt::net::http::Request request;request.origin=settings_.origin;request.path="/"+action;request.method="POST";request.body=args.dump();request.timeout=std::chrono::seconds(seconds);request.maxResponseBytes=128*1024;request.cancelled=[this]{return cancelled();};
        const auto token=ayt::net::http::environmentCredential(settings_.apiEnvironment);if(token.size()<16||token.size()>4096||token.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("api_token_missing");
        request.headers={{"Content-Type","application/json"},{"Authorization","Bearer "+token}};return ayt::net::http::perform(request);
    }
    bool login(){const auto response=api("get_login_info",PluginJson::object());if(!response||response.httpStatus!=200){issue("napcat_unavailable");return false;}const auto value=PluginJson::parse(response.body);if(value.value("status",std::string{})!="ok"||value.value("retcode",-1)!=0||digits(value.at("data").at("user_id"))!=settings_.bot){issue("bot_identity_mismatch");return false;}return true;}
    PluginJson sendScreenshot(const PluginJson& row,const std::filesystem::path& localRoot,const Json& args,const ProposalContext& context,const std::string& trace){
        if(cancelled()||!stillAllowed(row)||!settings_.access.controls(row.at("sender_id")==settings_.owner))return toolFailure("qq_image_send_denied");
        const auto source=args.at("source_id").get<std::string>();const auto key=digest(row.at("id").get<std::string>()+":"+source);const auto old=ledger_->media(key);
        if(!old.empty())return old.value("output",toolFailure("image_delivery_unknown_not_retried"));
        const auto image=screenshotImage(localRoot,context,source,trace);if(!image)return toolFailure(image.error.c_str());
        if(!login())return toolFailure("napcat_unavailable");if(cancelled())return toolFailure("cancelled");
        PluginJson message=PluginJson::array();const bool group=row.at("type")=="group";
        if(group&&settings_.replyAt)message.push_back({{"type","at"},{"data",{{"qq",row.at("sender_id")}}}});
        message.push_back({{"type","image"},{"data",image.value}});
        if(!ledger_->startMedia(key,row.at("id"),source))return ledger_->media(key).value("output",toolFailure("image_delivery_unknown_not_retried"));
        PluginJson output=toolFailure("image_delivery_unknown_not_retried");
        try{const auto response=api(group?"send_group_msg":"send_private_msg",{{group?"group_id":"user_id",row.at(group?"group_id":"sender_id")},{"message",message}},20);
            if(response&&response.httpStatus==200){const auto value=PluginJson::parse(response.body,nullptr,false);if(value.is_object()&&value.value("status",std::string{})=="ok"&&value.value("retcode",-1)==0)output=toolSuccess({{"sent",true},{"type","image"},{"recipient","current_conversation"},{"receipt",value.value("data",PluginJson::object())}});else if(value.is_object()&&value.contains("retcode")&&value.at("retcode").is_number_integer())output=toolFailure("image_send_rejected");}
            else if(response&&(response.httpStatus==401||response.httpStatus==403||response.httpStatus==429))output=toolFailure("image_send_rejected");
        }catch(...){} // Preserve uncertain delivery, never repeat it automatically.
        ledger_->finishMedia(key,output);return output;
    }
    void receive(const httplib::Request& request,httplib::Response& response){
        ++callbacks_;
        response.set_header("Cache-Control","no-store");auto respond=[&](int status,const std::string& code){response.status=status;response.set_content(PluginJson{{"status",code}}.dump(),"application/json");};
        try{
            if(request.has_header("Origin")){respond(403,"browser_forbidden");return;}
            const auto secret=ayt::net::http::environmentCredential(settings_.eventEnvironment);
            if(!authenticate(secret,request.body,request.get_header_value("X-Signature"))){++signatureFailures_;respond(401,"signature_invalid");return;}
            if(stopped_){respond(503,"stopped");return;} // paused events are retained, not acted on.
            const auto parsed=PluginJson::parse(request.body);const auto row=parseEvent(parsed,settings_,nowMs());if(!row){response.set_content("{}","application/json");return;}
            if(request.has_header("X-Self-ID")&&request.get_header_value("X-Self-ID")!=settings_.bot){respond(403,"self_id_mismatch");return;}
            std::lock_guard lock(mutex_);
            const auto known=ledger_->classify(*row);if(known=="duplicate"){response.set_content("{}","application/json");return;}if(known=="identity_conflict"){respond(409,known);return;}
            // Save follow-ups too. Cooldown paces work rather than discarding input.
            const auto state=ledger_->enqueue(*row,settings_.queueLimit);
            if(state=="queued"){if(offline_)ledger_->finish(row->at("id"),"offline");else wake_.notify_all();}
            if(state=="queue_full"||state=="conversation_limit"){respond(503,state);return;}
            if(state=="identity_conflict"){respond(409,state);return;}
            // Never use OneBot's quick-operation reply field; output is sent
            // explicitly after validation and approval, to the captured route.
            response.set_content("{}","application/json");
        }catch(const PluginJson::exception&){respond(400,"invalid_event");}catch(const std::runtime_error&){respond(400,"invalid_event");}catch(...){respond(503,"storage_unavailable");}
    }
    PluginJson converse(const PluginJson& row,const PluginJson& hostConfig){
        Config cfg;cfg.projectRoot=std::filesystem::u8path(hostConfig.at("projectRoot").get<std::string>());cfg.characterRoot=std::filesystem::u8path(hostConfig.at("characterRoot").get<std::string>());cfg.uiRoot=std::filesystem::u8path(hostConfig.at("uiRoot").get<std::string>());
        cfg.dataRoot=conversation(root_,row);cfg.pluginRoot=cfg.dataRoot/"disabled-plugins";cfg.pluginRootExplicit=true;cfg.computer={{"enabled",false},{"allowScreenCapture",false},{"readRoots",PluginJson::array()}};
        for(const auto& p:hostConfig.at("profiles"))cfg.profiles.push_back(readChannelProfile(p));
        cfg.reviewEnabled=hostConfig.at("reviewEnabled");cfg.reviewProfileId=hostConfig.at("reviewProfileId");cfg.maxReviewRewrites=hostConfig.at("maxReviewRewrites");cfg.contextBytes=hostConfig.at("contextBytes");cfg.contextControl=hostConfig.value("contextControl",Json::object());if(settings_.access.strict){cfg.contextControl["reviewReadOnlyTools"]=true;cfg.contextControl["reviewSilence"]=true;}cfg.maxToolRounds=hostConfig.at("maxToolRounds");cfg.maxStartsPerHour=hostConfig.at("maxStartsPerHour");
        auto policy=std::make_shared<ChannelPolicy>();policy->pricing=hostConfig.value("pricing",Json::object());policy->selectedProfile=hostConfig.at("selectedProfile");policy->identity=identity(row,settings_);policy->roleState=hostConfig.at("state");policy->stage=hostConfig.at("stage");
        const auto incoming=ledger_->recentImages(row);PluginJson references=PluginJson::array();for(const auto& image:incoming)references.push_back({{"id",image.at("id")},{"message_id",image.at("message_id")},{"kind",image.at("kind")},{"summary",image.at("summary")},{"received_ms",image.at("received_ms")}});
        const auto messageIds=row.value("input_message_ids",PluginJson::array({row.at("message_id")}));
        PluginJson currentImages=PluginJson::array();for(auto& reference:references){const bool current=std::find(messageIds.begin(),messageIds.end(),reference.at("message_id"))!=messageIds.end();reference["in_current_input"]=current;if(current)currentImages.push_back(reference);}
        policy->identity["received_images"]=references;policy->identity["current_input_images"]=currentImages;policy->identity["input_message_ids"]=messageIds;
        policy->identity["image_reference_note"]="received_images是对方近几分钟发来的历史图片索引；仅current_input_images属于本次输入。它们不是自己发出的图片，用户说没收到自己发图时应核对发送回执。";
        policy->desktopTools=hostConfig.value("desktopTools",PluginJson::array());policy->desktopCapabilities=hostConfig.value("desktopCapabilities",PluginJson::object());policy->body=hostConfig.value("body",PluginJson::object());
        policy->sharedMemoryRoot=std::filesystem::u8path(hostConfig.value("sharedMemoryRoot",std::string{}));
        if(!policy->desktopTools.empty())policy->desktopTools.push_back(sendImageTool());
        policy->desktopTools.push_back(viewImageTool());
        const auto captureTool=std::find_if(policy->desktopTools.begin(),policy->desktopTools.end(),[](const auto& tool){return tool.at("name")=="computer.screenshot";});
        if(captureTool!=policy->desktopTools.end()){const auto combined=captureAndSendTool(*captureTool);policy->desktopTools.push_back(combined);}
        const auto dispatch=[this,localRoot=cfg.dataRoot,requestId=row.at("id").get<std::string>()](const std::string& name,const Json& args,const ProposalContext& context,const std::function<bool()>& stop){
            Json sources=Json::array();const auto tag=memoryRootTag(localRoot);for(const auto& [key,source]:context.sources){const bool local=source.scope=="ha.archive"||source.scope=="ha.memory";sources.push_back({{"id",local?tag+key:key},{"revision",source.revision},{"kind",source.kind},{"scope",local?tag+source.scope:source.scope}});}
            auto hostArgs=args;auto sourceId=[&](Json& value){if(!value.is_string())return;const auto key=value.get<std::string>();const auto found=context.sources.find(key);if(found!=context.sources.end()&&(found->second.scope=="ha.archive"||found->second.scope=="ha.memory"))value=tag+key;};
            if(hostArgs.contains("source_id"))sourceId(hostArgs["source_id"]);
            for(const auto* field:{"source_ids","evidence_ids"})if(hostArgs.contains(field)&&hostArgs.at(field).is_array())for(auto& value:hostArgs[field])sourceId(value);
            // Stable across application retries; identical operations in one
            // input reuse the receipt instead of repeating desktop side effects.
            // Read-only snapshots need fresh pixels/layout even with identical
            // arguments. Side effects keep their stable recovery identity.
            const auto call=desktopOperationKey(requestId,name,hostArgs,std::to_string(nowMs())+":"+std::to_string(++observationSequence_));
            Json operation={{"tool",name},{"args",hostArgs},{"sources",sources},{"call_key",call},{"style_internal",name=="style_lora.preview"}};
            operation["input_source_id"]=tag+context.eventId;
            auto result=mailbox("napcat.tool.request",operation);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(110);
            while(result.is_object()&&!result.value("done",false)&&!cancelled()&&!(stop&&stop())&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(std::chrono::milliseconds(40));result=result.value("retry",false)?mailbox("napcat.tool.request",operation):mailbox("napcat.tool.result",{{"call_key",call}});}
            if(!result.is_object()||!result.value("done",false))return toolFailure("desktop_operation_incomplete");return result.value("output",toolFailure("desktop_operation_failed"));
        };
        policy->invokeDesktop=[this,incoming,dispatch,localRoot=cfg.dataRoot,row,requestId=row.at("id").get<std::string>()](const std::string& name,const Json& args,const ProposalContext& context,const std::string& trace,const std::function<bool()>& stop){
            if(name=="napcat.view_image"){
                if(cancelled()||(stop&&stop()))return toolFailure("cancelled");
                const auto found=std::find_if(incoming.begin(),incoming.end(),[&](const auto& image){return image.at("id")==args.at("id");});if(found==incoming.end())return toolFailure("qq_image_not_in_current_conversation");
                return readIncomingImage(*ledger_,*found,settings_,[this](const auto& action,const auto& arguments){return api(action,arguments,8);},[this,stop]{return cancelled()||(stop&&stop());});
            }
            if(name=="napcat.send_image")return stop&&stop()?toolFailure("cancelled"):sendScreenshot(row,localRoot,args,context,trace);
            if(name=="napcat.capture_and_send"){
                if(cancelled()||(stop&&stop())||!stillAllowed(row)||!settings_.access.controls(row.at("sender_id")==settings_.owner))return toolFailure("qq_image_send_denied");
                return captureAndSend(localRoot,requestId,args,context,trace,[&]{return dispatch("computer.screenshot",args,context,stop);},[&](const auto& image,const auto& proof,const auto& originalTrace){return stop&&stop()?toolFailure("cancelled"):sendScreenshot(row,localRoot,image,proof,originalTrace);});
            }
            return dispatch(name,args,context,stop);
        };
        if(hostConfig.value("styleEditingAvailable",false))policy->editStyle=[dispatch](const Json& args,const std::function<bool()>& stop){return dispatch("style_lora.preview",args,ProposalContext{},stop);};
        policy->providerUsage=[this](const std::string& profile,const std::string& purpose,const Json& usage){host_.emit({{"kind","napcat.model.usage"},{"plugin","napcat"},{"profile_id",profile},{"purpose",purpose},{"usage",usage}});};
        policy->providerFailure=[this](const std::string& id,const std::string& signature,int status){host_.emit({{"kind","napcat.balance.failure"},{"plugin","napcat"},{"request_id",activeRequest_},{"profile_id",id},{"signature",signature},{"http_status",status}});};
        auto budgetIssue=std::make_shared<Json>(Json::object());policy->budgetFailure=[budgetIssue]{return *budgetIssue;};
        policy->reserveModelCalls=[this,budgetIssue](int amount){if(cancelled()){*budgetIssue={{"code","channel_session_unavailable"},{"message","QQ 后台任务已暂停。"},{"http_status",0}};return false;}const auto result=mailbox("napcat.budget.request",{{"amount",amount}});const bool allowed=result.value("allowed",false)&&!cancelled();*budgetIssue=allowed?Json::object():result.value("mailbox_timeout",false)?Json{{"code","channel_host_timeout"},{"message","等待桌面后台授权超时，请重试。"},{"http_status",0}}:result.value("failure",Json{{"code","channel_session_unavailable"},{"message","QQ 会话与桌面后台的连接已失效，请重试。"},{"http_status",0}});return allowed;};cfg.channel=policy;
        Application app(std::move(cfg));if(!app.initialize())throw std::runtime_error("channel_initialize_failed");
        const auto requestId=row.at("id").get<std::string>();auto request=[&](const char* path,PluginJson body){const auto result=app.request({"POST",path,body.dump()});if(result.status>=400)throw std::runtime_error("channel_request_failed");return PluginJson::parse(result.body);};
        request("/api/control",{{"paused",false}});
        request("/api/message",{{"request_id",requestId},{"text",row.value("input_text",row.at("text").get<std::string>())}});
        auto end=std::chrono::steady_clock::now()+std::chrono::minutes(5);PluginJson result;
        while(!cancelled()&&std::chrono::steady_clock::now()<end){app.tick();result=request("/api/channel/result",{{"request_id",requestId}});const auto status=result.value("status",std::string{});if((status=="completed"||status=="rejected")&&!result.value("busy",true)){app.stop();return result;}if(!result.value("busy",true)&&status!="completed")break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        // Stop the worker before the channel Application or captured callbacks
        // disappear. Unfinished accepted requests recover with the same ID.
        request("/api/control",{{"paused",true}});if(offline_||workerEpoch_!=offlineEpoch_)request("/api/channel/abandon",{{"request_id",requestId}});app.stop();return {{"status",cancelled()?"paused":"failed"},{"text",""}};
    }
    void deliver(PluginJson row){
        const auto id=row.at("id").get<std::string>();if(row.value("reply",std::string{}).empty()){ledger_->finish(id,"silent");return;}
        if(cancelled())return;
        if(row.value("failure_code",std::string{})=="insufficient_balance"){ledger_->finish(id,"silent");issue("insufficient_balance");return;}
        // Check the logged-in bot before every send (also after an account switch).
        if(!login()){ledger_->update(id,{{"next_ms",nowMs()+15000}});return;}if(cancelled())return;
        PluginJson message=PluginJson::array();if(row.at("type")=="group"&&settings_.replyAt)message.push_back({{"type","at"},{"data",{{"qq",row.at("sender_id")}}}});
        message.push_back({{"type","text"},{"data",{{"text",row.at("reply")}}}});
        const bool group=row.at("type")=="group";PluginJson args={{group?"group_id":"user_id",row.at(group?"group_id":"sender_id")},{"message",message}};
        ledger_->update(id,{{"status","sending"}});
        const auto response=api(group?"send_group_msg":"send_private_msg",args);
        if(response&&response.httpStatus==200){const auto value=PluginJson::parse(response.body,nullptr,false);if(value.is_object()&&value.value("status",std::string{})=="ok"&&value.value("retcode",-1)==0){ledger_->update(id,{{"receipt",value.value("data",PluginJson::object())}});ledger_->finish(id,"sent");issue("");return;}}
        // Definite authentication/rate rejection can be retried. Transport loss,
        // malformed responses, 5xx and timeouts have an unknown delivery result.
        if(response&&(response.httpStatus==401||response.httpStatus==403||response.httpStatus==429)){
            const int attempts=row.value("attempts",0)+1;if(attempts<3){ledger_->update(id,{{"status","ready"},{"attempts",attempts},{"next_ms",nowMs()+15000*attempts}});issue("send_rejected_retry_pending");return;}
            ledger_->finish(id,"failed");issue("send_rejected");return;
        }
        ledger_->finish(id,"unknown");issue("delivery_unknown_not_retried");
    }
    bool stillAllowed(const PluginJson& row)const{return row.at("self_id")==settings_.bot&&(row.at("type")=="group"?settings_.groups.contains(row.at("group_id").get<std::string>()):row.at("sender_id")==settings_.owner||settings_.users.contains(row.at("sender_id").get<std::string>()));}
    void loop(){while(!stopped_){workerEpoch_=offlineEpoch_.load();activeRequest_.clear();try{
            if(cancelled()){std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(250));continue;}
            auto row=ledger_->next();if(!row){std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(250));continue;}
            const auto id=row->at("id").get<std::string>();if(!stillAllowed(*row)){ledger_->finish(id,"revoked");continue;}
            if(row->value("status",std::string{})=="sending"){ledger_->finish(id,"unknown");issue("delivery_unknown_not_retried");continue;}
            if(nowMs()-row->at("created_ms").get<std::int64_t>()>settings_.maxAgeSeconds*1000){ledger_->finish(id,"expired");continue;}
            working_=true;
            if(row->value("status",std::string{})=="ready"){deliver(*row);working_=false;continue;}
            const auto scope=conversationScope(*row);{std::lock_guard lock(mutex_);const auto next=admitted_[scope];if(next>nowMs()){ledger_->update(id,{{"next_ms",next}});working_=false;continue;}}
            *row=ledger_->batch(id,settings_.mergeWindowMs);
            activeRequest_=id;
            auto context=mailbox("napcat.context.request",{{"identity",identity(*row,settings_)}});if(!context.is_object()||!context.value("available",false)||cancelled()){host_.emit({{"kind","napcat.session.release"},{"plugin","napcat"},{"request_id",id}});working_=false;ledger_->update(id,{{"next_ms",nowMs()+1000}});continue;}
            struct Release {PluginHostContext& host;std::string id;~Release(){host.emit({{"kind","napcat.session.release"},{"plugin","napcat"},{"request_id",id}});}} release{host_,id};
            if(!login()){working_=false;ledger_->update(id,{{"next_ms",nowMs()+15000}});continue;}
            {std::lock_guard lock(mutex_);admitted_[scope]=nowMs()+settings_.cooldownSeconds*1000;}
            const int attempts=row->value("model_attempts",0)+1;if(attempts>3){ledger_->update(id,{{"status","ready"},{"application_notice",true},{"reply",failureNotice(Json::object())}});issue("channel_attempt_limit");working_=false;continue;}
            ledger_->update(id,{{"status","model"},{"model_attempts",attempts}});const auto result=converse(*row,context.at("config"));
            if(cancelled()||result.value("status",std::string{})=="paused"){if(offline_||workerEpoch_!=offlineEpoch_)ledger_->finish(id,"offline");else ledger_->update(id,{{"status","queued"},{"next_ms",nowMs()+1000}});working_=false;continue;}
            if(result.value("status",std::string{})!="completed"){const auto failure=result.value("failure",Json::object());if(failure.value("code",std::string{})=="insufficient_balance"){ledger_->update(id,{{"failure_code","insufficient_balance"}});ledger_->finish(id,"silent");issue("insufficient_balance");working_=false;continue;}ledger_->update(id,{{"status","ready"},{"application_notice",true},{"failure_code",failure.value("code",std::string("call_failed"))},{"reply",failureNotice(failure)}});issue("reply_failed_notice_pending");working_=false;continue;}
            ledger_->update(id,{{"status","ready"},{"reply",boundedReply(result.value("text",std::string{}),settings_.maxReplyCharacters)}});working_=false;
        }catch(...){working_=false;issue("napcat_operation_failed");try{if(!activeRequest_.empty()&&(offline_||workerEpoch_!=offlineEpoch_))ledger_->finish(activeRequest_,"offline");else if(!activeRequest_.empty())ledger_->update(activeRequest_,{{"status","ready"},{"application_notice",true},{"reply",failureNotice(Json::object())}});}catch(...){}std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::seconds(1));}}
    }
public:
    ~Napcat()override{shutdown();}
    const char* getName()const override{return "napcat";}
    const char* getVersion()const override{return "0.5.2";}
    const char* getDescription()const override{return "Shared character with separately stored QQ archives and configurable desktop tools";}
    bool bind(PluginHostContext host,PluginJson configuration,std::string& error)override{
        bool replacementStarted=false;
        try{const auto parsed=settings(configuration);if(!stopped_&&configuration==config_){error.clear();return true;}
            if(working_){error="QQ 通道正在处理消息，请先暂停后台，稍后再修改配置。";return false;}
            // Reject incomplete candidates before stopping an existing connection.
            const auto problem=credentialProblem(parsed);if(!problem.empty()){error=configurationMessage(problem);return false;}
            shutdown();replacementStarted=true;host_=std::move(host);settings_=parsed;config_=std::move(configuration);
            root_=host_.dataRoot/"channels"/"napcat"/settings_.bot;ledger_=std::make_unique<Ledger>(root_/"transport");
            server_=std::make_unique<httplib::Server>();server_->new_task_queue=[]{return new httplib::ThreadPool(2,2,8);};server_->set_payload_max_length(64*1024);server_->set_read_timeout(std::chrono::seconds(3));server_->set_write_timeout(std::chrono::seconds(3));
            // The callback port must belong to one receiver; Windows address
            // reuse can otherwise report a successful bind to an occupied port.
            server_->set_socket_options([](socket_t socket){const BOOL exclusive=TRUE;if(setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==SOCKET_ERROR)throw std::runtime_error("napcat_listener_failed");});
            server_->set_pre_routing_handler([this](const auto&,auto&){++httpRequests_;return httplib::Server::HandlerResponse::Unhandled;});server_->set_error_logger([this](const auto& error,const auto*){lastHttpError_=static_cast<int>(error);});
            server_->Post("/onebot/events",[this](const auto& req,auto& res){receive(req,res);});
            if(!server_->bind_to_port("127.0.0.1",settings_.port))throw std::runtime_error("napcat_callback_port_in_use");stopped_=false;port_=settings_.port;
            listener_=std::thread([this]{server_->listen_after_bind();});server_->wait_until_ready();if(!server_->is_running())throw std::runtime_error("napcat_listener_failed");worker_=std::thread([this]{loop();});error.clear();return true;
        }catch(const std::exception& exception){if(replacementStarted)shutdown();error=configurationMessage(exception.what());return false;}
        catch(...){if(replacementStarted)shutdown();error=configurationMessage("");return false;}
    }
    PluginJson tools()const override{return PluginJson::array({tool("napcat.status","查看 QQ 接入状态和数量统计；不会读取聊天内容，也不能向任意 QQ 发送消息。",object())});}
    PluginJson invoke(const std::string& name,const PluginJson&,const std::function<bool()>&)override{return name=="napcat.status"?toolSuccess(snapshot()):toolFailure("unknown_tool");}
    void tick(double)override{}
    void event(const PluginJson& value)override{const auto kind=value.value("kind",std::string{});if(kind=="runtime.paused"){std::lock_guard lock(mutex_);const bool automatic=value.value("balance_offline",false);if(automatic)issue_="insufficient_balance";else if(issue_=="insufficient_balance")issue_.clear();const bool offline=value.value("offline",false);if(offline&&!offline_.exchange(offline)){++offlineEpoch_;if(ledger_)ledger_->discardOffline(automatic?"insufficient_balance":"manual");}else offline_=offline;paused_=value.value("paused",false);wake_.notify_all();return;}if(kind!="runtime.napcat.context"&&kind!="runtime.napcat.budget"&&kind!="runtime.napcat.tool")return;std::lock_guard lock(mutex_);auto found=replies_.find(value.value("token",std::string{}));if(found!=replies_.end()){found->second=value;wake_.notify_all();}}
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);return {{"last_control",{{"kind",lastControlKind_},{"status",lastControlStatus_},{"elapsed_ms",lastControlMs_}}},{"effective_security",settings_.access.json()},{"callback_port",port_.load()},{"running",!stopped_.load()&&server_&&server_->is_running()},{"paused",paused_.load()},{"offline",offline_.load()},{"busy",working_.load()},{"http_requests",httpRequests_.load()},{"http_error",lastHttpError_.load()},{"callbacks",callbacks_.load()},{"signature_failures",signatureFailures_.load()},{"issue",issue_},{"transport",ledger_?ledger_->status():PluginJson::object()}};}
    bool restore(const PluginJson& value)override{return value.is_object();} // Actual inbox is in its own encrypted ledger.
    void shutdown()override{stopped_=true;wake_.notify_all();if(server_)server_->stop();if(listener_.joinable())listener_.join();if(worker_.joinable())worker_.join();server_.reset();port_=0;working_=false;}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new Napcat;}
