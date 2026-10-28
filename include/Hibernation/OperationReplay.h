#pragma once
#include <Hibernation/ObservationContext.h>
#include <Hibernation/OperationArguments.h>
#include <Hibernation/ActivityPlan.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace hibernation {
inline bool replayProtectedOperation(const std::string& tool){
    if(tool=="napcat.capture_and_send"||tool=="napcat.send_image")return true;
    static const std::set<std::string> names={"workspace.write","workspace.edit","activity.enqueue","habitat.create","habitat.act","habitat.arrange","atelier.write","atelier.append","pocket.start","pocket.press","pocket.leave","rhythm.rest","rhythm.cancel","schedule.create","schedule.cancel","memory.remember","memory.revise","reflection.open","reflection.resolve","self.notice","self.close","state.propose","stage.propose","computer.open_application","television.open","television.open_page"};
    return tool=="continuity.note"||names.contains(tool)||(tool.starts_with("task.")&&tool!="task.status")||pixelMutationTool(tool)||tool=="state.appraise"||tool=="mentor.execute";
}
class OperationReplay {
    using Json=nlohmann::json;
    std::map<std::string,std::vector<Json>> executed_,guarded_;
    std::map<std::string,std::size_t> cursor_;
    std::set<std::string> completedIds_;
    Json facts_=Json::array();
    static std::string signature(const std::string& tool,const Json& args){
        auto canonical=args;if(tool=="activity.enqueue"&&canonical.contains("commands")){
            const auto normalized=validateActivities(Json{{"version",1},{"commands",canonical.at("commands")}}.dump());if(normalized.ok)canonical["commands"]=normalized.value.at("commands");
        }return tool+":"+(canonicalOperationNumbers(canonical)?canonical:args).dump();
    }
public:
    void afterReviewRejection(){guarded_=executed_;cursor_.clear();}
    std::optional<Json> reuse(const std::string& tool,const Json& args){
        const auto key=signature(tool,args);const auto found=guarded_.find(key);if(found==guarded_.end())return {};
        auto& cursor=cursor_[key];if(cursor>=found->second.size())return Json{{"ok",false},{"error",{{"code","operation_already_recorded_after_review"},{"retryable",false}}},{"instruction","此前相同操作已有真实回执，修正对白无需再次安排或执行；使用已有事实继续本轮。"}};
        auto output=found->second[cursor++];output["reused_after_review"]=true;return output;
    }
    void completed(const std::string& tool,const Json& args,const Json& output,const std::string& callId){
        if(!replayProtectedOperation(tool)||!output.value("ok",false)||output.value("reused_after_review",false)||!completedIds_.insert(callId).second)return;
        executed_[signature(tool,args)].push_back(output);
        const auto observation=compactObservation(Json{{"kind","tool_observation"},{"tool",tool},{"result",output.value("result",Json::object())}},false);
        facts_.push_back({{"tool",tool},{"call_id",callId},{"source_id",output.value("source_id",std::string{})},{"result",observation.at("result")},{"authority","program_execution_receipt"}});
    }
    const Json& executionFacts()const{return facts_;}
};
}
