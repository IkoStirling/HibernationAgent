#pragma once
#include <Hibernation/Application.h>
#include <functional>
namespace hibernation::people {
std::string subject(const Json& identity,bool owner);
Json tools();
std::string conduct();
using Accessible=std::function<bool(const Json&)>;
Json read(const std::filesystem::path& root,const std::string& person,const Json& args,const Accessible& allowed,std::int64_t now,bool contextual=false);
Json note(const std::filesystem::path& root,const std::string& person,const Json& args,const Json& source,const std::string& trace,std::int64_t now);
}
