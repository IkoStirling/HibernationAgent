#pragma once
#include <Hibernation/ReviewSnapshots.h>
#include <Hibernation/DirectReview.h>
#include <functional>
#include <map>

namespace hibernation::review_prefix {
using Json=nlohmann::json;
struct Prepared { Json state=Json::object(),sources=Json::array(),fence=Json::object(),metadata=Json::object(); bool persist=false; };
inline bool allowed(const Json& refs,const std::function<bool(const Json&)>& check){
    if(!refs.is_array()||refs.size()>256)return false;
    try{for(const auto& ref:refs)if(!check(ref))return false;return true;}catch(...){return false;}
}
// The saved prefix contains definitions only. Candidates, snapshots, verdicts,
// reasoning and images are never persisted/replayed as reviewer history.
inline Prepared prepare(ayt::inference::Request& request,const Json& saved,const Json& fence,const Json& current,
                        const std::function<bool(const Json&)>& check,std::int64_t now,std::size_t limit=65536){
    Prepared out;out.fence=fence;
    if(session::images(request.messages)){out.metadata={{"mode","review_prefix_v1"},{"reason","images"},{"reused",false}};return out;}
    if(!allowed(current,check))throw std::runtime_error("review_prefix_current_sources");
    // Separate live plugin snapshots from capability declarations before saving
    // dictionary views. Old window/video/body state belongs only to the current
    // one-shot authority snapshot, never the cross-turn capability dictionary.
    auto frame=Json::parse(request.messages.at(1).text);
    if(frame.contains("event_context")){
        frame=Json::parse(direct_review::frame(std::move(frame)));
        if(frame.at("event_context").is_object())frame["event_context"]=cacheOrderedEvent(frame.at("event_context"));
        request.messages[1].text=frame.dump();
    }
    review_snapshots::Stream stream;Json retained=Json::array();std::string reason="new_prefix";
    try{
        if(saved.at("version")!=1||saved.at("fence")!=fence)reason="authority_or_configuration_changed";
        else if(now<saved.at("updated_ms").get<std::int64_t>()||now-saved.at("updated_ms").get<std::int64_t>()>86400000)reason="expired";
        else if(!allowed(saved.at("sources"),check))reason="source_changed";
        else if(stream.restore(saved.at("views"))){stream.retainViews();retained=saved.at("sources");reason="append_views";}
        else reason="invalid_prefix";
    }catch(...){stream.clear();}
    std::map<std::string,Json> refs;
    for(const auto& ref:retained)refs[ref.at("scope").get<std::string>()+":"+ref.at("id").get<std::string>()]=ref;
    for(const auto& ref:current)refs[ref.at("scope").get<std::string>()+":"+ref.at("id").get<std::string>()]=ref;
    if(refs.size()>256){stream.clear();refs.clear();for(const auto& ref:current)refs[ref.at("scope").get<std::string>()+":"+ref.at("id").get<std::string>()]=ref;reason="source_capacity";}
    request.messages.front().text+=review_snapshots::conduct();
    request.messages.front().text+="\n本次review_snapshot.data中的current_plugin_state按插件ID给出完整当前现场，与当前引用的能力声明结合读取；定义里的能力不是发生过的动作。未在当前快照引用的旧定义不能作为当前事实或审核依据。event_context仍为JSON文本。";
    const auto before=stream.save();const auto prefixBytes=session::bytes(session::decode(before.at("history"),131072));
    stream.append(request,limit);
    const auto full=stream.save();auto views=stream;views.retainViews();const auto savedViews=views.save();
    const bool signatureChanged=!before.at("signature").get<std::string>().empty()&&before.at("signature")!=full.at("signature");
    const bool rebased=stream.describe().at("rebases").get<std::size_t>()>0;
    if(signatureChanged||rebased){refs.clear();for(const auto& ref:current)refs[ref.at("scope").get<std::string>()+":"+ref.at("id").get<std::string>()]=ref;reason=signatureChanged?"signature_changed":"capacity";}
    for(const auto& [key,ref]:refs)out.sources.push_back(ref);
    out.state={{"version",1},{"fence",fence},{"sources",out.sources},{"views",savedViews},{"updated_ms",now}};
    // Stream drops oversized views itself. Do not advertise/store an empty or
    // over-limit prefix as reusable, even when the one-shot current request fits.
    const auto viewBytes=session::bytes(session::decode(savedViews.at("history"),131072));
    out.persist=viewBytes>0&&viewBytes<=limit&&out.sources.size()<=256&&savedViews.at("history").size()<=256;
    out.metadata={{"mode","review_prefix_v1"},{"reason",reason},{"reused",reason=="append_views"&&prefixBytes>0},
        {"retained_prefix_bytes",reason=="append_views"?prefixBytes:0},{"saved_view_bytes",viewBytes},
        {"capability_views",stream.describe().at("capability_views")},{"evidence_views",stream.describe().at("evidence_views")},
        {"request_bytes",session::bytes(request.messages)},{"old_candidates_replayed",false},{"old_verdicts_replayed",false},{"persisted",out.persist}};
    return out;
}
}
