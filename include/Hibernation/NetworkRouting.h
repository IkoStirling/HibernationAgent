#pragma once
#include <cstdlib>
#include <string>
namespace hibernation {
// Application preference only. Never changes user/machine environment or registry.
inline void directNetworkEnvironment(bool browserSystemProxy=false){
#ifdef _WIN32
    for(const char* name:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})_putenv_s(name,"");
    if(!browserSystemProxy){
        const char* inherited=std::getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");
        std::string args=inherited?inherited:"";
        if(args.find("--no-proxy-server")==std::string::npos)args+=" --no-proxy-server";
        _putenv_s("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",args.c_str());
    }
#endif
}
}
