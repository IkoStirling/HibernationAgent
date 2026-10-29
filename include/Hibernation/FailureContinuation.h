#pragma once
#include <Hibernation/SessionProjection.h>

namespace hibernation::failure_continuation {
using Json=nlohmann::json;
inline void completed(session::Prepared& run,const std::string& tool,const Json& args,const Json& receipt,const std::string& executionId){
    run.failureOperations.push_back({{"tool",tool},{"execution_id",executionId},{"args",args},{"receipt",receipt},{"authority","program_execution_receipt"}});
}
inline Json outcome(const session::Prepared& run,const std::string& source,const std::string& trace,const std::string& code,bool includeOperations=true){
    return {{"version",1},{"source_event_id",source},{"trace_id",trace},{"reply_status","not_approved"},{"reply_delivered",false},{"failure_code",code},{"completed_operations",includeOperations?run.failureOperations:Json::array()},{"context_invalidated",!includeOperations},
        {"meaning","本轮候选回复没有向对方展示；被拒对白不是角色说过的话，也不是经历。用户输入与这里的程序回执仍是事实。context_invalidated=true表示操作详情没有重放，不表示没有做过操作。ok=false是已尝试但失败；accepted/queued不等于最终完成。不要仅因回复未送达而重做已有操作；按最新用户意图继续，真正的新要求可以再执行。自然衔接即可，不必主动解释内部审核或编造上一轮对白。"}};
}
inline std::vector<session::Message> messages(const session::Prepared& run,const Json& event){
    std::vector<session::Message> result{run.failureInput};
    // Keep the originally sent input intact. Actual operations become explicit
    // program events, independent of native/JSON tool transcript conventions.
    for(const auto& operation:run.failureOperations)result.push_back({"user",Json{{"operation_receipt",operation}}.dump()});
    Json changes=Json::object();for(const auto* key:{"conversation_focus","expression_preference","turn_constraints","perception","body","authoritative_state","current_capabilities"})
        if(run.failureFrame.contains(key)&&(!run.fullFrame.contains(key)||run.failureFrame.at(key)!=run.fullFrame.at(key)))changes[key]=run.failureFrame.at(key);
    if(!changes.empty())result.push_back({"user",Json{{"runtime_event",{{"version",1},{"kind","turn.failed_state"},{"source_event_id",event.at("source_event_id")},{"sequence",1},{"changes",changes}}}}.dump()});
    // The final status is separate from receipts, so historical facts cannot
    // accidentally be interpreted as an approved or delivered assistant reply.
    auto marker=event;marker.erase("completed_operations");result.push_back({"user",Json{{"turn_outcome",marker}}.dump()});return result;
}
}
