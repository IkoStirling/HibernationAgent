#pragma once
#include <cstddef>
#include <optional>
#include <initializer_list>
#include <string_view>
namespace hibernation {
// Only remove a complete transport wrapper. The caller still validates the
// entire document with its own schema; no substring recovery or type coercion.
inline std::optional<std::string_view> modelJsonEnvelope(std::string_view text,std::size_t maximum=128*1024){
    if(text.size()>maximum)return {};
    const auto trim=[](std::string_view& value){while(!value.empty()&&(value.front()==' '||value.front()=='\t'||value.front()=='\r'||value.front()=='\n'))value.remove_prefix(1);while(!value.empty()&&(value.back()==' '||value.back()=='\t'||value.back()=='\r'||value.back()=='\n'))value.remove_suffix(1);};
    trim(text);if(text.starts_with("\xef\xbb\xbf")){text.remove_prefix(3);trim(text);}
    if(text.starts_with("```")){
        std::size_t header=0;for(const auto prefix:{std::string_view("```json\r\n"),std::string_view("```json\n"),std::string_view("```JSON\r\n"),std::string_view("```JSON\n"),std::string_view("```\r\n"),std::string_view("```\n")})if(text.starts_with(prefix)){header=prefix.size();break;}
        if(!header||!text.ends_with("```")||text.size()<header+3)return {};
        text.remove_prefix(header);text.remove_suffix(3);trim(text);
    }
    if(text.empty())return {};return text;
}
}
