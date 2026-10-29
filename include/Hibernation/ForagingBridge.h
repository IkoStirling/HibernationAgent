#pragma once
#include <Hibernation/RolePlugin.h>
#include <AYInventory/WorldTransactions.h>
namespace hibernation {
// Optional native interfaces; the existing role-plugin v2 ABI is unchanged.
// Only the owner-thread PluginRuntime coordinator calls these interfaces.
class IForagingInventory {
public:
    virtual ~IForagingInventory()=default;
    virtual PluginJson receiveCollection(const PluginJson& item,double remaining,
        ayt::inventory::IInteractionHost& source)=0;
};
class IForagingSource {
public:
    virtual ~IForagingSource()=default;
    using Import=std::function<PluginJson(const PluginJson&,double,ayt::inventory::IInteractionHost&)>;
    virtual PluginJson commitCollection(const std::string& token,const Import& import)=0;
};
}
