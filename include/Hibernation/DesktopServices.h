#pragma once
#include <Hibernation/Application.h>
#include <Hibernation/RolePlugin.h>
#include <optional>
namespace hibernation {
std::string encodeJpegBase64(const std::vector<std::uint8_t>& pixels,int width,int height,int maxWidth=960);
std::string imageBase64(const std::vector<std::uint8_t>& bytes);
/// Decode already-authorized bytes, bound dimensions, and produce one JPEG frame.
/// Never opens a path or fetches a URL. Animated sources expose frame zero only.
Json prepareImageObservation(std::string_view bytes);
/// Application authority for native actions and bounded read-only command interpretation.
/// No arbitrary operating-system shell is exposed.
class DesktopServices {
public:
    explicit DesktopServices(const Config& cfg,std::function<Json()> widgetGeometry={});~DesktopServices();
    bool configure(std::string& error);
    Json capabilities()const;
    std::optional<double> inputIdleSeconds()const;
    Json invoke(const std::string& name,const Json& args,const std::function<bool()>& cancelled);
    Json browser(const std::string& operation,const Json& args,const std::function<bool()>& cancelled);
    Json webVideoPlayback()const;
    void tick();void stop();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
