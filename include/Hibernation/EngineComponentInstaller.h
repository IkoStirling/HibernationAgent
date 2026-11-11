#pragma once
#include <Hibernation/ComponentInstaller.h>
#include <filesystem>
namespace hibernation {
// Load operator-owned pinned sources next to local.json, never from model output.
std::shared_ptr<ComponentInstaller> engineComponentInstaller(const std::filesystem::path& config);
}
