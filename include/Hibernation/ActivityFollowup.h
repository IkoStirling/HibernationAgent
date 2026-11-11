#pragma once
#include <Hibernation/Application.h>
namespace hibernation {
inline Json activityFollowupParameters(){return {{"type","object"},{"additionalProperties",false},{"required",Json::array({"topic","timeout_seconds"})},{"properties",{
    {"topic",{{"type","string"},{"minLength",1},{"maxLength",500}}},
    {"timeout_seconds",{{"type","integer"},{"minimum",5},{"maximum",600}}}
}}};}
inline std::string activityFollowupConduct(){return "\n在原会话明确表示完成某件事后再告诉对方时，将真实动作放入activity.enqueue，并在同一调用附上follow_up:{topic:稍后要接续的事情,timeout_seconds:5到600}。只有回执follow_up.registered=true才表示接续已登记；未登记时不能许诺程序会稍后自动回应。它绑定这一批实际活动，完成、失败、取消或超时后由原调度器给你一次接续机会，不依赖新的用户消息。普通意向、随手动作、没承诺后续结果时不必登记。排队不是已经完成，超时不是成功；先依据实际回执判断，有结果再回应，发生变化也可以如实解释或选择安静，避免重复许诺再等一下。没有焦糊等属性或实际观察时，不虚构已经尝到的细节。接续绑定创建时的原会话：桌面对话回到桌面，号主QQ对话回到原私聊或原群聊；后续切换聊天位置不会改变这一接续的去向。";}
}
