#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <Hibernation/ComponentInstaller.h>
namespace hibernation {
/// Operator controls, deliberately absent from the character tool catalog.
class DesktopIntegration {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    DesktopIntegration(std::filesystem::path configPath,
        std::wstring registrySubkey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        std::shared_ptr<ComponentInstaller> installer={});
    ~DesktopIntegration();
    static nlohmann::json settings(const nlohmann::json& input);
    bool canConfigure(const nlohmann::json& value)const;
    void configure(const nlohmann::json& value,const std::string& quickAccount);
    nlohmann::json status()const;
    nlohmann::json login()const;
    nlohmann::json action(const nlohmann::json& value);
    void stop();
};
}
