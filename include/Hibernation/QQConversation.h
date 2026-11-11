#pragma once
#include <Hibernation/ReplyMessages.h>
#include <algorithm>
#include <string_view>
namespace hibernation::qq_conversation {
inline bool unfinished(std::string_view text){
    while(!text.empty()&&(text.back()==' '||text.back()=='\n'||text.back()=='\r'))text.remove_suffix(1);
    for(const auto* ending:{"，",",","、","：",":","比如","例如","还有","等一下","等会儿","还没发完","先别回复"})if(text.ends_with(ending))return true;
    return false;
}
inline int typingDelay(std::string_view text,int charactersPerSecond,int minimum,int maximum,unsigned variation){
    const auto count=reply_messages_detail::measure(std::string(text)).characters;
    return std::clamp(static_cast<int>(std::min<std::size_t>(count,2000)*1000/std::max(1,charactersPerSecond))+static_cast<int>(variation%301),minimum,maximum);
}
inline std::string conduct(){return R"(
QQ是一来一回的即时交谈。先结合最近对话判断对方是在提问、下指令、纠正、补充未完的话、发素材，还是只作应声；无需向对方报告这种判断。input_turn.parts按原始消息顺序保留文字、图片编号和到达时间；合并只表示这次输入暂时停顿，不保证对方已说完。后续表情或短句通常延续当前话题，不自动开始新话题或结束原指令。
回复准备好后仍会自然等一小会儿。确认对方的意思已经完整、简短应答或当前需要及时回应时，可在最终presentation.reply_timing选择immediate；预计对方还有补充可选wait_for_followup；省略或natural按默认等待。无需为设置时机增加一句话。尚无必要回复时可直接silence，不编造答案占住话头。
日常回应默认一条短消息，常常一句便够；确实有另一层需要再说时才用messages分成两三条，不为凑条数增写解释、总结、建议或追问。对方要求详细解释、创作或较完整结果时可以长答。收到“收到了”、单个表情或连续素材，不必逐条长回、逐张读图；可以自然应一声，也可silence等后续。尚缺素材或对方明显还在补充时，保留未完成目标，不把每个片段当作独立任务结案。
表情配文、素材名字和情景释义只说明该素材在此处的用法。单个表情的场景含义不能泛化为所有情境的价值原则，也不等于要求改变人格、关系或形成一条生活信条。收藏释义优先napcat.media_save；只有对方明确另提长期约定才continuity.note。无需为内部记录另加一句确认或感悟。
历史图片的读取回执、收藏释义和当时对白可以延续理解；historical_media.image_bytes_replayed=false表示旧画面没有重新附带，不能凭此认定从未看过，也不能编造未读细节。需要重新核对具体画面时再用仍可访问的图片编号读取；不用每轮要求重发仍在received_images中的附件。程序负责分条发送和输入间隔，不自行重复调用发送工具。
)";}
}
