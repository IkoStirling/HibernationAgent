#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hibernation {
inline constexpr std::size_t replyMessagesMaximum=6;
inline constexpr std::size_t replyMessageCharactersMaximum=2000;
inline constexpr std::size_t replyCharactersMaximum=6000;
struct ReplyMessagesValidation {bool ok=false;std::string error;nlohmann::json value;};
namespace reply_messages_detail {
struct Measure {bool valid=true,visible=false;std::size_t characters=0;};
inline bool blank(std::uint32_t c){return c==9||c==10||c==13||c==32||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200d)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x2060||c==0x3000||c==0xfeff;}
inline Measure measure(std::string_view text){
    Measure result;for(std::size_t offset=0;offset<text.size();){
        const auto lead=static_cast<unsigned char>(text[offset++]);std::uint32_t c=lead;unsigned continuation=0;std::uint32_t minimum=0;
        if(lead<0x80){}else if(lead>=0xc2&&lead<=0xdf){c=lead&0x1f;continuation=1;minimum=0x80;}else if(lead>=0xe0&&lead<=0xef){c=lead&0x0f;continuation=2;minimum=0x800;}else if(lead>=0xf0&&lead<=0xf4){c=lead&7;continuation=3;minimum=0x10000;}else{return {false,false,0};}
        for(unsigned index=0;index<continuation;++index){if(offset>=text.size())return {false,false,0};const auto byte=static_cast<unsigned char>(text[offset++]);if((byte&0xc0)!=0x80)return {false,false,0};c=(c<<6)|(byte&0x3f);}
        if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff)||(c<32&&c!=9&&c!=10&&c!=13)||(c>=0x7f&&c<=0x9f))return {false,false,0};
        ++result.characters;if(!blank(c))result.visible=true;
    }return result;
}
}
/// Optional message boundaries authored by the model. The canonical text limit
/// includes the two newline separators; ordinary text is never auto-split.
inline nlohmann::json replyMessagesSchema(){return {{"anyOf",nlohmann::json::array({{{"type","null"}},{{"type","array"},{"minItems",1},{"maxItems",replyMessagesMaximum},{"description","可选，同一次回复的1至6条独立消息；可自行选择一条长回复或多条短消息，不为凑条数扩写。使用时对应的完整对白字段可为null；程序以两个换行连接为完整对白，含分隔总长不超过6000字。"},{"items",{{"type","string"},{"minLength",1},{"maxLength",replyMessageCharactersMaximum}}}}})}};}
/// Validates the whole bundle before publishing any part. Returns a normalized
/// copy, retaining unrelated fields; schema/permissions remain the caller's job.
/// Null messages means omission for strict-provider compatibility. Legacy empty
/// text is allowed only when explicitly requested (e.g. quiet background output).
inline ReplyMessagesValidation normalizeReplyMessages(nlohmann::json value,std::string_view textField="text",bool allowEmptyText=false){
    using Json=nlohmann::json;const std::string key(textField);
    if(!value.is_object()||key.empty()||key=="messages"||!value.contains(key))return {false,"reply_shape",{}};
    if(value.contains("messages")&&value.at("messages").is_null())value.erase("messages");
    if(!value.contains("messages")){
        if(!value.at(key).is_string())return {false,"reply_shape",{}};const auto& text=value.at(key).get_ref<const std::string&>();
        if(text.size()>replyCharactersMaximum*4)return {false,"reply_text_size",{}};const auto measured=reply_messages_detail::measure(text);
        if(!measured.valid||(!allowEmptyText&&!measured.visible))return {false,"reply_text",{}};
        if(measured.characters>replyCharactersMaximum)return {false,"reply_text_size",{}};return {true,"ok",std::move(value)};
    }
    const auto& messages=value.at("messages");if(!messages.is_array()||messages.empty()||messages.size()>replyMessagesMaximum)return {false,"reply_messages_count",{}};
    std::string canonical;std::size_t characters=0;
    for(const auto& item:messages){
        if(!item.is_string())return {false,"reply_message_type",{}};const auto& text=item.get_ref<const std::string&>();
        if(text.size()>replyMessageCharactersMaximum*4)return {false,"reply_message_size",{}};const auto measured=reply_messages_detail::measure(text);
        if(!measured.valid||!measured.visible)return {false,"reply_message_text",{}};if(measured.characters>replyMessageCharactersMaximum)return {false,"reply_message_size",{}};
        if(!canonical.empty()){canonical+="\n\n";characters+=2;}canonical+=text;characters+=measured.characters;
        if(characters>replyCharactersMaximum)return {false,"reply_messages_total",{}};
    }
    if(!value.at(key).is_null()&&(!value.at(key).is_string()||value.at(key).get_ref<const std::string&>()!=canonical))return {false,"reply_messages_text_mismatch",{}};
    value[key]=std::move(canonical);return {true,"ok",std::move(value)};
}
/// Returns independently authored records from a fully validated bundle. Invalid
/// input is rejected as a whole, never silently downgraded to a fallback text.
inline std::vector<std::string> replyMessageParts(const nlohmann::json& value,std::string_view textField="text"){
    const auto checked=normalizeReplyMessages(value,textField,true);if(!checked.ok)throw std::invalid_argument(checked.error);
    if(checked.value.contains("messages"))return checked.value.at("messages").get<std::vector<std::string>>();
    const auto& text=checked.value.at(std::string(textField)).get_ref<const std::string&>();return text.empty()?std::vector<std::string>{}:std::vector<std::string>{text};
}
}
