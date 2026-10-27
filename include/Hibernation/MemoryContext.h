#pragma once
#include <Hibernation/Application.h>
#include <Hibernation/TextPlayback.h>
#include <cctype>
#include <algorithm>
namespace hibernation {
struct MemoryVisibility {
    std::set<std::string> memories,archives;
    bool source(const Source& s)const{return !(s.scope=="ha.memory"?memories:archives).contains(s.id);}
    bool allows(const Json& memory)const{for(const auto& s:memory.at("sources"))if((s.at("scope")=="ha.memory"?memories:archives).contains(s.at("id").get<std::string>()))return false;return true;}
};
inline MemoryVisibility memoryVisibility(const std::vector<ayt::storage::VersionedRecord>& rows,const Json& hidden){
    MemoryVisibility result;for(const auto& id:hidden)result.memories.insert(id.get<std::string>());
    bool changed=true;while(changed){changed=false;for(const auto& r:rows){const auto value=Json::parse(r.payload);if(result.memories.contains(r.id)){for(const auto& s:value.at("sources"))if(s.at("scope")=="ha.archive")changed=result.archives.insert(s.at("id").get<std::string>()).second||changed;}else if(!result.allows(value))changed=result.memories.insert(r.id).second||changed;}}
    return result;
}
inline bool memoryCjkLetter(std::string_view scalar){
    if(scalar.size()!=3&&scalar.size()!=4)return false;
    std::uint32_t code=static_cast<unsigned char>(scalar[0])&(scalar.size()==3?0x0f:0x07);
    for(std::size_t i=1;i<scalar.size();++i){const auto byte=static_cast<unsigned char>(scalar[i]);if((byte&0xc0)!=0x80)return false;code=(code<<6)|(byte&0x3f);}
    return (code>=0x3400&&code<=0x9fff)||(code>=0xf900&&code<=0xfaff)||(code>=0x20000&&code<=0x3134f)||
        (code>=0x3041&&code<=0x3096)||(code>=0x30a1&&code<=0x30fa)||(code>=0xac00&&code<=0xd7af);
}
inline std::set<std::string> memoryTerms(std::string text){
    if(text.size()>8192)text.resize(8192);for(auto& c:text)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto offsets=textOffsets(text);std::set<std::string> terms;std::string word;
    for(std::size_t i=0;i+1<offsets.size();++i){const auto at=offsets[i],size=offsets[i+1]-at;const auto byte=static_cast<unsigned char>(text[at]);if(size==1&&std::isalnum(byte)){word+=text[at];continue;}if(!word.empty()){terms.insert(word);word.clear();}if(i+2<offsets.size()&&memoryCjkLetter(std::string_view(text).substr(at,size))&&memoryCjkLetter(std::string_view(text).substr(offsets[i+1],offsets[i+2]-offsets[i+1])))terms.insert(text.substr(at,offsets[i+2]-at));}if(!word.empty())terms.insert(word);return terms;
}
inline int memoryRelevance(const std::string& query,const std::string& text){const auto wanted=memoryTerms(query),actual=memoryTerms(text);int shared=0;for(const auto& term:wanted)if(actual.contains(term))++shared;return shared?std::min(45,10+shared*5):0;}
// Exact content grouping affects retrieval only; every original record survives.
inline std::string memoryContentKey(const Json& value){std::string text;bool space=false;for(unsigned char c:value.at("text").get<std::string>()){if(c<128&&std::isspace(c)){space=true;continue;}if(space&&!text.empty())text+=' ';space=false;text+=static_cast<char>(c);}return value.at("kind").get<std::string>()+":"+text;}
inline Json canonicalSources(Json refs){std::sort(refs.begin(),refs.end(),[](const auto& a,const auto& b){return a.dump()<b.dump();});return refs;}
inline void addMemoryGroup(std::map<std::string,Json>& groups,const ayt::storage::VersionedRecord& row,const Json& value){auto& group=groups[memoryContentKey(value)];if(group.is_null())group={{"count",0},{"records",Json::array()}};group["count"]=group.at("count").get<std::size_t>()+1;if(group.at("records").size()<8)group["records"].push_back({{"id",row.id},{"revision",row.revision},{"created_at",value.value("created_at",std::int64_t(0))}});}
}
