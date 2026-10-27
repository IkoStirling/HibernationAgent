#pragma once
#include "Protocol.h"
#include <Hibernation/Application.h>
#include <Hibernation/Vault.h>
namespace hibernation::napcat {
inline PluginJson sendImageTool(){return tool("napcat.send_image","把本次 QQ 会话中已成功取得的截图发送给当前对话。先调用 computer.screenshot，再传其 source_id；无需模型支持视觉。仅发往当前 QQ 会话，不接受路径、URL、base64 或其他收件人；群聊图片全群可见。发送成功必须以本工具回执为准。",object({{"source_id",{{"type","string"},{"minLength",1},{"maxLength",256}}}},PluginJson::array({"source_id"})));}
inline PluginJson captureAndSendTool(const PluginJson& screenshot){
    return tool("napcat.capture_and_send","直接截图并发送到当前 QQ 对话；仅发截图时优先使用它，一次操作完成，不需要先看图。参数与computer.screenshot一致，仍受截图与QQ控制权限约束。群聊全群可见；返回sent才代表送达。不会把图片传给模型，不得据此声称看到了画面细节。需要分析画面时使用computer.screenshot。",screenshot.at("parameters"));
}
struct ScreenshotImage {PluginJson value=PluginJson::object();std::string error;explicit operator bool()const{return error.empty();}};
inline ScreenshotImage screenshotImage(const std::filesystem::path& root,const hibernation::ProposalContext& context,const std::string& key,const std::string& trace){
    const auto source=context.sources.find(key);
    if(source==context.sources.end()||source->second.scope!="ha.archive"||source->second.kind!="tool_observation")return {{},"image_source_not_available"};
    ayt::storage::RecordStore store;if(!store.open(root/"archive/records.sqlite",{hibernation::openVaultCodec(root),false,false}))return {{},"image_archive_unavailable"};
    ayt::storage::RecordQuery q;q.scope="ha.archive";q.id=key;q.limit=1;q.maxPayloadBytes=1024*1024;
    const auto rows=store.query({{"ha.archive"},{key}},q);if(!rows||rows.records.empty()||rows.records.front().revision!=source->second.revision||rows.records.front().correlation!=trace)return {{},"image_source_stale"};
    const auto observation=PluginJson::parse(rows.records.front().payload);
    if(observation.value("kind",std::string{})!="tool_observation"||observation.value("tool",std::string{})!="computer.screenshot")return {{},"image_source_not_screenshot"};
    const auto result=observation.value("result",PluginJson::object());const auto image=result.value("image",PluginJson::object());
    const auto mime=image.value("mime_type",std::string{}),bytes=image.value("base64",std::string{});
    if((mime!="image/jpeg"&&mime!="image/png")||bytes.empty()||bytes.size()>768*1024||bytes.size()%4||bytes.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")!=std::string::npos)return {{},"screenshot_image_invalid"};
    return {{{"file","base64://"+bytes},{"summary","桌面截图"}},""};
}
// Persist the captured original before sending. Recovered requests use the same
// encrypted image and the sender's durable outbox, including unknown outcomes.
inline PluginJson captureAndSend(const std::filesystem::path& root,const std::string& requestId,const PluginJson& args,const ProposalContext& context,const std::string& trace,const std::function<PluginJson()>& capture,const std::function<PluginJson(const PluginJson&,const ProposalContext&,const std::string&)>& send){
    const auto key="qq-capture-"+digest(requestId+":"+args.dump());
    ayt::storage::RecordStore store;if(!store.open(root/"archive/records.sqlite",{hibernation::openVaultCodec(root),false,false}))return toolFailure("image_archive_unavailable");
    ayt::storage::RecordQuery query;query.scope="ha.archive";query.id=key;query.limit=1;query.maxPayloadBytes=1024*1024;
    auto saved=store.query({{"ha.archive"},{key}},query);if(!saved)return toolFailure("image_archive_unavailable");
    if(saved.records.empty()){
        auto output=capture();if(!output.value("ok",false))return output;
        const auto value=PluginJson{{"kind","tool_observation"},{"tool","computer.screenshot"},{"result",output.at("result")},{"request_id",requestId},{"capture_args",args},{"created_at",nowMs()}};
        if(!store.commit({{"ha.archive",key,0,1,value.dump(),trace}},key,nowMs()))return toolFailure("image_archive_commit_failed");
        saved=store.query({{"ha.archive"},{key}},query);
    }
    if(!saved||saved.records.size()!=1)return toolFailure("image_archive_unavailable");
    const auto& row=saved.records.front();const auto original=PluginJson::parse(row.payload);
    if(original.value("request_id",std::string{})!=requestId||original.value("capture_args",PluginJson::object())!=args)return toolFailure("image_capture_identity_mismatch");
    auto proof=context;proof.sources[key]={key,row.revision,"tool_observation","ha.archive"};
    auto output=send({{"source_id",key}},proof,row.correlation);
    if(output.value("ok",false)){output["result"]["capture_source_id"]=key;output["result"]["captured"]=true;output["result"]["image_delivered_to_model"]=false;}
    return output;
}
// Application notices are clearly marked and never contain provider bodies,
// private prompts, tokens, or arbitrary exception text.
inline std::string failureNotice(const PluginJson& failure){
    const auto code=failure.value("code",std::string{});std::string reason;
    if(code=="channel_host_timeout"||code=="channel_session_unavailable")reason="与桌面后台的连接超时或已失效，请稍后重试。";
    else if(code=="local_hourly_budget"||code=="background_budget_reserved")reason="应用的模型调用额度暂时用完，请在管理页检查调用预算。";
    else if(code=="insufficient_balance")reason="模型 API 余额不足，请充值后重试。";
    else if(code=="credential_invalid"||code=="access_denied")reason="模型连接的密钥或访问权限有问题，请在管理页检查。";
    else if(code=="timeout"||code=="connection_failed"||code=="provider_unavailable")reason="模型服务超时或暂时无法连接，请稍后重试。";
    else reason="模型调用或审核未能完成，请稍后重试，并在管理页查看接入状态。";
    return "【程序提示】这次请求没有完成："+reason;
}
}
