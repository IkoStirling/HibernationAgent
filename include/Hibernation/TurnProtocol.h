#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
namespace hibernation {
struct TurnValidation {bool ok=false;std::string error;nlohmann::json value;};
std::string turnSchema();
/// A bounded authored disposition for later continuity, never internal reasoning.
nlohmann::json personalCarrySchema();
/// Strict-provider schema enumerates only currently enabled tool argument shapes.
std::string turnProviderSchema(const nlohmann::json& catalog);
/// Explicit compatibility rule: null for a known optional argument means omitted.
/// Required values and unknown properties are never dropped or coerced.
nlohmann::json normalizeToolArguments(nlohmann::json arguments,const nlohmann::json& schema);
TurnValidation validateTurn(std::string_view response);
/// Optional application compatibility candidate: bounded plain dialogue only.
/// JSON/code/tool-shaped content is rejected; it never creates an action. A host
/// must still apply the normal final schema and semantic review before display.
TurnValidation plainReplyCandidate(std::string_view response);
}
