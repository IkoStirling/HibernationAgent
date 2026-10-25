#pragma once
#include <Hibernation/Application.h>
namespace hibernation {
Json rolePersona(const Json& persona);
std::string characterConduct();
std::string reviewerConduct();
bool isLegacyMetaReply(const Json& record);
std::string reviewSchema();
ValidatedOutput validateReview(std::string_view text);
}
