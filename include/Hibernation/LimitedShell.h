#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <string_view>
#include <cwctype>
#include <string>
#include <vector>
namespace hibernation::limited_shell {
using Json=nlohmann::json;
struct Command {bool ok=false;std::string name,path,error;};
inline Command parse(const std::string& line){
    if(line.empty()||line.size()>1024||line.find_first_of("\r\n\0",0,3)!=std::string::npos)return {false,{},{},"shell_command_invalid"};
    std::vector<std::string> words;std::string word;char quote=0;bool started=false;
    for(char c:line){if(quote){if(c==quote)quote=0;else word+=c;started=true;continue;}if(c=='\''||c=='"'){quote=c;started=true;continue;}
        if(c==' '||c=='\t'){if(started){words.push_back(word);word.clear();started=false;}continue;}
        if(std::string_view(";&|<>`$%").find(c)!=std::string_view::npos)return {false,{},{},"shell_operators_not_supported"};word+=c;started=true;}
    if(quote)return {false,{},{},"shell_quote_unclosed"};if(started)words.push_back(word);if(words.empty()||words.size()>2)return {false,{},{},"shell_arguments_invalid"};
    auto name=words[0];std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(name=="ls"||name=="dir")name="list";if(name=="type"||name=="read")name="cat";
    if(name!="help"&&name!="roots"&&name!="pwd"&&name!="list"&&name!="cd"&&name!="cat")return {false,{},{},"shell_command_not_allowed"};
    if((name=="help"||name=="roots"||name=="pwd")&&words.size()!=1||name=="cat"&&words.size()!=2)return {false,{},{},"shell_arguments_invalid"};
    return {true,name,words.size()==2?words[1]:std::string{}, {}};
}
inline std::string pathText(const std::filesystem::path& p){const auto u=p.generic_u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
inline bool withinLexically(const std::filesystem::path& root,const std::filesystem::path& target){
    const auto fold=[](auto s){std::transform(s.begin(),s.end(),s.begin(),[](auto c){return std::towlower(c);});return s;};
    const auto a=root.lexically_normal(),b=target.lexically_normal();auto i=a.begin(),j=b.begin();for(;i!=a.end();++i,++j)if(j==b.end()||fold(i->native())!=fold(j->native()))return false;return true;
}
inline Json sessionRoute(Json route){route.erase("message_id");route.erase("input_message_ids");if(route.contains("identity")){route["identity"].erase("message_id");route["identity"].erase("input_message_ids");}return route;}
struct Location {bool ok=false;std::string root,path,error;};
inline Location resolve(std::string text,const Json& cwd,const Json& roots){
    std::replace(text.begin(),text.end(),'\\','/');if(text.starts_with("//"))return {false,{},{},"shell_network_paths_denied"};
    auto root=cwd.value("root",std::string("drafts")),relative=cwd.value("path",std::string{});
    if(text.empty())return {true,root,relative,{}};if(text=="/")return {true,{},{},{}};
    if(text.size()>=3&&std::isalpha(static_cast<unsigned char>(text[0]))&&text[1]==':'&&text[2]=='/'){
        const auto absolute=std::filesystem::u8path(text).lexically_normal();for(const auto& row:roots){const auto base=std::filesystem::u8path(row.at("path").get<std::string>());if(withinLexically(base,absolute)){auto part=absolute.begin();const auto normalizedBase=base.lexically_normal();for(auto i=normalizedBase.begin();i!=normalizedBase.end();++i)++part;std::filesystem::path tail;for(;part!=absolute.end();++part)tail/=*part;const auto rel=pathText(tail);return {true,row.at("id"),rel=="."?std::string{}:rel,{}};}}return {false,{},{},"shell_path_not_allowed"};
    }
    const auto colon=text.find(":/");if(colon!=std::string::npos){root=text.substr(0,colon);relative.clear();text=text.substr(colon+2);}
    else if(text.starts_with('/')){text.erase(text.begin());const auto slash=text.find('/');root=text.substr(0,slash);relative.clear();text=slash==text.npos?std::string{}:text.substr(slash+1);}
    else if(root.empty()){const auto slash=text.find('/');root=text.substr(0,slash);text=slash==text.npos?std::string{}:text.substr(slash+1);relative.clear();}
    if(std::none_of(roots.begin(),roots.end(),[&](const auto& row){return row.at("id")==root;}))return {false,{},{},"shell_root_not_allowed"};
    const auto normalized=(std::filesystem::u8path(relative)/std::filesystem::u8path(text)).lexically_normal();const auto path=pathText(normalized);
    if(normalized.is_absolute()||normalized.has_root_name()||path==".."||path.starts_with("../")||path.find(':')!=path.npos)return {false,{},{},"shell_path_escape"};
    return {true,root,path=="."?std::string{}:path,{}};
}
inline std::string display(const Json& cwd){const auto root=cwd.value("root",std::string{});return root.empty()?"/":root+":/"+cwd.value("path",std::string{});}
}
