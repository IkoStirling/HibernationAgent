#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>

namespace hibernation {
inline nlohmann::json compactChannelDialogue(nlohmann::json data){
    if(!data.contains("channel_identity")||!data.at("channel_identity").is_object())return data;
    const auto& original=data.at("channel_identity");nlohmann::json who=nlohmann::json::object();
    for(const auto* key:{"channel","transport","self_user_id","sender_user_id","sender_is_owner","message_type","group_id","message_id","input_message_ids","current_input_images","input_turn"})if(original.contains(key))who[key]=original.at(key);
    if(who.contains("input_turn")&&who.at("input_turn").contains("parts"))for(auto& part:who["input_turn"]["parts"]){part.erase("text");part["text_in_message"]=true;}
    data["channel_identity"]=std::move(who);return data;
}
inline bool assistantDialogueRecord(const nlohmann::json& value) {
    return value.value("kind",std::string{})=="assistant_message"||
        (value.value("kind",std::string{})=="summary"&&value.value("original_kind",std::string{})=="assistant_message");
}
// This is a working-context projection, never an archive accessibility rule.
// Explicit authorized archive retrieval can still recover the original facts.
inline bool dialogueExcluded(const nlohmann::json& policy,const std::string& id) {
    const auto found=policy.find("excluded_assistants");
    return found!=policy.end()&&found->is_array()&&std::find(found->begin(),found->end(),id)!=found->end();
}
inline bool dialogueDigestExcluded(const nlohmann::json& policy,const nlohmann::json& memory) {
    const auto source=memory.value("compacted_from",nlohmann::json::object());
    return source.is_object()&&source.value("scope",std::string{})=="ha.archive"&&dialogueExcluded(policy,source.value("id",std::string{}));
}
inline nlohmann::json rebuildDialogueProjection(const nlohmann::json& previous,
    const nlohmann::json& retainedIds,const nlohmann::json& candidates,std::int64_t now,
    const std::string& reason) {
    std::set<std::string> wanted;
    const auto add=[&](const nlohmann::json& values){if(values.is_array())for(const auto& value:values)if(value.is_string())wanted.insert(value.get<std::string>());};
    add(previous.value("excluded_assistants",nlohmann::json::array()));add(candidates);
    auto excluded=nlohmann::json::array();
    // The durable recent index has a program-owned maximum of 200. Retire old
    // IDs with it, instead of growing a permanent blacklist or using wall time.
    if(retainedIds.is_array())for(const auto& value:retainedIds)if(value.is_string()&&wanted.contains(value.get<std::string>())&&excluded.size()<200)excluded.push_back(value);
    const std::set<std::string> reasons={"meta_leak","persona","continuity","unsupported_claim","tool_intent","other","operator_requested"};
    return {{"version",1},{"generation",previous.value("generation",std::uint64_t(0))+1},
        {"excluded_assistants",std::move(excluded)},{"rebuilt_at_ms",now},
        {"reason",reasons.contains(reason)?reason:"other"}};
}
}
