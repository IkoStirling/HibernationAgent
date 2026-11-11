#pragma once
#include "Protocol.h"
#include <Hibernation/Vault.h>
namespace hibernation::napcat {
inline bool supersededReply(const std::filesystem::path& root,const PluginJson& row,bool mark=false){
    const auto local=conversation(root,row);const auto database=local/"archive/records.sqlite";if(!mark&&!std::filesystem::is_regular_file(database))return false;
    if(mark)std::filesystem::create_directories(local/"archive");ayt::storage::RecordStore store;
    if(!store.open(database,{openVaultCodec(local),false,mark}))throw std::runtime_error("reply_visibility_unavailable");
    const auto id=row.at("id").get<std::string>();ayt::storage::RecordQuery query;query.scope="ha.channel-delivery";query.id=id;query.limit=1;query.maxPayloadBytes=4096;
    const auto prior=store.query({{"ha.channel-delivery"}},query);if(!prior)throw std::runtime_error("reply_visibility_unavailable");
    if(!prior.records.empty())return PluginJson::parse(prior.records[0].payload).value("status",std::string{})=="superseded_before_send";
    if(!mark)return false;
    const PluginJson value={{"version",1},{"request_id",id},{"status","superseded_before_send"},{"confirmed_parts",0},{"reason","new_user_input_before_first_reply"},{"created_ms",nowMs()}};
    if(!store.commit({{"ha.channel-delivery",id,0,1,value.dump(),"napcat-delivery"}},id,nowMs()))throw std::runtime_error("reply_visibility_write");return true;
}
}
