#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
namespace hibernation {using PluginJson=nlohmann::json;}
namespace hibernation::pixel {
bool validId(const std::string& id);
PluginJson defaultPalette();
bool validateDocument(const PluginJson& document,std::string& issue);
PluginJson createDocument(const PluginJson& args,std::int64_t now,std::string& issue);
bool editDocument(PluginJson& document,int frame,const PluginJson& commands,std::string& issue);
bool changeFrame(PluginJson& document,const PluginJson& args,std::string& issue);
bool changePalette(PluginJson& document,int index,const std::string& rgba,std::string& issue);
PluginJson metadata(const PluginJson& document);
// RGBA bytes, either one selected frame or all frames packed horizontally.
std::vector<std::uint8_t> rgbaPixels(const PluginJson& document,bool sheet,int frame,int& width,int& height);
}
