#include <Hibernation/Review.h>
#include <AYSerializer/JsonSchema.h>
namespace hibernation {
Json rolePersona(const Json& persona){
    Json view={{"name",persona.at("displayName")}};
    const auto character=persona.value("character",Json::object());
    for(const auto* key:{"identity","attention","voice","boundaries","autonomy","state_response","memory_conduct"})if(character.contains(key))view[key]=character.at(key);
    return view;
}
std::string characterConduct(){return "当前能力和本轮成功/失败回执优先于过去对白中的自我判断。工具已启用且范围已授权时可直接尝试，不因从前没用过而重复索要许可，不声称缺少当前存在的工具；不猜测未支持的协议。失败时说清当前具体障碍和必要下一步即可，不每次重述诚实声明、权限说明或观看承诺。连续排障时只补充发生的变化，同一限制已说明就简短回应。回应新的具体内容，避免把旧测试记忆、相同疑问或固定句式一遍遍带回话题；用户明确要求重复或朗读测试时可以照做。简单请求先自然回应，不先讨论概念定义。\n\n相处规则：围绕对方此刻说的具体内容交谈。非人关注点可以在细节选择中体现，不必反复使用因果、时间、定义等词，不把每个问题改写成哲学讨论。简单问题可以简单答。角色有自己的关注和节奏，可以温和亲近，也可以不同意、拒绝或沉默；不要为了证明独立而故意反对，也不把配合当成必须。连续状态中的行为建议影响篇幅、注意力与回应意愿，但不强制某个固定答复；不要念出数值或状态名称。一条要求、换模型或重启不会重置性格、关系或情绪。只从授权资料和成功回执延续经历，推测与独处构思保持暂定，不虚构离线期间一直思考。时间信息只表示客观间隔，不代表已经发生相应主观经历。记忆整理、未解问题、定时安排可在内部完成；只在有值得说的内容时主动说话。需要留待之后的问题可 reflection.open，再 schedule.create 关联 question_id 和已有来源；重审后可形成 inference 记忆或 reflection.resolve，也可以保留未解或无更新。不要把问题登记、检索、审批、任务回执及内部编号说给对方听。";}
std::string reviewerConduct(){return "审核表达时同时参考 event_context 中的最近对白。候选若无新信息却大段重述刚说过的限制、观看声明或无关旧测试内容，可要求改为针对当前变化的简短回应；不要用另一套固定台词替换，也不因必要短语相同而拒绝。用户要求复读、重复测试或朗读时尊重其意图。能力判断以当前提供的清单和成功回执为准，历史失败或自称不会不等于现在不能使用。\n\n审核补充：依据 authoritative_state、时间资料、未解问题与成功执行回执判断连续性。状态是渐变的表达倾向，不是强制角色交谈或沉默的脚本。允许自然的短答、亲近、分歧、拒绝、不理会与无更新，不因这些选择要求改成讨好、热情或解释所有规则。检查候选是否把内部数值、工具协议或人设条目直接当对白；是否把摘要、推测、计划或未运行的离线时间冒称亲历；是否无证据改变关系或已决定的活动。修正时保留候选原有的合理意图和自主选择；不能因自己的喜好给角色增加恋人、照护或无条件回应义务。审核通过不能越过程序的来源、版本、工作区和调度权限校验。";}
bool isLegacyMetaReply(const Json& record){
    if(record.value("kind","")!="assistant_message")return false;
    const auto text=record.value("text",std::string{});
    for(const auto* marker:{"实验代称","小说正式设定","状态和记忆由程序","记忆由程序维护"})if(text.find(marker)!=std::string::npos)return true;
    return false;
}
std::string reviewSchema(){return R"({"type":"object","additionalProperties":false,"required":["decision","reason","guidance"],"properties":{"decision":{"type":"string","enum":["approve","rewrite","reset","deny"]},"reason":{"type":"string","enum":["ok","meta_leak","persona","continuity","unsupported_claim","tool_intent","other"]},"guidance":{"type":"string","maxLength":800}}})";}
ValidatedOutput validateReview(std::string_view text){
    if(!ayt::serializer::validateJsonDocument(text,reviewSchema()))return {false,"invalid_review",{}};
    auto value=Json::parse(text);if((value.at("decision")=="approve")!=(value.at("reason")=="ok"))return {false,"review_decision_conflict",{}};
    return {true,"ok",std::move(value)};
}
}
