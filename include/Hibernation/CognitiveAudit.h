#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
namespace hibernation::cognitive_audit {
using Json=nlohmann::json;
Json activityTransitions(const Json& before,const Json& after,std::int64_t now,const std::string& reason);
Json metadata(const Json& record);
bool internalKind(const std::string&);
std::string sleepReceiptReason(const Json& event);
Json previousClosure(const Json& session);
}
