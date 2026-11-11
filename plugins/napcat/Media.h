#pragma once
#include "Protocol.h"
#include <Hibernation/Application.h>
#include <Hibernation/Vault.h>
#include <Hibernation/ReplyMessages.h>
#include <sstream>
#include <AYIO/Workspace.h>
namespace hibernation::napcat {
// Presentation fallback only: approved text and authored bundles stay intact.
// Blank lines inside fenced code are not conversation boundaries.
inline PluginJson paragraphReply(const std::string& text,bool enabled=true){
    if(text.empty())return PluginJson::array();if(!enabled)return PluginJson::array({text});
    PluginJson parts=PluginJson::array();std::istringstream lines(text);std::string line,current;char fence=0;
    auto flush=[&]{if(!current.empty()){parts.push_back(current);current.clear();}};
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();const auto start=line.find_first_not_of(" \t");
        const bool marker=start!=std::string::npos&&(line.compare(start,3,"```")==0||line.compare(start,3,"~~~")==0);
        if(marker){if(!fence)fence=line[start];else if(fence==line[start])fence=0;}
        if(!fence&&!marker&&start==std::string::npos){flush();continue;}
        if(!current.empty())current+='\n';current+=line;
    }flush();
    if(parts.empty())return PluginJson::array({text});
    while(parts.size()>replyMessagesMaximum){const auto tail=parts.back().get<std::string>();parts.erase(parts.end()-1);parts.back()=parts.back().get<std::string>()+"\n\n"+tail;}
    for(const auto& part:parts)if(reply_messages_detail::measure(part.get<std::string>()).characters>replyMessageCharactersMaximum)return PluginJson::array({text});
    return parts;
}
inline PluginJson sendImageTool(){return tool("napcat.send_image","把本次 QQ 会话中已成功取得的截图、像素画预览或工作区图片发送给当前对话。computer.screenshot、pixel.preview或workspace.view_image成功后传其source_id；无需模型支持视觉。仅发往当前 QQ 会话，不接受路径、URL、base64 或其他收件人；群聊图片全群可见。发送成功必须以本工具回执为准。",object({{"source_id",{{"type","string"},{"minLength",1},{"maxLength",256}}}},PluginJson::array({"source_id"})));}
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
    if(observation.value("kind",std::string{})!="tool_observation"||(observation.value("tool",std::string{})!="computer.screenshot"&&observation.value("tool",std::string{})!="pixel.preview"&&observation.value("tool",std::string{})!="workspace.view_image"))return {{},"image_source_not_screenshot"};
    const auto result=observation.value("result",PluginJson::object());const auto image=result.value("image",PluginJson::object());
    const auto mime=image.value("mime_type",std::string{}),bytes=image.value("base64",std::string{});
    if((mime!="image/jpeg"&&mime!="image/png")||bytes.empty()||bytes.size()>768*1024||bytes.size()%4||bytes.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")!=std::string::npos)return {{},"screenshot_image_invalid"};
    return {{{"file","base64://"+bytes},{"summary","桌面截图"}},""};
}
inline PluginJson sendFileTool(){return tool("napcat.send_file","把已授权工作区中的现有文件发送到当前QQ会话；像素画先pixel.export得到相对路径，再用本工具。mount只选drafts或materials，path是区内相对路径。图片预览可直接napcat.send_image；导出/预览成功不代表送达，sent=true才代表发送成功。单文件最多8MiB，不接受外部路径、网址或其他收件人。",object({{"mount",{{"enum",{"drafts","materials"}}}},{"path",{{"type","string"},{"minLength",1},{"maxLength",1024}}}},PluginJson::array({"mount","path"})));}
struct WorkspaceAttachment{std::filesystem::path path;std::string name,hash;std::size_t bytes=0;};
inline WorkspaceAttachment workspaceAttachment(const PluginJson& bindings,const PluginJson& args){
    const auto mount=args.at("mount").get<std::string>(),relative=args.at("path").get<std::string>();
    if((mount!="drafts"&&mount!="materials")||!bindings.contains(mount))throw std::runtime_error("qq_file_workspace_denied");
    const auto root=std::filesystem::u8path(bindings.at(mount).get<std::string>());if(!root.is_absolute())throw std::runtime_error("qq_file_workspace_denied");
    ayt::io::Workspace broker;std::string issue,content;ayt::io::WorkspaceMount entry{mount,root,true,false};entry.maxReadBytes=8*1024*1024;
    if(!broker.configure({entry},[](const auto& a){return a.operation==ayt::io::WorkspaceOperation::Read;},issue)||!broker.read("qq.current-conversation",mount,relative,content,issue)||content.empty())throw std::runtime_error("qq_file_workspace_denied");
    auto path=(root/std::filesystem::u8path(relative)).lexically_normal();path.make_preferred();return {path,pathUtf8(path.filename()),digest(content),content.size()};
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
// Failure fallbacks stay short and never contain provider bodies,
// private prompts, tokens, or arbitrary exception text.
inline std::string failureNotice(const PluginJson& failure){
    const auto code=failure.value("code",std::string{});std::string reason;
    if(code=="channel_host_timeout"||code=="channel_session_unavailable")reason="连接超时或已断开，稍后再试。";
    else if(code=="local_hourly_budget"||code=="background_budget_reserved")reason="暂时无法回应，过一会儿再试。";
    else if(code=="insufficient_balance")reason="暂时离线，恢复后再聊。";
    else if(code=="credential_invalid"||code=="access_denied")reason="连接暂时不可用，需要先检查连接设置。";
    else if(code=="timeout"||code=="connection_failed"||code=="provider_unavailable")reason="连接暂时不稳定，稍后再试。";
    else reason="这次没能回上来，稍后再试。";
    return reason;
}
}
