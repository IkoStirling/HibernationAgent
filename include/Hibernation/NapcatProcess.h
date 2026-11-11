#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace hibernation {
// App-specific lifecycle bridge. The separate host owns the engine process tree.
// Its generation survives a backend restart; no WebUI/API keys enter this IPC.
class NapcatProcess {
    std::filesystem::path config_;
public:
    explicit NapcatProcess(std::filesystem::path config);
    bool start();
    bool stop();
    nlohmann::json snapshot() const;
};
int runNapcatHost(const std::filesystem::path& config);
}
