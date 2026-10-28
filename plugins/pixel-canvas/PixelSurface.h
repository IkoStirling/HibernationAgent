#pragma once
#include <Hibernation/RolePlugin.h>
#include <memory>
namespace hibernation::pixel {
// Commands can arrive on the model worker; all HWND/GDI work stays in tick()
// and shutdown() on the native plugin's UI owner.
class DesktopSurface {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    DesktopSurface();
    ~DesktopSurface();
    DesktopSurface(const DesktopSurface&)=delete;
    DesktopSurface& operator=(const DesktopSurface&)=delete;
    void configure(bool desktopEnabled,const PluginJson& configuration,
                   std::function<void(PluginJson)> emit={});
    PluginJson show(const PluginJson& document,bool visible,double scale);
    PluginJson hide(const std::string& documentId);
    bool placement(int left,int top);
    void tick(double elapsedSeconds);
    void event(const PluginJson& event);
    PluginJson snapshot()const;
    void reset();
    void shutdown();
};
}
