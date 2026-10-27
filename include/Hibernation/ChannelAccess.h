#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>
namespace hibernation {
// One policy for dashboard, native adapter and host-side enforcement. A preset
// constrains effective values without destroying the user's individual choices.
struct ChannelAccess {
    bool strict=false,isolateMemory=false,desktopControl=true,review=true;
    std::string controllers="owner";
    static ChannelAccess read(const nlohmann::json& j){
        ChannelAccess p;
        for(const auto* key:{"stricterSecurity","isolateMemory","allowDesktopControl","reviewReplies"})
            if(j.contains(key)&&!j.at(key).is_boolean())throw std::runtime_error("napcat_security_type");
        p.strict=j.value("stricterSecurity",false);p.isolateMemory=j.value("isolateMemory",false);
        p.desktopControl=j.value("allowDesktopControl",true);p.review=j.value("reviewReplies",true);
        p.controllers=j.value("controlTrust",std::string("owner"));
        if(p.controllers!="owner"&&p.controllers!="allowed"&&p.controllers!="none")throw std::runtime_error("napcat_control_trust");
        if(p.strict){p.isolateMemory=true;p.desktopControl=false;p.review=true;p.controllers="owner";}
        return p;
    }
    bool controls(bool owner)const{return desktopControl&&controllers!="none"&&(owner||controllers=="allowed");}
    nlohmann::json json()const{return {{"stricterSecurity",strict},{"isolateMemory",isolateMemory},{"allowDesktopControl",desktopControl},{"reviewReplies",review},{"controlTrust",controllers}};}
};
}
