#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
namespace hibernation {
inline bool utf8WorkspaceText(std::string_view text){
    if(text.find('\0')!=std::string_view::npos)return false;
    try{const auto encoded=nlohmann::json(std::string(text)).dump();(void)encoded;return true;}
    catch(const nlohmann::json::type_error&){return false;}
}
}
