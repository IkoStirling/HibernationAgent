#include <Hibernation/ContinuityMind.h>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace hibernation::mind {
Settings Settings::read(const Json& value){
    if(!value.is_object())throw std::runtime_error("cognition_config");
    const std::set<std::string> keys={"enabled","attention","observations","dwellMs","resumeMinutes","observationCooldownMs"};
    for(auto it=value.begin();it!=value.end();++it)if(!keys.contains(it.key()))throw std::runtime_error("cognition_field");
    Settings s;
    auto flag=[&](const char* key,bool& target){if(value.contains(key)){if(!value.at(key).is_boolean())throw std::runtime_error("cognition_boolean");target=value.at(key);}};
    auto number=[&](const char* key,std::int64_t& target,std::int64_t low,std::int64_t high){if(!value.contains(key))return;const auto& n=value.at(key);if(!n.is_number_integer()||(n.is_number_unsigned()&&n.get<std::uint64_t>()>static_cast<std::uint64_t>(high)))throw std::runtime_error("cognition_range");target=n.get<std::int64_t>();if(target<low||target>high)throw std::runtime_error("cognition_range");};
    flag("enabled",s.enabled);flag("attention",s.attention);flag("observations",s.observations);
    number("dwellMs",s.dwellMs,1000,60000);number("resumeMinutes",s.resumeMinutes,1,60);number("observationCooldownMs",s.observationCooldownMs,30000,3600000);return s;
}
Json Settings::json()const{return {{"enabled",enabled},{"attention",attention},{"observations",observations},{"dwellMs",dwellMs},{"resumeMinutes",resumeMinutes},{"observationCooldownMs",observationCooldownMs}};}
State::State():data_({{"version",1},{"revision",1},{"clock_ms",0},{"focus",nullptr},{"focus_since_ms",0},{"candidates",Json::array()},{"suspended",Json::array()},{"predictions",Json::array()},{"observations",Json::array()},{"metrics",{{"switches",0},{"suspended",0},{"resumed",0},{"completed",0},{"unknown",0},{"late_effects",0}}}}){}
bool State::load(const Json& value){
    try{
        if(!value.is_object()||value.at("version")!=1||!value.at("revision").is_number_integer()||value.at("revision").get<std::int64_t>()<1||!value.at("clock_ms").is_number_integer()||value.at("clock_ms").get<std::int64_t>()<0)return false;
        for(const auto* key:{"candidates","suspended","predictions","observations"})if(!value.at(key).is_array())return false;
        if(value.at("candidates").size()>6||value.at("suspended").size()>3||value.at("predictions").size()>16||value.at("observations").size()>32||value.dump().size()>98304)return false;
        if(!value.at("focus").is_null()&&!value.at("focus").is_string())return false;
        if(!value.at("focus_since_ms").is_number_integer())return false;
        for(const auto& c:value.at("candidates"))if(!c.at("id").is_string()||!c.at("score").is_number()||!c.at("kind").is_string())return false;
        for(const auto& r:value.at("suspended"))if(!r.at("id").is_string()||!r.at("revision").is_number_integer()||!r.at("expires_clock_ms").is_number_integer()||!r.at("commands").is_array()||r.at("commands").size()>6||!r.at("status").is_string())return false;
        for(const auto& r:value.at("predictions"))if(!r.at("id").is_string()||!r.at("status").is_string())return false;
        for(const auto& r:value.at("observations"))if(!r.at("key").is_string()||!r.at("clock_ms").is_number_integer())return false;
        for(const auto* key:{"switches","suspended","resumed","completed","unknown","late_effects"})if(!value.at("metrics").at(key).is_number_integer())return false;
        data_=value;
        // A process restart is not a fresh receipt. Never restart an in-flight effect.
        for(auto& r:data_["predictions"])if(r.at("status")=="running"||r.at("status")=="queued")r["status"]="restart_unconfirmed";
        data_["focus"]=nullptr;data_["candidates"]=Json::array();changed();return true;
    }catch(const Json::exception&){return false;}
}
void State::changed(){data_["revision"]=data_.at("revision").get<std::int64_t>()+1;}
void State::advance(std::int64_t elapsed,bool paused){
    if(paused)return;data_["clock_ms"]=data_.at("clock_ms").get<std::int64_t>()+std::clamp<std::int64_t>(elapsed,0,1000);
    const auto now=data_.at("clock_ms").get<std::int64_t>();for(auto& r:data_["suspended"])if(r.at("status")=="pending"&&r.at("expires_clock_ms").get<std::int64_t>()<=now){r["status"]="expired";r["revision"]=r.at("revision").get<std::int64_t>()+1;changed();}
}
void State::select(const Json& input,const Settings& s){
    Json list=Json::array();std::set<std::string> ids;
    if(input.is_array())for(const auto& c:input){if(list.size()>=6)break;if(!c.is_object()||!c.contains("id")||!c.at("id").is_string()||!c.contains("kind")||!c.at("kind").is_string()||!c.contains("score")||!c.at("score").is_number())continue;
        if(!ids.insert(c.at("id").get<std::string>()).second)continue;auto clean=c;clean["score"]=std::clamp(c.at("score").get<double>(),0.,1.);list.push_back(std::move(clean));}
    if(list!=data_.at("candidates")){data_["candidates"]=list;changed();}
    Json next=nullptr;if(!list.empty()){
        auto best=std::max_element(list.begin(),list.end(),[](const Json& a,const Json& b){return a.at("score").get<double>()<b.at("score").get<double>();});
        auto current=std::find_if(list.begin(),list.end(),[&](const Json& c){return c.at("id")==data_.at("focus");});
        const auto elapsed=data_.at("clock_ms").get<std::int64_t>()-data_.at("focus_since_ms").get<std::int64_t>();
        const bool urgent=best->value("foreground",false);
        if(s.attention&&current!=list.end()&&!urgent&&(elapsed<s.dwellMs||best->at("score").get<double>()<current->at("score").get<double>()+.15))best=current;
        next=best->at("id");
    }
    if(next!=data_.at("focus")){data_["focus"]=next;data_["focus_since_ms"]=data_.at("clock_ms");data_["metrics"]["switches"]=data_["metrics"]["switches"].get<int>()+1;changed();}
}
bool State::attend(const std::string& id,std::int64_t revision,const Settings&){
    if(data_.at("revision")!=revision)return false;
    const auto& list=data_.at("candidates");auto selected=std::find_if(list.begin(),list.end(),[&](const Json& c){return c.at("id")==id;});if(selected==list.end())return false;
    if(std::any_of(list.begin(),list.end(),[&](const Json& c){return c.value("foreground",false)&&c.at("id")!=id;}))return false;
    if(data_.at("focus")!=id){data_["focus"]=id;data_["focus_since_ms"]=data_.at("clock_ms");data_["metrics"]["switches"]=data_["metrics"]["switches"].get<int>()+1;changed();}return true;
}
void State::suspend(const Json& queue,const std::string& reason,const Settings& s){
    if(reason!="input_active"&&reason!="new_input"&&reason!="qq_input"&&reason!="application_restarted"&&reason!="shutdown")return;
    const auto commands=queue.value("entries",Json::array());const auto active=queue.value("active",Json(nullptr));if(commands.empty()&&active.is_null())return;
    const auto signature=[&](const Json& entries,const Json& running){Json ids=Json::array();if(running.is_object())ids.push_back(running.at("id"));for(const auto& c:entries)ids.push_back(c.at("id"));return ids;};
    const auto ids=signature(commands,active);for(const auto& old:data_.at("suspended"))if(old.at("status")=="pending"&&signature(old.at("commands"),old.at("interrupted"))==ids)return;
    Json item={{"id","resume-"+std::to_string(data_.at("revision").get<std::int64_t>())},{"revision",1},{"status","pending"},{"reason",reason},{"created_clock_ms",data_.at("clock_ms")},{"expires_clock_ms",data_.at("clock_ms").get<std::int64_t>()+s.resumeMinutes*60000},{"commands",commands},{"interrupted",active},{"requires_new_plan",active.is_object()}};
    // Active effects are described for reconsideration, never copied to replay commands.
    data_["suspended"].push_back(std::move(item));while(data_["suspended"].size()>3||(!data_["suspended"].empty()&&data_.dump().size()>80000))data_["suspended"].erase(0);
    data_["metrics"]["suspended"]=data_["metrics"]["suspended"].get<int>()+1;changed();
}
Json State::resumable(const std::string& id,std::int64_t revision)const{
    for(const auto& r:data_.at("suspended"))if(r.at("id")==id&&r.at("revision")==revision&&r.at("status")=="pending"&&r.at("expires_clock_ms").get<std::int64_t>()>data_.at("clock_ms").get<std::int64_t>())return r;return nullptr;
}
bool State::settle(const std::string& id,std::int64_t revision,const std::string& status){
    if(status!="resumed"&&status!="abandoned"&&status!="invalidated")return false;
    if(resumable(id,revision).is_null())return false;
    for(auto& r:data_["suspended"])if(r.at("id")==id){r["status"]=status;r["revision"]=revision+1;r["commands"]=Json::array();if(status=="resumed")data_["metrics"]["resumed"]=data_["metrics"]["resumed"].get<int>()+1;changed();return true;}return false;
}
void State::queued(const Json& c){
    const auto id=c.at("id");for(const auto& r:data_.at("predictions"))if(r.at("id")==id)return;
    data_["predictions"].push_back({{"id",id},{"kind",c.at("kind")},{"expected","matching_actual_completion_receipt"},{"status","queued"},{"clock_ms",data_.at("clock_ms")}});
    while(data_["predictions"].size()>16)data_["predictions"].erase(0);changed();
}
void State::outcome(const Json& c,const std::string& status,bool late){
    for(auto& r:data_["predictions"])if(r.at("id")==c.at("id")){
        if(late){if(r.value("effect_outcome",std::string{})==status)return;r["effect_outcome"]=status;data_["metrics"]["late_effects"]=data_["metrics"]["late_effects"].get<int>()+1;}
        else {if(r.at("status")==status)return;r["status"]=status;if(status=="completed")data_["metrics"]["completed"]=data_["metrics"]["completed"].get<int>()+1;else if(status.find("timeout")!=std::string::npos||status=="timed_out"||status=="unconfirmed"||status=="restart_unconfirmed")data_["metrics"]["unknown"]=data_["metrics"]["unknown"].get<int>()+1;}
        r["updated_clock_ms"]=data_.at("clock_ms");changed();return;
    }
}
void State::observed(const std::string& key){
    if(key.empty()||key.size()>256)return;for(auto it=data_["observations"].begin();it!=data_["observations"].end();)if(it->at("key")==key)it=data_["observations"].erase(it);else ++it;
    data_["observations"].push_back({{"key",key},{"clock_ms",data_.at("clock_ms")}});while(data_["observations"].size()>32)data_["observations"].erase(0);changed();
}
bool State::observationDue(const std::string& key,const Settings& s)const{
    if(!s.observations)return false;for(const auto& r:data_.at("observations"))if(r.at("key")==key)return data_.at("clock_ms").get<std::int64_t>()-r.at("clock_ms").get<std::int64_t>()>=s.observationCooldownMs;return true;
}
Json State::view()const{return {{"version",1},{"revision",data_.at("revision")},{"focus",data_.at("focus")},{"candidates",data_.at("candidates")},{"suspended",data_.at("suspended")},{"predictions",data_.at("predictions")},{"scope","private_working_state_not_original_evidence"}};}
Json State::status()const{return {{"version",1},{"revision",data_.at("revision")},{"candidate_count",data_.at("candidates").size()},{"pending_count",std::count_if(data_.at("suspended").begin(),data_.at("suspended").end(),[](const Json& r){return r.at("status")=="pending";})},{"prediction_count",data_.at("predictions").size()},{"metrics",data_.at("metrics")}};}
bool privateTool(const std::string& name){return name.starts_with("mind.")||name=="activity.resume";}
Json tools(){
    auto object=[](Json p,Json required){return Json{{"type","object"},{"additionalProperties",false},{"properties",p},{"required",required}};};
    const Json id={{"type","string"},{"minLength",1},{"maxLength",64}},revision={{"type","integer"},{"minimum",0}};
    Json list=Json::array();auto add=[&](const char* n,const char* d,Json p){list.push_back({{"name",n},{"description",d},{"parameters",p}});};
    add("mind.observe","查看当前注意、可恢复打算、预期与真实结果，以及可修正的判断；这是私有工作状态，不是新的原始经历。",object(Json::object(),Json::array()));
    add("mind.attend","按mind.observe版本选择一个当前存在的注意候选。不能抢占正在输入的用户，也不直接执行动作。",object({{"id",id},{"expected_revision",revision}},Json::array({"id","expected_revision"})));
    add("mind.interpret","保存或撤回一条有来源的暂定判断，不能将推测当作事实、改变人设或直接写情绪数值。新id版本0；撤回state=withdrawn仍保留历史。",object({{"id",id},{"expected_revision",revision},{"text",{{"type","string"},{"minLength",1},{"maxLength",400}}},{"confidence",{{"enum",Json::array({"uncertain","tentative"})}}},{"state",{{"enum",Json::array({"active","withdrawn"})}}},{"source_ids",{{"type","array"},{"minItems",1},{"maxItems",4},{"items",{{"type","string"},{"minLength",1},{"maxLength",256}}}}}},Json::array({"id","expected_revision","text","confidence","state","source_ids"})));
    add("activity.resume","自行决定继续或放下被交谈打断的打算。continue只重新校验并排入尚未开始的命令，已执行/结果未知部分必须另作新计划。accepted不表示完成。",object({{"id",id},{"expected_revision",revision},{"decision",{{"enum",Json::array({"continue","abandon"})}}}},Json::array({"id","expected_revision","decision"})));return list;
}
std::string conduct(){return "mind是私有的当前注意、暂定判断、打算与实际结果摘要，不是人格或新的亲历来源。可忽略无关候选，不必在对白提及或复述水果、身体数值和旧事。activity.resume供你选择续接尚未执行的部分或放下；中断中的操作结果未知时先查真实回执，不重放它。观察建议是可选的问题入口，不代表已看过，不要求持续截图、每轮调用或生成心得。要观察时使用现有工具并依据实际资料。暂定解释可用mind.interpret修正/撤回；体验性的喜好和长期打算继续用self.notice及其真实来源。";}
}
