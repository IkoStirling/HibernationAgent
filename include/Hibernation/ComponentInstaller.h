#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
namespace hibernation {
// Application boundary, not a public engine API. EngineComponentInstaller binds
// the engine distribution Service; persona and UI do not own download mechanics.
struct ComponentInstallRequest {
    enum class Operation { Install, Update, Repair, Remove };
    Operation operation=Operation::Install;
    std::string componentId,version;
};
class ComponentInstaller {
public:
    virtual ~ComponentInstaller()=default;
    // Sanitized metadata only. Installation records are separate from archives.
    virtual nlohmann::json status()const=0;
    // Submit asynchronously; return accepted/job_id, not a false completion receipt.
    // The adapter resolves trusted release sources; callers cannot pass shell code.
    virtual nlohmann::json submit(const ComponentInstallRequest& request)=0;
};
inline nlohmann::json componentInstallationStatus(const std::shared_ptr<ComponentInstaller>& provider){
    return provider?provider->status():nlohmann::json{{"available",false},{"provider","none"},{"reason","engine_installation_not_connected"},{"local_import_available",true}};
}
}
