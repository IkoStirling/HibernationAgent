#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

namespace hibernation::mind {
using Json=nlohmann::json;
struct Settings {
    bool enabled=false,attention=true,observations=true;
    std::int64_t dwellMs=5000,resumeMinutes=15,observationCooldownMs=120000;
    static Settings read(const Json&);
    Json json()const;
};
// Application policy, not another model, event bus, emotional state or archive.
// Its clock advances only while the application is running and unpaused.
class State {
public:
    State();
    bool load(const Json&);
    Json snapshot()const{return data_;}
    void advance(std::int64_t elapsedMs,bool paused);
    void select(const Json& candidates,const Settings&);
    bool attend(const std::string& id,std::int64_t expectedRevision,const Settings&);
    void suspend(const Json& queue,const std::string& reason,const Settings&);
    Json resumable(const std::string& id,std::int64_t revision)const;
    bool settle(const std::string& id,std::int64_t revision,const std::string& status);
    void queued(const Json& command);
    void outcome(const Json& command,const std::string& status,bool late=false);
    void observed(const std::string& key);
    bool observationDue(const std::string& key,const Settings&)const;
    Json view()const;
    Json status()const;
private:
    Json data_;
    void changed();
};
Json tools();
std::string conduct();
bool privateTool(const std::string& name);
}
