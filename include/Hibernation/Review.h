#pragma once
#include <Hibernation/Application.h>
#include <string_view>
namespace hibernation {
Json rolePersona(const Json& persona);
Json recordAuthorship(std::string_view kind,bool derived=false);
Json dialogueDeliveryContext();
std::string dialogueDeliveryConduct();
std::string characterConduct();
std::string reviewerConduct();
std::string toolFailureGuidance(std::string_view tool,std::string_view code);
bool isLegacyMetaReply(const Json& record);
std::string reviewSchema(bool withFocus=false,bool withContinuity=false);
bool advisoryRoleReview(const Json& verdict,bool roleOnly);
bool retryableReviewFailure(int status,int httpStatus);
ValidatedOutput validateReview(std::string_view text,bool withFocus=false,bool withContinuity=false);
}
