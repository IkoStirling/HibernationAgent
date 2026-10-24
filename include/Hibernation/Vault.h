#pragma once
#include <AYStorage/RecordStore.h>
#include <filesystem>
#include <memory>
#include <string>
namespace hibernation {
/// At-rest protection only. Keys are Windows-user protected; plaintext is
/// available in process memory and to the selected model as needed.
std::shared_ptr<const ayt::storage::IRecordPayloadCodec> openVaultCodec(const std::filesystem::path& dataRoot);
bool saveUserSecret(const std::filesystem::path& path,const std::string& value);
std::string loadUserSecret(const std::filesystem::path& path);
}
