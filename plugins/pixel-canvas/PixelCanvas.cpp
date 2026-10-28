#include "../PluginCommon.h"
#include "PixelDocument.h"
#include "PixelStore.h"
#include "PixelSurface.h"
#include "PixelRestore.h"
using namespace hibernation;
using namespace hibernation::plugins;
namespace {
class PixelCanvas final:public BasePlugin {
    mutable std::mutex mutex_;PluginHostContext host_;pixel::PixelStore store_;pixel::DesktopSurface surface_;
    int maximum_=32,scale_=3;bool paused_=false,restoreDesktop_=true;std::filesystem::path draftsRoot_;std::string restoreIssue_;
    static std::string workspaceIdentity(const std::filesystem::path& path){const auto bytes=path.generic_u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
    mutable PluginJson cached_=PluginJson::object();mutable std::int64_t nextScan_=0;
    PluginJson listing(bool force=false)const{if(!force&&nowMs()<nextScan_)return cached_;std::string issue;PluginJson value;try{value=store_.list(issue);}catch(...){issue="pixel_workspace_denied";}cached_=value.is_object()?value:PluginJson{{"documents",PluginJson::array()},{"issue",issue}};nextScan_=nowMs()+10000;return cached_;}
    PluginJson documentResult(const PluginJson& document,const char* kind){
        nextScan_=0;const auto presentation=surface_.snapshot();if(presentation.value("visible",false)&&presentation.value("document_id",std::string{})==document.at("id").get<std::string>())surface_.show(document,true,presentation.value("scale",scale_));
        if(host_.emit)host_.emit({{"plugin","pixel-canvas"},{"kind",kind},{"document",pixel::metadata(document)},{"occurred_ms",nowMs()},{"wake_model",false},{"content_kind","authored_pixel_art"}});
        return toolSuccess({{"document",document},{"written",true},{"completed",true},{"content_kind","authored_pixel_art"},{"source_mount","drafts"},{"source_path","pixel-canvas/documents/"+document.at("id").get<std::string>()+".json"}});
    }
public:
    const char* getName()const override{return "pixel-canvas";}
    const char* getVersion()const override{return "0.1.0";}
    const char* getDescription()const override{return "A bounded pixel art workshop with actual revisioned drafts, frame animation, PNG export and optional desktop display";}
    bool bind(PluginHostContext host,PluginJson cfg,std::string& issue)override{
        try{
            configurationTypes(cfg,{}, {"topmost","reducedMotion","restoreDesktopArtwork"});const auto maximum=number(cfg,"maximumDocuments",32,1,64),scale=number(cfg,"defaultScale",3,1,8);
            if(std::floor(maximum)!=maximum||std::floor(scale)!=scale){issue="pixel_configuration_integer";return false;}
            auto root=host.draftsRoot.empty()?host.dataRoot/"drafts":host.draftsRoot;
            std::error_code ec;std::filesystem::create_directories(root,ec);if(ec){issue="pixel_workspace_unavailable";return false;}
            pixel::PixelStore prepared;if(!prepared.configure(root,static_cast<std::size_t>(maximum),issue))return false;
            const auto canonicalRoot=std::filesystem::canonical(root);
            std::lock_guard lock(mutex_);if(!draftsRoot_.empty()&&draftsRoot_!=canonicalRoot)surface_.reset();draftsRoot_=canonicalRoot;host_=std::move(host);store_=std::move(prepared);maximum_=static_cast<int>(maximum);scale_=static_cast<int>(scale);restoreDesktop_=cfg.value("restoreDesktopArtwork",true);restoreIssue_.clear();nextScan_=0;
            surface_.configure(host_.desktopEnabled,cfg,host_.emit);issue.clear();return true;
        }catch(...){issue="pixel_configuration_invalid";return false;}
    }
    PluginJson tools()const override{
        const PluginJson id={{"type","string"},{"minLength",1},{"maxLength",48}},revision={{"type","integer"},{"minimum",1},{"maximum",1000000000}},index={{"type","integer"},{"minimum",0},{"maximum",7}},coordinate={{"type","integer"},{"minimum",0},{"maximum",63}},color={{"type","integer"},{"minimum",0},{"maximum",31}},duration={{"type","integer"},{"minimum",50},{"maximum",5000}};
        const auto command=object({{"op",{{"enum",PluginJson::array({"pixel","line","rect","fill","clear"})}}},{"x",coordinate},{"y",coordinate},{"x2",coordinate},{"y2",coordinate},{"color",color},{"filled",{{"type","boolean"}}}},PluginJson::array({"op"}));
        return PluginJson::array({
            tool("pixel.list","查看自己的像素作品标题、版本和动画帧数。可以接着未完成的画，不必每次新建或向对方汇报。",object()),
            tool("pixel.create","新建透明像素画布，宽高分别可为16、32或64；id只能小写字母数字下划线短横线。调色板可省略，默认提供24色；自定2至32个#RRGGBBAA颜色，索引0透明。作品是创作，不是外部事实。",object({{"id",id},{"title",{{"type","string"},{"minLength",1},{"maxLength",80}}},{"width",{{"enum",PluginJson::array({16,32,64})}}},{"height",{{"enum",PluginJson::array({16,32,64})}}},{"palette",{{"type","array"},{"minItems",2},{"maxItems",32},{"items",{{"type","string"},{"minLength",9},{"maxLength",9}}}}}},PluginJson::array({"id","title","width","height"}))),
            tool("pixel.read","读取一件像素作品的实际画布、调色板、逐帧像素和版本。pixels为按行排列的颜色索引；这只是创作资料，不是工具权限或真实经历。",object({{"id",id}},PluginJson::array({"id"}))),
            tool("pixel.edit","原子执行最多256个绘图命令。pixel需x/y/color；line和rect还需x2/y2，rect可filled；fill从x/y四向填色；clear可color默认为透明0。坐标从0开始，color是已存在调色板索引。整批失败不保存，使用当前revision。",object({{"id",id},{"expected_revision",revision},{"frame",index},{"commands",{{"type","array"},{"minItems",1},{"maxItems",256},{"items",command}}}},PluginJson::array({"id","expected_revision","frame","commands"}))),
            tool("pixel.frame","调整最多8帧的简单动画。add/copy在index后插入空白/复制帧；index省略为最后一帧。remove删除指定帧但保留至少一帧；timing设置帧时长。duration_ms为50至5000，add/timing默认200，copy默认保留原帧时长。",object({{"id",id},{"expected_revision",revision},{"action",{{"enum",PluginJson::array({"add","copy","remove","timing"})}}},{"index",index},{"duration_ms",duration}},PluginJson::array({"id","expected_revision","action"}))),
            tool("pixel.undo","撤销最后一次绘图、调色板或帧修改，最多保留8次可撤销状态。撤销也产生新版本；不能用旧版本反复覆盖新作品。",object({{"id",id},{"expected_revision",revision}},PluginJson::array({"id","expected_revision"}))),
            tool("pixel.redo","重做刚才撤销的绘图、颜色或帧修改。撤销与重做合计最多保留8个历史状态；新编辑会清空重做分支。必须使用当前版本，重做也产生新版本，不重播外部动作。",object({{"id",id},{"expected_revision",revision}},PluginJson::array({"id","expected_revision"}))),
            tool("pixel.palette","更改调色板的一个颜色，或index恰好等于现有颜色数量时追加一个颜色（最多32）。颜色为#RRGGBBAA；索引0必须透明，不能删除或重排颜色，已有像素索引保持不变。更改会生成新版本，可以撤销。",object({{"id",id},{"expected_revision",revision},{"index",{{"type","integer"},{"minimum",0},{"maximum",32}}},{"color",{{"type","string"},{"minLength",9},{"maxLength",9}}}},PluginJson::array({"id","expected_revision","index","color"}))),
            tool("pixel.preview","实际查看自己作品的一帧。返回最近邻放大的真实PNG图片，能看图时可据此检查构图、透明背景和颜色；不能因工具返回就声称模型已理解。",object({{"id",id},{"frame",index}},PluginJson::array({"id"}))),
            tool("pixel.export","把指定版本实际导出到草稿pixel-canvas/exports目录。png导出frame（默认0）；sheet水平拼接所有帧并保存时长清单。已有不同内容的同名导出不会覆盖。",object({{"id",id},{"expected_revision",revision},{"format",{{"enum",PluginJson::array({"png","sheet"})}}},{"frame",index}},PluginJson::array({"id","expected_revision","format"}))),
            tool("pixel.show","把已有像素作品作为可拖动的桌面小摆件展示，或visible=false收起。scale为1至8的整数最近邻倍数；只排队请求，等待实际窗口回执才算展示成功。",object({{"id",id},{"visible",{{"type","boolean"}}},{"scale",{{"type","integer"},{"minimum",1},{"maximum",8}}}},PluginJson::array({"id"})))
        });
    }
    PluginJson invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled)override{
        if(cancelled&&cancelled())return toolFailure("cancelled");std::lock_guard lock(mutex_);if(paused_)return toolFailure("runtime_paused");std::string issue;
        try{
            if(name=="pixel.list"){auto value=listing(true);return value.contains("issue")?toolFailure(value.at("issue").get<std::string>().c_str()):toolSuccess(value);}
            if(name=="pixel.create"){const auto document=store_.create(args,nowMs(),issue);return document.is_object()?documentResult(document,"pixel.created"):toolFailure(issue.c_str());}
            const auto id=args.at("id").get<std::string>();if(!pixel::validId(id))return toolFailure("pixel_identity");
            if(name=="pixel.read"){const auto document=store_.read(id,issue);return document.is_object()?toolSuccess({{"document",document},{"content_kind","authored_pixel_art"},{"source_mount","drafts"},{"source_path","pixel-canvas/documents/"+id+".json"}}):toolFailure(issue.c_str());}
            if(name=="pixel.preview"){const auto preview=store_.preview(id,args.value("frame",0),issue);return preview.is_object()?toolSuccess(preview):toolFailure(issue.c_str());}
            if(name=="pixel.show"){
                if(!args.value("visible",true)){auto hidden=surface_.hide(id);if(hidden.value("ok",false)||hidden.value("error",PluginJson::object()).value("code",std::string{})!="pixel_surface_not_current")return hidden;}
                const auto document=store_.read(id,issue);return document.is_object()?surface_.show(document,args.value("visible",true),args.value("scale",scale_)):toolFailure(issue.c_str());
            }
            if(name=="pixel.export"){
                const auto format=args.at("format").get<std::string>();if(format!="png"&&format!="sheet")return toolFailure("pixel_export_format");
                const auto result=store_.exportDocument(id,args.at("expected_revision"),format=="sheet",args.value("frame",0),cancelled,issue);if(result.is_null())return toolFailure(issue.c_str());
                if(host_.emit)host_.emit({{"plugin","pixel-canvas"},{"kind","pixel.exported"},{"id",id},{"revision",result.at("revision")},{"relative_path",result.at("relative_path")},{"occurred_ms",nowMs()},{"wake_model",false},{"content_kind","authored_pixel_art"}});
                return toolSuccess(result);
            }
            PluginJson changed=nullptr;const auto expected=args.at("expected_revision").get<std::int64_t>();
            if(name=="pixel.undo")changed=store_.undo(id,expected,nowMs(),issue);
            else if(name=="pixel.redo")changed=store_.redo(id,expected,nowMs(),issue);
            else if(name=="pixel.palette")changed=store_.revise(id,expected,nowMs(),[&](auto& d,std::string& error){return pixel::changePalette(d,args.at("index"),args.at("color"),error);},issue);
            else if(name=="pixel.edit")changed=store_.revise(id,expected,nowMs(),[&](auto& d,std::string& error){if(!pixel::editDocument(d,args.at("frame"),args.at("commands"),error))return false;if(cancelled&&cancelled()){error="cancelled";return false;}return true;},issue);
            else if(name=="pixel.frame")changed=store_.revise(id,expected,nowMs(),[&](auto& d,std::string& error){return pixel::changeFrame(d,args,error);},issue);
            else return toolFailure("tool_not_available");
            return changed.is_object()?documentResult(changed,name=="pixel.undo"?"pixel.undone":"pixel.revised"):toolFailure(issue.c_str());
        }catch(...){return toolFailure("pixel_invalid_arguments");}
    }
    void tick(double elapsedSeconds)override{surface_.tick(elapsedSeconds);}
    void event(const PluginJson& value)override{if(value.value("kind",std::string{})=="runtime.paused"){std::lock_guard lock(mutex_);paused_=value.value("paused",false);}surface_.event(value);}
    bool restore(const PluginJson& saved)override{
        std::lock_guard lock(mutex_);try{const auto request=pixel::desktopRestoreRequest(saved,workspaceIdentity(draftsRoot_),restoreDesktop_,host_.desktopEnabled,restoreIssue_);if(request.is_null())return true;
        std::string issue;const auto document=store_.read(request.at("id"),issue);if(document.is_null()){restoreIssue_=issue;return true;}
        if(document.at("revision")!=request.at("revision")){restoreIssue_="pixel_restore_revision_changed";return true;}
        if(request.contains("left"))surface_.placement(request.at("left"),request.at("top"));
        const auto queued=surface_.show(document,true,request.at("scale"));if(!queued.value("ok",false))restoreIssue_=queued.value("error",PluginJson::object()).value("code",std::string("pixel_restore_unavailable"));return true;}catch(...){restoreIssue_="pixel_restore_unavailable";return true;}
    }
    PluginJson snapshot()const override{std::lock_guard lock(mutex_);auto value=listing();auto documents=value.value("documents",PluginJson::array());const auto count=documents.size();while(documents.size()>6)documents.erase(documents.end()-1);return {{"documents",documents},{"document_count",count},{"maximum",maximum_},{"can_create",value.value("occupied_slots",static_cast<std::size_t>(maximum_))<static_cast<std::size_t>(maximum_)},{"surface",surface_.snapshot()},{"workspace_root",workspaceIdentity(draftsRoot_)},{"restore_desktop_artwork",restoreDesktop_},{"restore_issue",restoreIssue_},{"content_kind","authored_pixel_art"},{"guide","可以画一件小作品、接着修改、预览实际画面或放到桌面。它们是自己创作的图形；动作接受、文件写入和桌面实际展示有分别的回执。不要求每轮创作或汇报。"}};}
    void shutdown()override{surface_.shutdown();}
};
}
extern "C" __declspec(dllexport) ayt::plugin::IPlugin* CreatePlugin(){return new PixelCanvas;}
