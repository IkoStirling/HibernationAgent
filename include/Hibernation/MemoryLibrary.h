#pragma once
#include <Hibernation/MemoryContext.h>
#include <Hibernation/Review.h>
#include <Hibernation/ArchiveExperience.h>
#include <AYSerializer/JsonSchema.h>
namespace hibernation {
inline bool libraryArchiveKind(std::string_view kind){return kind=="user_message"||kind=="assistant_message"||kind=="story_end"||kind=="outer_event"||kind=="task_complete"||kind=="tool_attempt";}
inline Json libraryArchiveView(const Json& record){return libraryArchiveKind(record.value("kind",std::string{}))?record:archiveWorldExperience(record);}
inline std::string digestSchema(){return R"({"type":"object","additionalProperties":false,"required":["summary","keywords","uncertainties"],"properties":{"summary":{"type":"string","minLength":1,"maxLength":900},"keywords":{"type":"array","minItems":1,"maxItems":12,"items":{"type":"string","minLength":1,"maxLength":32}},"uncertainties":{"type":"array","maxItems":4,"items":{"type":"string","minLength":1,"maxLength":160}}}})";}
inline ValidatedOutput validateDigest(std::string_view text){if(!ayt::serializer::validateJsonDocument(text,digestSchema()))return {false,"memory_compaction_invalid",{}};auto value=Json::parse(text);if(value.dump().size()>6000||value.dump().find("\\u0000")!=std::string::npos)return {false,"memory_compaction_invalid",{}};std::set<std::string> seen;for(const auto& word:value.at("keywords"))if(!seen.insert(word.get<std::string>()).second)return {false,"memory_compaction_duplicate_keyword",{}};return {true,"ok",std::move(value)};}
// Byte offsets are explicit and always advance on UTF-8 boundaries.
inline Json libraryPage(Json data,std::size_t offset=0,std::size_t length=2400){
    const auto original=data.value("text",std::string{});offset=std::min(offset,original.size());while(offset>0&&offset<original.size()&&(static_cast<unsigned char>(original[offset])&0xc0)==0x80)--offset;
    auto end=std::min(original.size(),offset+length);while(end>offset&&end<original.size()&&(static_cast<unsigned char>(original[end])&0xc0)==0x80)--end;
    data["text"]=original.substr(offset,end-offset);data["offset"]=offset;data["next_offset"]=end;data["total_bytes"]=original.size();data["truncated"]=end<original.size();return data;
}
inline std::string memorySearchText(const Json& data){std::string result=data.value("text",std::string{});for(const auto& word:data.value("keywords",Json::array()))result+=' '+word.get<std::string>();if(data.value("kind",std::string{})=="tool_attempt"){const auto attempt=data.value("result",Json::object());result+=' '+attempt.value("target",Json::object()).value("id",std::string{});for(const auto* field:{"outcome","error_code"})result+=' '+attempt.value(field,std::string{});}return result;}
inline Json libraryContext(Json data){
    if(data.value("text",std::string{}).size()>1500){data=libraryPage(std::move(data),0,1200);data["context_note"]="原文保留在加密数据库，此处仅为节选。细节需memory.read或memory.archive_read分段读取；节选不代表完整内容。";}
    return data;
}
}
