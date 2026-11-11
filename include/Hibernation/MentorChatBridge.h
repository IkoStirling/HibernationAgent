#pragma once
#include <Hibernation/Application.h>
#include <functional>
#include <memory>
namespace hibernation {
/// Experimental, visible ChatGPT Chat UI only. No cookies, hidden APIs or model JS.
/// Construct/tick/stop on the native STA owner. Send runs on a waiting worker.
class MentorChatBridge {
public:
    explicit MentorChatBridge(const Config& cfg);
    ~MentorChatBridge();
    /// open accepts visible=false to hide only the native parent and preserve page layout.
    /// Omitting visible shows the window for the user to manage login manually.
    Json invoke(const std::string& operation,const Json& args,const std::function<bool()>& cancelled={});
    void tick();
    void stop();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
