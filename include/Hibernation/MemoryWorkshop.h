#pragma once
#include <Hibernation/Application.h>
namespace hibernation::workshop {
void validateSettings(const Json& value);
void validateDashboard(const Json& value);
std::filesystem::path backupRoot(const Config& config);
bool validateRoots(const Config& config);
Json planBackup(const Config& config,const std::string& id);
Json copyBackupItem(const Json& plan,std::size_t index);
Json finishBackup(const Json& plan,const Json& receipts);
}
