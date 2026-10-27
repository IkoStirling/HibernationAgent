#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
namespace hibernation::reading {
using Json=nlohmann::json;
inline bool language(std::string_view value){return value=="zh"||value=="en"||value=="ja";}
inline std::string bounded(std::string value,std::size_t maximum){if(value.size()<=maximum)return value;while(maximum&&(static_cast<unsigned char>(value[maximum])&0xc0)==0x80)--maximum;value.resize(maximum);return value;}
inline std::string plainSnippet(std::string_view source){
    std::string result;bool tag=false;
    for(const char c:source){if(c=='<'){tag=true;continue;}if(c=='>'){tag=false;continue;}if(!tag)result.push_back(c);}
    // Decode only fixed display entities after removing markup. Nothing here is
    // rendered as HTML, fetched as a link, or interpreted as tool instructions.
    for(const auto& [from,to]:{std::pair{"&amp;","&"},{"&quot;","\""},{"&#39;","'"},{"&lt;","<"},{"&gt;",">"},{"&nbsp;"," "}}){std::size_t at=0;while((at=result.find(from,at))!=std::string::npos){result.replace(at,std::char_traits<char>::length(from),to);at+=std::char_traits<char>::length(to);}}
    return bounded(std::move(result),900);
}
inline bool title(std::string_view value){return !value.empty()&&value.size()<=512&&value.find_first_of("\r\n\0",0,3)==std::string_view::npos;}
inline Json searchResults(const Json& wire){
    if(!wire.is_object()||!wire.contains("pages")||!wire.at("pages").is_array())throw std::runtime_error("reading_search_format");
    Json result=Json::array();for(const auto& page:wire.at("pages")){
        if(result.size()==5)break;
        if(!page.is_object()||!page.contains("id")||!page.at("id").is_number_integer()||page.at("id").get<std::int64_t>()<=0||!page.contains("key")||!page.at("key").is_string()||!title(page.at("key").get_ref<const std::string&>())||!page.contains("title")||!page.at("title").is_string()||!title(page.at("title").get_ref<const std::string&>()))continue;
        Json row={{"page_id",page.at("id")},{"key",page.at("key")},{"title",page.at("title")},{"scope","search_excerpt"}};
        if(page.contains("excerpt")&&page.at("excerpt").is_string())row["excerpt"]=plainSnippet(page.at("excerpt").get_ref<const std::string&>());
        if(page.contains("description")&&page.at("description").is_string())row["description"]=bounded(page.at("description").get<std::string>(),600);
        result.push_back(std::move(row));
    }return result;
}
inline Json summary(const Json& wire){
    if(!wire.is_object()||!wire.contains("title")||!wire.at("title").is_string()||!title(wire.at("title").get_ref<const std::string&>())||!wire.contains("extract")||!wire.at("extract").is_string()||wire.at("extract").get_ref<const std::string&>().empty())throw std::runtime_error("reading_summary_format");
    const auto& text=wire.at("extract").get_ref<const std::string&>();
    Json result={{"title",wire.at("title")},{"text",bounded(text,6000)},{"text_truncated",text.size()>6000},{"scope","article_lead_summary"},{"full_article_read",false},{"content_kind","external_reference"},{"disambiguation",wire.value("type",std::string{})=="disambiguation"}};
    if(wire.contains("pageid")&&wire.at("pageid").is_number_integer()&&wire.at("pageid").get<std::int64_t>()>0)result["page_id"]=wire.at("pageid");
    if(wire.contains("revision")&&(wire.at("revision").is_number_integer()||wire.at("revision").is_string()))result["revision"]=wire.at("revision").is_string()?bounded(wire.at("revision").get<std::string>(),64):wire.at("revision").dump();
    if(wire.contains("description")&&wire.at("description").is_string())result["description"]=bounded(wire.at("description").get<std::string>(),600);
    return result;
}
}
