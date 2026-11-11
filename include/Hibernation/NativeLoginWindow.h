#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <cstdint>
namespace hibernation {
class NativeLoginWindow {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    NativeLoginWindow(std::uintptr_t owner,std::string url,std::filesystem::path profile);
    ~NativeLoginWindow();
    bool show();
    void update();
};
}
