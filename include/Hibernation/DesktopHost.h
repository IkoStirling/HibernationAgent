#pragma once
#include <Hibernation/Application.h>
#include <memory>
namespace hibernation {
class DesktopHost {
public:
    DesktopHost(Application& app,std::string dashboardUrl);
    ~DesktopHost();
    bool create();
    void show();
    void openDashboard();
    bool pump();
    void update();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
