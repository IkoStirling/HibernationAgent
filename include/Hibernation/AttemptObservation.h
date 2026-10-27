#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace hibernation {
// Only actual domain refusals from an invoked, approved local operation qualify.
// No model/provider/audit/configuration error or untrusted error message becomes
// a lived experience. Target metadata comes from the application's live plugin.
inline nlohmann::json failedDomainAttempt(const std::string& tool,const nlohmann::json& args,const nlohmann::json& receipt,const nlohmann::json& target){
    using Json=nlohmann::json;
    try{
    if(!receipt.is_object()||receipt.value("ok",false)||!target.is_object())return nullptr;
    const bool writing=tool=="atelier.write"||tool=="atelier.append",playing=tool=="pocket.press"||tool=="pocket.leave";
    if(!writing&&!playing)return nullptr;
    const auto code=receipt.value("error",Json::object()).value("code",std::string{});
    const bool conflict=writing?code=="atelier_revision_conflict":code=="pocket_revision_conflict";
    const bool ended=tool=="atelier.append"?code=="atelier_piece_not_draft":playing&&code=="pocket_not_playing";
    if(!conflict&&!ended)return nullptr;
    const auto key=writing?"id":"game_id";
    if(!args.contains(key)||!args.at(key).is_string()||!args.contains("expected_revision")||!args.at("expected_revision").is_number_integer()||!target.contains("id")||target.at("id")!=args.at(key)||!target.contains("revision")||!target.at("revision").is_number_integer())return nullptr;
    const auto expected=args.at("expected_revision").get<std::int64_t>(),current=target.at("revision").get<std::int64_t>();
    if(args.at(key).get_ref<const std::string&>().empty()||args.at(key).get_ref<const std::string&>().size()>96||expected<0||current<1||current>1000000000||conflict&&expected==current)return nullptr;
    if(ended){const auto state=target.value(writing?"stage":"status",std::string{});if(writing?state!="finished"&&state!="set_aside":state!="won"&&state!="left"&&state!="exhausted")return nullptr;}
    Json metadata={{"id",target.at("id")},{"revision",current}};const auto state=writing?"stage":"status";if(target.contains(state)&&target.at(state).is_string())metadata[state]=target.at(state);
    return {{"attempted",true},{"succeeded",false},{"applied",false},{"completed",false},{"outcome",conflict?"version_changed":"target_no_longer_active"},{"error_code",code},{"expected_revision",expected},{"target_kind",writing?"authored_piece":"puzzle"},{"target",metadata},{"target_observed_after_attempt",true}};
    }catch(...){return nullptr;}
}
}
