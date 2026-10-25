#pragma once
#include <memory>
#include <string>
namespace hibernation {
class StartupSplash {
public:
    StartupSplash(bool enabled,bool reduced,std::string palette="forest");
    ~StartupSplash();
    void ready();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
