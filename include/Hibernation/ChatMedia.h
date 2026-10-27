#pragma once
#include <string>
#include <string_view>
#include <cctype>
#include <algorithm>
namespace hibernation {
inline std::string chatMediaMime(std::string extension){
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".png")return "image/png";if(extension==".jpg"||extension==".jpeg")return "image/jpeg";
    if(extension==".gif")return "image/gif";if(extension==".webp")return "image/webp";
    if(extension==".mp4"||extension==".m4v")return "video/mp4";if(extension==".webm")return "video/webm";return {};
}
inline bool chatMediaSignature(const std::string& mime,std::string_view bytes){
    if(mime=="image/png")return bytes.starts_with("\x89PNG\r\n\x1a\n");
    if(mime=="image/jpeg")return bytes.size()>3&&static_cast<unsigned char>(bytes[0])==255&&static_cast<unsigned char>(bytes[1])==216&&static_cast<unsigned char>(bytes[2])==255;
    if(mime=="image/gif")return bytes.starts_with("GIF87a")||bytes.starts_with("GIF89a");
    if(mime=="image/webp")return bytes.size()>12&&bytes.starts_with("RIFF")&&bytes.substr(8,4)=="WEBP";
    if(mime=="video/mp4")return bytes.size()>12&&bytes.substr(4,4)=="ftyp";
    if(mime=="video/webm")return bytes.starts_with("\x1a\x45\xdf\xa3");return false;
}
}
