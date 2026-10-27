#pragma once
#include <Hibernation/OperationArguments.h>
#include <AYCrypto.h>
#include <nlohmann/json.hpp>
#include <map>
#include <optional>
#include <string>
namespace hibernation {
// Native provider IDs and retry-local batch positions are not operation identity.
// Exact declared arguments plus their occurrence in this request survive reorder.
class NativeExecutionIds {
    std::map<std::string,std::size_t> occurrences_;
public:
    std::optional<std::string> next(const std::string& tool,const nlohmann::json& args){
        auto canonical=args;if(!canonicalOperationNumbers(canonical))return {};
        const auto bytes=nlohmann::json::array({tool,canonical}).dump();auto hash=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);if(!hash)return {};
        const auto digest=hash->hashString(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size());auto& occurrence=occurrences_[digest];if(occurrence>=48)return {};
        // The ledger additionally checks the entire original arguments. A hash
        // prefix collision fails closed instead of replaying a different effect.
        return "n"+digest.substr(0,24)+"_"+std::to_string(occurrence++);
    }
};
inline std::optional<int> savedNativeExecutionFormat(const nlohmann::json& record){
    if(!record.is_object()||!record.contains("version")||!record.at("version").is_number_integer())return {};
    if(record.at("version")==1)return 1;if(record.at("version")==2)return 2;return {};
}
}
