#pragma once
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <string>
namespace hibernation {
inline bool operatorStyleTool(const std::string& name){return name.starts_with("style_lora.");}
// A small mechanical gate, not a claim of semantic equivalence. The independent
// reviewer still compares original and proposed wording, including uncertainty.
inline bool styleLiteralIntegrity(const std::string& original,const std::string& proposed){
    if(proposed.empty()||proposed.size()>12000||proposed.find('\0')!=std::string::npos)return false;
    static const std::regex atoms(R"((https?://[^\s<>"']+)|([-+]?[0-9]+(?:\.[0-9]+)?))");
    const auto collect=[&](const std::string& text){std::multiset<std::string> result;for(auto it=std::sregex_iterator(text.begin(),text.end(),atoms);it!=std::sregex_iterator();++it)result.insert(it->str());return result;};
    return collect(original)==collect(proposed);
}
}
