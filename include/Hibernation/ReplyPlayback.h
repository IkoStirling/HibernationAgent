#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <string_view>
namespace hibernation {
inline std::size_t replyPlaybackCharacters(std::string_view text){std::size_t count=0;for(unsigned char c:text)if((c&0xc0)!=0x80)++count;return count;}
// One continuous voice has a single global cursor; record boundaries remain
// visible in history without restarting the voice for each authored message.
inline nlohmann::json replyPlaybackProjection(const nlohmann::json& parts,const nlohmann::json& playback){
    using Json=nlohmann::json;Json result=Json::object();std::uint64_t visible=0;
    if(const auto at=playback.find("visible_characters");at!=playback.end()&&at->is_number_integer()){
        if(at->is_number_unsigned())visible=std::min<std::uint64_t>(at->get<std::uint64_t>(),6000);
        else visible=static_cast<std::uint64_t>(std::clamp<std::int64_t>(at->get<std::int64_t>(),0,6000));
    }
    const auto status=playback.value("state",std::string("waiting"));
    for(const auto& part:parts){
        const auto start=part.at("offset").get<std::uint64_t>(),length=part.at("characters").get<std::uint64_t>();
        const auto local=visible>start?std::min(length,visible-start):0;
        auto value=playback;value["visible_characters"]=status=="completed"?length:local;
        if(status=="completed"||local==length)value["state"]="completed";
        else if(status=="failed"||status=="interrupted")value["state"]=status;
        else value["state"]=status=="speaking"&&visible>=start?"speaking":"waiting";
        result[part.at("reply_id").get<std::string>()]=std::move(value);
    }
    return result;
}
}
