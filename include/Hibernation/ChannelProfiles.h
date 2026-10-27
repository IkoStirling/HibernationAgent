#pragma once
#include <Hibernation/Application.h>
namespace hibernation {
// Private native mailbox DTO: environment variable names, never credentials.
inline Json channelProfile(const Profile& p){const auto& m=p.model;return {
    {"id",p.id},{"label",p.label},{"enabled",p.enabled},{"protocol",static_cast<int>(m.protocol)},
    {"origin",m.origin},{"basePath",m.basePath},{"model",m.model},{"credentialEnvironment",m.credentialEnvironment},
    {"supportsJsonSchema",m.supportsJsonSchema},{"supportsJsonObject",m.supportsJsonObject},{"supportsStreaming",m.supportsStreaming},{"supportsVision",m.supportsVision},{"supportsToolCalls",m.supportsToolCalls},{"reasoningEffort",m.reasoningEffort},
    {"legacyMaxTokens",m.legacyMaxTokens},{"timeoutMs",m.timeout.count()},{"maxOutputTokens",p.maxOutputTokens},
    {"proxyOrigin",m.proxyOrigin},{"useSystemProxy",m.useSystemProxy}};}
inline Profile readChannelProfile(const Json& j){Profile p;p.id=j.at("id");p.label=j.at("label");p.enabled=j.at("enabled");auto& m=p.model;
    m.protocol=static_cast<ayt::inference::Protocol>(j.at("protocol").get<int>());m.origin=j.at("origin");m.basePath=j.at("basePath");m.model=j.at("model");m.credentialEnvironment=j.at("credentialEnvironment");
    m.supportsJsonSchema=j.at("supportsJsonSchema");m.supportsJsonObject=j.value("supportsJsonObject",false);m.supportsStreaming=j.at("supportsStreaming");m.supportsVision=j.at("supportsVision");m.supportsToolCalls=j.value("supportsToolCalls",false);m.reasoningEffort=j.value("reasoningEffort",std::string{});m.legacyMaxTokens=j.at("legacyMaxTokens");
    m.timeout=std::chrono::milliseconds(j.at("timeoutMs").get<int>());p.maxOutputTokens=j.at("maxOutputTokens");m.proxyOrigin=j.at("proxyOrigin");m.useSystemProxy=j.at("useSystemProxy");
    m.maxRequestBytes=m.supportsVision?2*1024*1024:512*1024;m.maxResponseBytes=2*1024*1024;return p;}
}
