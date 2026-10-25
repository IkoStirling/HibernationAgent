#pragma once
#include <Hibernation/Application.h>
#include <Hibernation/TextPlayback.h>
#include <cctype>
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
inline std::set<std::string> memoryTerms(std::string text){
    if(text.size()>8192)text.resize(8192);for(auto& c:text)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto offsets=textOffsets(text);std::set<std::string> terms;std::string word;
    for(std::size_t i=0;i+1<offsets.size();++i){const auto at=offsets[i],size=offsets[i+1]-at;const auto byte=static_cast<unsigned char>(text[at]);if(size==1&&std::isalnum(byte)){word+=text[at];continue;}if(!word.empty()){terms.insert(word);word.clear();}if(size>=3&&i+2<offsets.size()&&offsets[i+2]-offsets[i+1]>=3)terms.insert(text.substr(at,offsets[i+2]-at));}if(!word.empty())terms.insert(word);return terms;
}
inline int memoryRelevance(const std::string& query,const std::string& text){const auto wanted=memoryTerms(query),actual=memoryTerms(text);int shared=0;for(const auto& term:wanted)if(actual.contains(term))++shared;return shared?std::min(45,10+shared*5):0;}
}
