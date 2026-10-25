#pragma once
#include <Hibernation/Application.h>
#include <Hibernation/RolePlugin.h>
namespace hibernation {
std::string encodeJpegBase64(const std::vector<std::uint8_t>& pixels,int width,int height,int maxWidth=960);
std::string imageBase64(const std::vector<std::uint8_t>& bytes);
/// Application authority for native actions. Model receives IDs, never a shell.
class DesktopServices {
public:
    explicit DesktopServices(const Config& cfg);~DesktopServices();
    bool configure(std::string& error);
    Json capabilities()const;
    Json invoke(const std::string& name,const Json& args,const std::function<bool()>& cancelled);
    Json browser(const std::string& operation,const Json& args,const std::function<bool()>& cancelled);
    void tick();void stop();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
