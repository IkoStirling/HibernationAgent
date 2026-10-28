#pragma once
#include "PixelDocument.h"
#include <AYIO/Workspace.h>
#include <filesystem>
#include <functional>
namespace hibernation::pixel {
class PixelStore {
    ayt::io::Workspace files_;
    std::filesystem::path root_;
    std::size_t maximum_=32;
    PluginJson load(const std::string& id,std::string& issue)const;
    bool save(const std::string& id,const PluginJson& record,std::string& issue)const;
public:
    bool configure(const std::filesystem::path& draftsRoot,std::size_t maximum,std::string& issue);
    PluginJson list(std::string& issue)const;
    PluginJson read(const std::string& id,std::string& issue)const;
    PluginJson create(const PluginJson& args,std::int64_t now,std::string& issue);
    PluginJson revise(const std::string& id,std::int64_t expected,std::int64_t now,const std::function<bool(PluginJson&,std::string&)>& change,std::string& issue);
    PluginJson undo(const std::string& id,std::int64_t expected,std::int64_t now,std::string& issue);
    PluginJson redo(const std::string& id,std::int64_t expected,std::int64_t now,std::string& issue);
    PluginJson exportDocument(const std::string& id,std::int64_t expected,bool sheet,int frame,const std::function<bool()>& cancelled,std::string& issue);
    PluginJson preview(const std::string& id,int frame,std::string& issue)const;
};
}
