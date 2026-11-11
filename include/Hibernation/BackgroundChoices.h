#pragma once
#include <Hibernation/RememberedSchedule.h>
#include <Hibernation/TurnProtocol.h>
namespace hibernation {
inline Json backgroundScheduleParameters(){
    return {{"type","object"},{"additionalProperties",false},{"required",Json::array({"id","topic","memory","when","source_ids"})},{"properties",{
        {"id",{{"type","string"},{"minLength",1},{"maxLength",96}}},
        {"topic",{{"type","string"},{"minLength",1},{"maxLength",500}}},
        {"memory",{{"type","string"},{"enum",Json::array({"short","medium","long","trigger"})}}},
        {"when",reminder::whenSchema()},
        {"source_ids",{{"type","array"},{"minItems",1},{"maxItems",8},{"items",{{"type","string"},{"minLength",1},{"maxLength",256}}}}}
    }}};
}
inline std::string backgroundChoiceConduct(){return "\n可选feeling沿用self.feel的source_id和stimulus格式，对evidence中实际经历提出有限情绪/愿望刺激；不得用同批未来动作当经历，不要求每批表态。可选schedule是一个schedule.create提案，仅在schedule_configuration.available=true时使用：id/topic/memory/when/source_ids。source_ids只选evidence中本次实际提供的来源。它用于想以后再做的事，而非立刻执行的commands；先参考existing避免同一计划反复创建。不要求每轮安排，不为展示自主性编造任务。计划唤醒允许沉默、取消或调整，不等于已经完成，也不强制主动找用户说话。程序沿用来源、权限、数量、概率、遗忘、预算和作息检查，只有后续回执成功才算安排成功。";}
}
