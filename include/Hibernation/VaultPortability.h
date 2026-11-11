#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hibernation::portableVault {
using Json=nlohmann::json;
using Cancellation=std::shared_ptr<std::atomic<bool>>;
void clearSensitiveBuffer(std::string& value)noexcept;
// These owner-only management functions never return plaintext keys or memories.
// Passwords are moved into a scoped, zeroed buffer. Callers must clear their own
// input fields/request buffers and must not persist or log the request body.
Json exportBundle(const std::filesystem::path& dataRoot,std::string password,const Cancellation& cancel={});
Json verifyBackup(const Json& bundle,std::string password,const std::filesystem::path& backupRoot,const Cancellation& cancel={});
// targetRoot is a NEW data directory. No existing path is overwritten or deleted.
// protectedRoots must include the running data, workspace and installation roots.
Json restoreBackup(const Json& bundle,std::string password,const std::filesystem::path& backupRoot,const std::filesystem::path& targetRoot,const std::vector<std::filesystem::path>& protectedRoots={},const Cancellation& cancel={});
}
