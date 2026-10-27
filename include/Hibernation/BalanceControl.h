#pragma once
#include <Hibernation/Application.h>
#include <optional>
#include <regex>
namespace hibernation::balance {
inline std::string signature(const Profile& p){return Json::array({p.model.origin,p.model.basePath,p.model.credentialEnvironment,p.model.model}).dump();}
inline bool supported(const Profile& p){return p.model.protocol!=ayt::inference::Protocol::Mock&&(p.model.origin=="https://api.deepseek.com"||p.model.origin=="https://api.deepseek.com:443"||(p.model.model=="balance-fixture"&&p.model.origin.starts_with("http://127.0.0.1:")));}
// Strict bounded decimal syntax: neither malformed replies nor positive-looking
// strings can clear a persisted insufficient-balance latch.
inline std::optional<bool> available(const std::string& body){
    const auto value=Json::parse(body,nullptr,false);if(!value.is_object()||!value.contains("is_available")||!value.at("is_available").is_boolean()||!value.contains("balance_infos")||!value.at("balance_infos").is_array()||value.at("balance_infos").size()>8)return {};
    if(value.at("balance_infos").empty())return value.at("is_available")==false?std::optional<bool>(false):std::nullopt;
    bool positive=false;std::set<std::string> currencies;
    for(const auto& row:value.at("balance_infos")){
        if(!row.is_object()||!row.contains("currency")||!row.at("currency").is_string()||!row.contains("total_balance")||!row.at("total_balance").is_string())return {};
        const auto currency=row.at("currency").get<std::string>(),amount=row.at("total_balance").get<std::string>();
        if((currency!="CNY"&&currency!="USD")||!currencies.insert(currency).second||amount.size()>40||!std::regex_match(amount,std::regex("-?[0-9]{1,20}(\\.[0-9]{1,12})?")))return {};
        if(!amount.starts_with('-')&&amount.find_first_of("123456789")!=std::string::npos)positive=true;
    }
    if(value.at("is_available")==false)return false;
    return positive?std::optional<bool>(true):std::nullopt;
}
inline bool manual(const Json& control){return control.value("manual_offline",control.value("offline",false));}
inline bool offline(const Json& control){return manual(control)||control.value("balance_offline",false);}
}
