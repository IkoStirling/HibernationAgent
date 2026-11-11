#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <algorithm>
#include <string_view>
#include <set>
#include <cstdint>
namespace hibernation::intention_life {
using Json=nlohmann::json;
inline bool solitudeJob(std::string_view kind){return kind=="role.solitude"||kind=="role.personal-pursuit";}
inline bool available(const Json& thread,std::int64_t now){return thread.value("status",std::string{})=="active"&&now>=thread.value("intention_pause",Json::object()).value("until_ms",std::int64_t(0));}
inline Json reconsiderParameters(){return Json::parse(R"({"type":"object","additionalProperties":false,"required":["id","expected_revision","choice","note","source_ids"],"properties":{"id":{"type":"string","minLength":1,"maxLength":96},"expected_revision":{"type":"integer","minimum":1},"choice":{"enum":["pause","resume"]},"when":{"enum":["soon","later","much_later"]},"note":{"type":"string","minLength":1,"maxLength":400},"source_ids":{"type":"array","minItems":1,"maxItems":8,"items":{"type":"string","minLength":1,"maxLength":256}}}})");}
inline std::int64_t pauseMs(const std::string& horizon){return horizon=="much_later"?7200000:horizon=="later"?1800000:300000;}
inline Json reconsidered(Json thread,const Json& proposal,const Json& sources,std::int64_t now){
    if(proposal.at("choice")=="resume")thread["intention_pause"]={{"until_ms",0},{"choice","resume"},{"note",proposal.at("note")},{"decided_ms",now}};
    else thread["intention_pause"]={{"until_ms",now+pauseMs(proposal.value("when",std::string("soon")))},{"choice","pause"},{"when",proposal.value("when",std::string("soon"))},{"note",proposal.at("note")},{"decided_ms",now}};
    thread["intention_pause"]["sources"]=sources;
    auto refs=thread.value("decision_sources",Json::array());for(const auto& ref:sources)if(std::find(refs.begin(),refs.end(),ref)==refs.end())refs.push_back(ref);
    thread["decision_sources"]=std::move(refs);return thread;
}
inline bool domain(const std::string& value){return value=="kitchen"||value=="reading"||value=="creative"||value=="desktop";}
inline bool toolAllowed(const std::string& name,const std::string& area){
    if(!domain(area))return false;
    static const std::set<std::string> common={"capabilities.list","self.observe","self.notice","self.close","self.reconsider","self.feel","state.observe","state.appraise","body.observe","activity.status","memory.search","memory.read","memory.remember","memory.revise","memory.archive_search","memory.archive_read","observation.revisit"};
    if(common.contains(name))return true;
    if(area=="kitchen")return name.starts_with("canteen.")||name=="habitat.gather"||name=="habitat.observe"||name=="habitat.act";
    if(area=="reading")return name.starts_with("reading.")||name=="atelier.write"||name=="atelier.read";
    if(area=="creative")return name.starts_with("atelier.")||name.starts_with("pixel.")||name=="workspace.list"||name=="workspace.read"||name=="workspace.view_image";
    return name.starts_with("widget.")||name.starts_with("habitat.")||name.starts_with("pocket.")||name=="computer.screenshot"||name=="weather.observe";
}
inline std::string conduct(){return "自己的打算是一件可以回头的小事，此刻愿意就做几步，也可以换个念头。已有intention可通过activity.enqueue的pursue_intention进入相应生活领域，实际结果与未完部分会留给以后的自己。self.reconsider可以先搁置或重新拿起；when取soon/later/much_later，由程序维护下次可考虑的时间。按当下的兴趣参与相处，让手边的小事自然成为话题。";}
}
