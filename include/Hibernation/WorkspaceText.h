#pragma once
#include <AYString/Utf8.h>
#include <string_view>
namespace hibernation {
inline bool utf8WorkspaceText(std::string_view text){
    if(text.find('\0')!=std::string_view::npos)return false;
    return ayt::string::isValidUtf8(text);
}
}
