#include <Hibernation/CognitiveLoop.h>
#include <AYCrypto.h>
#include <AYSerializer/JsonSchema.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hibernation::cognitive {
namespace {
void require(bool value, const char* issue) { if (!value) throw std::runtime_error(issue); }
bool id(const Json& v) {
    if (!v.is_string()) return false;
    const auto s = v.get<std::string>();
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
bool number(const Json& v, double low, double high) {
    return v.is_number() && std::isfinite(v.get<double>()) && v.get<double>() >= low && v.get<double>() <= high;
}
bool integer(const Json& v, std::int64_t low, std::int64_t high) {
    return v.is_number_integer() && number(v, static_cast<double>(low), static_cast<double>(high));
}
std::string fingerprint(const Json& value) {
    const auto bytes=value.dump();auto hash=ayt::crypto::createHash(ayt::crypto::HashAlgo::SHA256);
    require(static_cast<bool>(hash),"hash_unavailable");
    return hash->hashString(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size());
}
void exact(const Json& v, std::initializer_list<const char*> fields) {
    require(v.is_object() && v.size() == fields.size(), "unexpected_fields");
    for (const auto* field : fields) require(v.contains(field), "missing_field");
}
}

Loop::Loop(Settings settings) : settings_(settings) {
    require(settings.dwellMs >= 0 && settings.dwellMs <= 10000 && std::isfinite(settings.switchMargin)
            && settings.switchMargin >= 0 && settings.switchMargin <= 1, "invalid_settings");
    body_.restore(Json::object(), now_);
    metrics_ = {{"model_calls",0},{"focus_switches",0},{"actions_started",0},{"actions_completed",0},
                {"goals_completed",0},{"goals_failed",0},{"goals_interrupted",0},{"goals_unknown",0},
                {"goals_abandoned",0},{"rejected_receipts",0},{"invalid_proposals",0},{"consumed",0}};
}

void Loop::count(const std::string& name) { metrics_[name] = metrics_.at(name).get<std::int64_t>() + 1; }
std::string Loop::key(const std::string& target, std::int64_t rev) const { return target + ":" + std::to_string(rev); }
Json Loop::goalJson(const Goal& goal) const {
    return {{"id",goal.id},{"kind",goal.kind},{"target",goal.target},
            {"target_revision",goal.targetRevision},{"started_at",goal.startedAt},{"duration_ms",goal.durationMs}};
}
void Loop::note(std::string kind, Json data) {
    Json item = {{"kind",std::move(kind)},{"at_ms",now_},{"source_event",sourceEvent_},{"data",std::move(data)}};
    emitted_.push_back(item); trace_.push_back(std::move(item));
    if (trace_.size() > 64) trace_.erase(trace_.begin());
}

std::vector<Loop::Candidate> Loop::candidates() const {
    std::vector<Candidate> result;
    if (hunger_ >= .3) for (const auto& [name, object] : objects_) {
        if (!blocked_.contains(key(name, object.revision))) {
            const double continuity=suspended_&&suspended_->target==name&&suspended_->targetRevision==object.revision
                &&now_-suspended_->startedAt<=30000?.1:0.;
            result.push_back({"food-"+name,"food",name,object.revision,.25+.4*hunger_+.3*object.salience+continuity});
        }
    }
    const double energy = body_.snapshot().at("energy").get<double>();
    if (energy < .35 && now_ >= restAfter_) result.push_back({"rest","rest",{},0,.95-energy});
    std::sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) {
        return a.priority == b.priority ? a.id < b.id : a.priority > b.priority;
    });
    // Keep the selected item available inside the bounded workspace.
    if (result.size() > 4) {
        const auto found = std::find_if(result.begin()+4, result.end(), [&](const Candidate& c){return c.id==focus_;});
        if (found != result.end()) result[3] = *found;
        result.resize(4);
    }
    return result;
}

Json Loop::request() const {
    Json options = Json::array();
    for (const auto& candidate : candidates()) options.push_back({{"id",candidate.id},{"kind",candidate.kind},
        {"target",candidate.target},{"target_revision",candidate.revision},{"priority",candidate.priority}});
    return {{"version",1},{"context_revision",revision_},{"focus_id",focus_},{"candidates",options},
            {"body",{{"energy",body_.snapshot().at("energy")},{"hunger",hunger_},{"position_x",x_}}}};
}

Json Loop::mock(const Json& input) const {
    if (mode_ == "malformed") return {{"action","set_energy"},{"energy",1}};
    for (const auto& candidate : input.at("candidates")) if (candidate.at("id") == focus_) {
        return {{"version",1},{"context_revision",mode_=="stale"?revision_-1:revision_},{"focus_id",focus_},
                {"action",mode_=="silent"?"silence":candidate.at("kind")=="rest"?"rest":"approach_and_eat"},
                {"target",candidate.at("target")},{"target_revision",candidate.at("target_revision")}};
    }
    return nullptr;
}

bool Loop::validateProposal(const Json& proposal, std::string& issue) const {
    try {
        exact(proposal,{"version","context_revision","focus_id","action","target","target_revision"});
        require(proposal.at("version")==1 && integer(proposal.at("context_revision"),0,1000000)
                && proposal.at("context_revision")==revision_, "stale_context");
        require(proposal.at("focus_id").is_string() && proposal.at("focus_id")==focus_, "wrong_focus");
        require(proposal.at("action").is_string() && proposal.at("target").is_string()
                && integer(proposal.at("target_revision"),0,1000000), "invalid_proposal");
        const auto action = proposal.at("action").get<std::string>();
        const auto options = candidates();
        const auto selected = std::find_if(options.begin(), options.end(), [&](const Candidate& c){return c.id==focus_;});
        require(!paused_ && dialogue_.is_null() && selected!=options.end(), "focus_not_available");
        require(proposal.at("target")==selected->target && proposal.at("target_revision")==selected->revision,
                "wrong_target");
        require(action=="silence" || (selected->kind=="rest" && action=="rest")
                || (selected->kind=="food" && action=="approach_and_eat"), "action_not_available");
        issue.clear(); return true;
    } catch (const std::exception& e) { issue=e.what(); return false; }
}

void Loop::settle() {
    if(!settings_.automaticPlanning)return;
    if (paused_ || !dialogue_.is_null()) return;
    auto options = candidates();
    if (options.empty()) {
        if (goal_) stopGoal("abandoned","need_or_target_no_longer_available",false);
        if (!focus_.empty()) { focus_.clear(); note("focus_cleared"); }
        return;
    }
    auto selected = options.front();
    const auto current = std::find_if(options.begin(), options.end(), [&](const Candidate& c){return c.id==focus_;});
    if (current != options.end() && selected.id != focus_ && settings_.attention
        && (now_-focusAt_ < settings_.dwellMs || selected.priority < current->priority+settings_.switchMargin))
        selected = *current;
    if (selected.id != focus_) {
        if (goal_) stopGoal("interrupted","attention_changed",false);
        focus_=selected.id; focusAt_=now_; decisionKey_.clear(); count("focus_switches");
        note("focus_selected",{{"id",focus_},{"priority",selected.priority}});
    }
    if (goal_) return;
    const auto decision = focus_+":"+std::to_string(selected.revision)+":"+mode_;
    if (decision == decisionKey_) return;
    decisionKey_=decision;
    const auto input = request(); count("model_calls"); const auto proposal = mock(input);
    std::string issue;
    if (!validateProposal(proposal,issue)) {
        count("invalid_proposals"); note("proposal_rejected",{{"issue",issue}}); return;
    }
    if (proposal.at("action")=="silence") { note("silence_selected"); return; }
    Goal next{"goal-"+std::to_string(++serial_), selected.kind=="rest"?"rest":"eat",selected.target,selected.revision,now_};
    if (suspended_ && suspended_->kind==next.kind && suspended_->target==next.target
        && suspended_->targetRevision==next.targetRevision && now_-suspended_->startedAt<=30000) {
        next.id=suspended_->id; note("goal_resumed",goalJson(next)); suspended_.reset();
    }
    if(suspended_){note("suspended_goal_abandoned",goalJson(*suspended_));count("goals_abandoned");suspended_.reset();}
    goal_=next; note("goal_started",goalJson(next));
    startOperation(next.kind=="rest"?"rest":std::abs(objects_.at(next.target).x-x_)<=.01?"eat":"move");
}

void Loop::startOperation(std::string kind) {
    const auto expected = kind=="move"?"arrived":kind=="eat"?"consumed":kind=="observe"?"inspected":kind=="wait"?"wait_elapsed":"rest_elapsed";
    const auto duration = kind=="move"?10000:kind=="eat"?2000:kind=="observe"?1500:(goal_->durationMs?goal_->durationMs:15000)+1000;
    operation_=Operation{"op-"+std::to_string(++serial_),std::move(kind),expected,now_,now_+duration,false};
    count("actions_started");
    note("operation_started",{{"token",operation_->token},{"kind",operation_->kind},{"expected",expected},
                             {"deadline_ms",operation_->deadline},{"goal",goalJson(*goal_)}});
    std::string issue;
    body_.propose(operation_->kind=="rest"?"resting":operation_->kind=="wait"?"idle":"engaging",{},"synthetic-operation",now_,issue);
}

void Loop::stopGoal(std::string outcome, std::string reason, bool block) {
    if (!goal_) return;
    count("goals_"+outcome);
    if(!settings_.automaticPlanning)decisionNeeded_=goal_->kind!="observe"||outcome!="completed";
    if (block && goal_->kind=="eat") blocked_.insert(key(goal_->target,goal_->targetRevision));
    if (goal_->kind=="rest") restAfter_=now_+30000;
    Json result={{"goal",goalJson(*goal_)},{"outcome",outcome},{"reason",reason}};
    if (operation_) result["operation_token"]=operation_->token;
    note("goal_result",std::move(result));
    goal_.reset(); operation_.reset();
    std::string issue; body_.propose("idle",{},"synthetic-operation-ended",now_,issue);
}

bool Loop::receipt(const Json& event) {
    auto reject = [&](const char* issue) {count("rejected_receipts"); note("receipt_rejected",{{"issue",issue}}); return false;};
    if (!operation_ || !goal_ || event.at("token")!=operation_->token) return reject("stale_operation");
    if (now_ >= operation_->deadline) {stopGoal("unknown","receipt_after_deadline",true);return reject("expired_operation");}
    const auto status=event.at("status").get<std::string>();
    if (status=="accepted") { operation_->accepted=true; note("operation_accepted",{{"token",operation_->token}}); return true; }
    if (status=="failed") {
        const bool certain=event.at("proof").value("effect_applied",true)==false;
        stopGoal(certain?"failed":"unknown",certain?"executor_failed_without_effect":"effect_uncertain",true); return true;
    }
    if (status!="completed" || event.at("proof").value("result",std::string{})!=operation_->expected)
        return reject("wrong_result");
    if (goal_->kind=="eat"||goal_->kind=="observe") {
        const auto target=objects_.find(goal_->target);
        if (target==objects_.end() || target->second.revision!=goal_->targetRevision) return reject("stale_target");
        if (event.at("proof").value("target",std::string{})!=goal_->target
            || event.at("proof").value("target_revision",std::int64_t{})!=goal_->targetRevision) return reject("wrong_target");
        if (operation_->kind=="move") {
            if (!event.at("proof").contains("position_x") || !number(event.at("proof").at("position_x"),0,1)
                || std::abs(event.at("proof").at("position_x").get<double>()-target->second.x)>.01)
                return reject("arrival_not_confirmed");
            x_=event.at("proof").at("position_x").get<double>();
        } else if(operation_->kind=="eat") {
            if (std::abs(x_-target->second.x)>.01 || !event.at("proof").value("consumed",false))
                return reject("consumption_not_confirmed");
            objects_.erase(target); hunger_=std::max(0.,hunger_-.55); count("consumed");
        } else if(!event.at("proof").value("inspected",false))return reject("inspection_not_confirmed");
    } else if(operation_->kind=="wait") {
        if(now_-operation_->startedAt<goal_->durationMs)return reject("wait_not_elapsed");
    } else if (operation_->kind!="rest" || now_-operation_->startedAt<(goal_->durationMs?goal_->durationMs:15000)
               || body_.snapshot().at("activity")!="resting") return reject("rest_not_elapsed");
    const auto kind=operation_->kind; count("actions_completed");
    note("operation_completed",{{"token",operation_->token},{"kind",kind},{"proof",event.at("proof")}});
    if (kind=="move") startOperation("eat"); else stopGoal("completed","expected_result_observed",false);
    return true;
}

void Loop::advance(std::int64_t ms, bool automatic) {
    if (paused_) {note("paused_time_ignored",{{"ms",ms}});return;}
    for (std::int64_t remaining=ms;remaining>0;) {
        const auto step=std::min<std::int64_t>(remaining,250); remaining-=step; now_+=step;
        body_.tick(now_); hunger_=std::min(1.,hunger_+static_cast<double>(step)*.000001);
        if(suspended_&&now_-suspended_->startedAt>30000){note("suspended_goal_expired",goalJson(*suspended_));count("goals_abandoned");suspended_.reset();if(!settings_.automaticPlanning)decisionNeeded_=true;}
        for (auto it=objects_.begin();it!=objects_.end();) {
            if (it->second.expiresAt<=now_) {
                if (goal_ && goal_->target==it->first) stopGoal("failed","target_expired",true);
                note("object_expired",{{"id",it->first}}); it=objects_.erase(it);
            } else ++it;
        }
        if (operation_ && automatic) {
            if (!operation_->accepted) receipt({{"token",operation_->token},{"status","accepted"},{"proof",Json::object()}});
            if (operation_) {
                Json proof={{"result",operation_->expected}};
                bool done=false;
                if ((goal_->kind=="eat"||goal_->kind=="observe") && objects_.contains(goal_->target)) {
                    const auto& object=objects_.at(goal_->target);
                    proof.update({{"target",object.id},{"target_revision",object.revision}});
                    if (operation_->kind=="move") {
                        const auto delta=object.x-x_, distance=static_cast<double>(step)*.00015;
                        x_+=std::clamp(delta,-distance,distance); done=std::abs(x_-object.x)<=.001; proof["position_x"]=x_;
                    } else if(operation_->kind=="eat"){done=now_-operation_->startedAt>=1000;proof["consumed"]=true;}
                    else {done=now_-operation_->startedAt>=500;proof["inspected"]=true;}
                } else if (operation_->kind=="rest"||operation_->kind=="wait")
                    done=now_-operation_->startedAt>=(goal_->durationMs?goal_->durationMs:15000);
                if (done) receipt({{"token",operation_->token},{"status","completed"},{"proof",proof}});
            }
        }
        if (operation_ && now_>=operation_->deadline) stopGoal("unknown","completion_not_observed",true);
        settle();
    }
}

void Loop::checkEvent(const Json& event) {
    require(event.is_object() && event.contains("event_id") && id(event.at("event_id"))
            && event.contains("type") && event.at("type").is_string(), "invalid_event");
    require(!event.at("event_id").get<std::string>().starts_with("system-"),"reserved_event_id");
    const auto type=event.at("type").get<std::string>();
    if (type=="seed") {
        exact(event,{"event_id","type","energy","hunger","x"});
        require(number(event.at("energy"),0,1)&&number(event.at("hunger"),0,1)&&number(event.at("x"),0,1),"invalid_seed");
    } else if (type=="observe") {
        exact(event,{"event_id","type","object"});const auto& o=event.at("object");
        exact(o,{"id","revision","x","salience","ttl_ms"});
        require(id(o.at("id"))&&integer(o.at("revision"),1,1000000)&&number(o.at("x"),0,1)
                &&number(o.at("salience"),0,1)&&integer(o.at("ttl_ms"),1000,3600000),"invalid_object");
    } else if (type=="advance") {
        exact(event,{"event_id","type","ms","executor"});
        require(integer(event.at("ms"),0,60000)&&(event.at("executor")=="automatic"||event.at("executor")=="silent"),"invalid_advance");
    } else if (type=="message") {
        exact(event,{"event_id","type","text"});
        require(event.at("text").is_string()&&!event.at("text").get_ref<const std::string&>().empty()
                &&event.at("text").get_ref<const std::string&>().size()<=1024,"invalid_message");
    } else if (type=="conversation_done") exact(event,{"event_id","type"});
    else if (type=="pause") {exact(event,{"event_id","type","value"});require(event.at("value").is_boolean(),"invalid_pause");}
    else if (type=="remove") {
        exact(event,{"event_id","type","id","revision"});
        require(id(event.at("id"))&&integer(event.at("revision"),1,1000000),"invalid_remove");
    } else if (type=="model_mode") {
        exact(event,{"event_id","type","mode"});require(event.at("mode")=="normal"||event.at("mode")=="stale"
                ||event.at("mode")=="malformed"||event.at("mode")=="silent","invalid_model_mode");
    } else if (type=="receipt") {
        exact(event,{"event_id","type","token","status","proof"});
        require(id(event.at("token"))&&(event.at("status")=="accepted"||event.at("status")=="failed"||event.at("status")=="completed")
                &&event.at("proof").is_object()&&event.at("proof").dump().size()<=1024,"invalid_receipt");
        const auto& proof=event.at("proof");
        for (const auto& [name,value]:proof.items()) {
            if (name=="effect_applied"||name=="consumed"||name=="inspected") require(value.is_boolean(),"invalid_proof");
            else if (name=="result") require(value.is_string()&&value.get_ref<const std::string&>().size()<=32,"invalid_proof");
            else if (name=="target") require(id(value),"invalid_proof");
            else if (name=="target_revision") require(integer(value,1,1000000),"invalid_proof");
            else if (name=="position_x") require(number(value,0,1),"invalid_proof");
            else throw std::runtime_error("invalid_proof");
        }
    } else if(type=="decision") {exact(event,{"event_id","type","proposal"});require(event.at("proposal").is_object(),"invalid_choice");}
    else if(type=="decision_deferred") {exact(event,{"event_id","type","reason"});require(event.at("reason")=="validation_failed"||event.at("reason")=="review_rejected"||event.at("reason")=="provider_failed", "invalid_deferral");}
    else throw std::runtime_error("unknown_event");
}

void Loop::apply(const Json& event) {
    const auto type=event.at("type").get<std::string>();
    if (type=="seed") {
        require(seen_.empty(),"seed_only_at_start");
        auto state=body_.snapshot();state["energy"]=event.at("energy");body_.restore(state,now_);
        hunger_=event.at("hunger");x_=event.at("x");
        if(!settings_.automaticPlanning)decisionNeeded_=event.at("energy").get<double>()<.35;
    } else if (type=="observe") {
        const auto& value=event.at("object");const auto name=value.at("id").get<std::string>();const auto rev=value.at("revision").get<std::int64_t>();
        const auto existing=objects_.find(name);
        if (lastRevision_.contains(name) && rev<=lastRevision_.at(name)) {
            if (existing!=objects_.end() && rev==existing->second.revision && value.at("x")==existing->second.x
                &&value.at("salience")==existing->second.salience) {note("unchanged_observation",{{"id",name}});return;}
            throw std::runtime_error("stale_object_revision");
        }
        require(objects_.contains(name)||objects_.size()<16,"world_capacity");
        require(lastRevision_.contains(name)||lastRevision_.size()<256,"identity_capacity");
        if (goal_ && goal_->target==name) stopGoal("interrupted","target_revision_changed",false);
        objects_[name]=Object{name,rev,now_+value.at("ttl_ms").get<std::int64_t>(),value.at("x"),value.at("salience")};
        lastRevision_[name]=rev;decisionKey_.clear();note("object_observed",value);decisionNeeded_=true;
    } else if (type=="remove") {
        const auto name=event.at("id").get<std::string>();const auto it=objects_.find(name);
        require(it!=objects_.end()&&it->second.revision==event.at("revision"),"stale_object_revision");
        if (goal_ && goal_->target==name) stopGoal("failed","target_removed",true);
        objects_.erase(it);note("object_removed",{{"id",name}});decisionNeeded_=true;
    } else if (type=="advance") advance(event.at("ms"),event.at("executor")=="automatic");
    else if (type=="pause") {paused_=event.at("value");note(paused_?"paused":"resumed");}
    else if (type=="message") {
        if (goal_) {if(goal_->kind!="wait"||!suspended_)suspended_=goal_;stopGoal("interrupted","user_message",false);}
        dialogue_={{"id",event.at("event_id")},{"text",event.at("text")}};focus_="conversation";focusAt_=now_;
        count("focus_switches");
        dialogueHistory_.push_back({{"speaker","user"},{"text",event.at("text")}});
        if(dialogueHistory_.size()>6)dialogueHistory_.erase(dialogueHistory_.begin());
        if(settings_.automaticPlanning){count("model_calls");note("mock_reply",{{"text","收到，先停一下。"},{"synthetic",true}});}
        else{decisionNeeded_=true;note("message_received");}
    } else if (type=="conversation_done") {
        dialogue_=nullptr;focus_.clear();decisionKey_.clear();
        if(!settings_.automaticPlanning)decisionNeeded_=!objects_.empty()||suspended_.has_value()||body_.snapshot().at("energy").get<double>()<.35;
        if (suspended_) {
            const bool valid=now_-suspended_->startedAt<=30000&&(suspended_->kind=="rest"
                ||(objects_.contains(suspended_->target)&&objects_.at(suspended_->target).revision==suspended_->targetRevision));
            if (!valid) {note("suspended_goal_abandoned",goalJson(*suspended_));count("goals_abandoned");suspended_.reset();}
        }
    } else if (type=="model_mode") {mode_=event.at("mode");decisionKey_.clear();}
    else if (type=="receipt") receipt(event);
    else if(type=="decision")applyChoice(event.at("proposal"));
    else if(type=="decision_deferred"){require(!settings_.automaticPlanning,"manual_decisions_required");decisionNeeded_=false;count("model_calls");note("decision_deferred",{{"reason",event.at("reason")}});}
    settle();
}

Json Loop::dispatch(const Json& event) {
    try {
        checkEvent(event);const auto eventId=event.at("event_id").get<std::string>();
        if (seen_.contains(eventId)) {
            require(seen_.at(eventId)==fingerprint(event),"event_id_conflict");
            return {{"ok",true},{"duplicate",true},{"events",Json::array()},{"view",view()}};
        }
        require(seen_.size()<4096,"experiment_event_limit");
        // Stage through the same validated snapshot path; no partial event effects.
        Loop next(settings_);next.load(snapshot());next.emitted_=Json::array();next.sourceEvent_=eventId;
        const bool choice=event.at("type")=="decision";if(!choice)++next.revision_;
        next.apply(event);if(choice)++next.revision_;next.seen_[eventId]=fingerprint(event);
        auto saved=next.snapshot();auto emitted=next.emitted_;load(saved);emitted_=emitted;
        return {{"ok",true},{"duplicate",false},{"events",emitted},{"view",view()}};
    } catch (const std::exception& e) {return {{"ok",false},{"issue",e.what()},{"events",Json::array()},{"view",view()}};}
}

Json Loop::view() const {
    Json options=Json::array();for(const auto& candidate:candidates()) options.push_back({{"id",candidate.id},{"priority",candidate.priority}});
    Json operation=nullptr;if(operation_)operation={{"token",operation_->token},{"kind",operation_->kind},
        {"accepted",operation_->accepted},{"expected",operation_->expected},{"deadline_ms",operation_->deadline}};
    return {{"synthetic",true},{"now_ms",now_},{"paused",paused_},{"focus",focus_},{"workspace",options},
        {"body",body_.snapshot()},{"hunger",hunger_},{"position_x",x_},{"goal",goal_?goalJson(*goal_):Json(nullptr)},
        {"operation",operation},{"suspended",suspended_?goalJson(*suspended_):Json(nullptr)},
        {"object_count",objects_.size()},{"needs_decision",needsDecision()},{"automatic_planning",settings_.automaticPlanning},{"metrics",metrics_}};
}

Json Loop::snapshot() const {
    Json objects=Json::array();for(const auto& [name,o]:objects_)objects.push_back({{"id",name},{"revision",o.revision},
        {"expires_at",o.expiresAt},{"x",o.x},{"salience",o.salience}});
    Json op=nullptr;if(operation_)op={{"token",operation_->token},{"kind",operation_->kind},{"expected",operation_->expected},
        {"started_at",operation_->startedAt},{"deadline",operation_->deadline},{"accepted",operation_->accepted}};
    return {{"format","ha.cognitive-prototype.v1"},{"settings",{{"attention",settings_.attention},{"dwell_ms",settings_.dwellMs},{"switch_margin",settings_.switchMargin},{"automatic_planning",settings_.automaticPlanning}}},
        {"now",now_},{"revision",revision_},{"serial",serial_},{"focus_at",focusAt_},{"rest_after",restAfter_},
        {"hunger",hunger_},{"x",x_},{"paused",paused_},{"focus",focus_},{"mode",mode_},{"decision_key",decisionKey_},
        {"body",body_.snapshot()},{"objects",objects},{"last_revisions",lastRevision_},{"blocked",blocked_},{"seen",seen_},
        {"goal",goal_?goalJson(*goal_):Json(nullptr)},{"suspended",suspended_?goalJson(*suspended_):Json(nullptr)},
        {"operation",op},{"dialogue",dialogue_},{"dialogue_history",dialogueHistory_},{"decision_needed",decisionNeeded_},{"metrics",metrics_},{"trace",trace_}};
}

std::string Loop::choiceSchema(){
    return Json{{"type","object"},{"additionalProperties",false},
        {"required",Json::array({"version","context_revision","choice","target","target_revision","seconds","text"})},
        {"properties",{{"version",{{"type","integer"},{"enum",Json::array({2})}}},
            {"context_revision",{{"type","integer"},{"minimum",0},{"maximum",1000000000}}},
            {"choice",{{"type","string"},{"enum",Json::array({"eat","rest","observe","wait","continue","abandon","reply","silence"})}}},
            {"target",{{"type","string"},{"maxLength",64}}},{"target_revision",{{"type","integer"},{"minimum",0},{"maximum",1000000}}},
            {"seconds",{{"type","integer"},{"minimum",0},{"maximum",30}}},{"text",{{"type","string"},{"maxLength",160}}}}}}.dump();
}

bool Loop::needsDecision()const{return !settings_.automaticPlanning&&!paused_&&decisionNeeded_;}

Json Loop::decisionContext()const{
    Json observations=Json::array(),allowed=Json::array();
    const bool replace=!goal_||!settings_.attention||now_-focusAt_>=settings_.dwellMs;
    auto add=[&](const std::string& action,const std::string& target="",std::int64_t rev=0){allowed.push_back({{"choice",action},{"target",target},{"target_revision",rev}});};
    add("silence");
    if(!dialogue_.is_null())add("reply");
    else{
        if(replace){add("wait");if(now_>=restAfter_)add("rest");}
        for(const auto& [name,object]:objects_){
            if(observations.size()>=4)break;
            observations.push_back({{"id",name},{"revision",object.revision},{"x",object.x},{"salience",object.salience}});
            if(replace){
                if(!goal_||goal_->kind!="observe"||goal_->target!=name)add("observe",name,object.revision);
                if(hunger_>=.3&&!blocked_.contains(key(name,object.revision))&&(!goal_||goal_->kind!="eat"||goal_->target!=name))add("eat",name,object.revision);
            }
        }
    }
    const bool resumable=suspended_&&now_-suspended_->startedAt<=30000&&(suspended_->kind=="rest"
        ||(objects_.contains(suspended_->target)&&objects_.at(suspended_->target).revision==suspended_->targetRevision));
    if((goal_||resumable)&&dialogue_.is_null())add("continue");
    if(goal_||suspended_)add("abandon");
    Json facts=Json::array();for(auto it=trace_.rbegin();it!=trace_.rend()&&facts.size()<5;++it)
        if(it->at("kind")=="goal_result"||it->at("kind")=="operation_completed"||it->at("kind")=="receipt_rejected")facts.push_back(*it);
    std::reverse(facts.begin(),facts.end());
    return {{"version",2},{"context_revision",revision_},{"synthetic_world",true},{"now_ms",now_},
        {"body",{{"energy",body_.snapshot().at("energy")},{"hunger",hunger_},{"position_x",x_},{"activity",body_.snapshot().at("activity")}}},
        {"focus",focus_},{"focus_replace_allowed",replace},{"objects",observations},{"allowed_choices",allowed},
        {"goal",goal_?goalJson(*goal_):Json(nullptr)},{"suspended",suspended_?goalJson(*suspended_):Json(nullptr)},
        {"operation",view().at("operation")},{"dialogue",dialogue_},{"recent_dialogue",dialogueHistory_},{"observed_results",facts}};
}

bool Loop::validateChoice(const Json& proposal,std::string& issue)const{
    try{
        require(!settings_.automaticPlanning&&needsDecision(),"no_decision_requested");
        require(static_cast<bool>(ayt::serializer::validateJsonDocument(proposal.dump(),choiceSchema())),"choice_format");
        require(proposal.at("context_revision")==revision_,"stale_context");
        const auto choice=proposal.at("choice").get<std::string>();bool allowed=false;
        const auto context=decisionContext();
        for(const auto& option:context.at("allowed_choices"))if(option.at("choice")==choice&&option.at("target")==proposal.at("target")
            &&option.at("target_revision")==proposal.at("target_revision")){allowed=true;break;}
        require(allowed,"choice_not_available");const auto seconds=proposal.at("seconds").get<int>();
        require(choice=="wait"?seconds>=1:choice=="rest"?seconds>=15:seconds==0,"invalid_duration");
        const auto& text=proposal.at("text").get_ref<const std::string&>();
        require(text.size()<=640&&(choice=="reply"?!text.empty():text.empty()),"invalid_reply");
        issue.clear();return true;
    }catch(const std::exception& e){issue=e.what();return false;}
}

void Loop::applyChoice(const Json& proposal){
    std::string issue;require(validateChoice(proposal,issue),issue.c_str());count("model_calls");
    const auto choice=proposal.at("choice").get<std::string>();note("choice_approved",proposal);
    if(choice=="reply"){
        dialogueHistory_.push_back({{"speaker","character"},{"text",proposal.at("text")}});
        if(dialogueHistory_.size()>6)dialogueHistory_.erase(dialogueHistory_.begin());
        note("visible_reply",{{"text",proposal.at("text")}});
    }else if(choice=="continue"){
        if(goal_)note("goal_continued",goalJson(*goal_));
        else{
            auto resumed=*suspended_;resumed.startedAt=now_;suspended_.reset();goal_=resumed;
            focus_=resumed.kind=="eat"?"food-"+resumed.target:resumed.kind;focusAt_=now_;count("focus_switches");
            note("goal_resumed",goalJson(resumed));
            startOperation(resumed.kind=="eat"?(std::abs(objects_.at(resumed.target).x-x_)<=.01?"eat":"move"):resumed.kind);
        }
    }
    else if(choice=="abandon"){
        if(goal_)stopGoal("abandoned","character_changed_mind",false);
        if(suspended_){note("suspended_goal_abandoned",goalJson(*suspended_));count("goals_abandoned");suspended_.reset();}
        if(dialogue_.is_null())focus_.clear();
    }else if(choice!="silence"){
        if(goal_){
            if(choice=="wait"&&goal_->kind!="wait"){
                if(suspended_){note("suspended_goal_abandoned",goalJson(*suspended_));count("goals_abandoned");}
                suspended_=goal_;stopGoal("interrupted","character_paused_goal",false);
            }else stopGoal("abandoned","character_selected_another_activity",false);
        }
        const auto target=proposal.at("target").get<std::string>();
        Goal next{"goal-"+std::to_string(++serial_),choice,target,proposal.at("target_revision"),now_,proposal.at("seconds").get<std::int64_t>()*1000};
        if(choice!="wait"&&suspended_&&suspended_->kind==next.kind&&suspended_->target==next.target&&suspended_->targetRevision==next.targetRevision&&now_-suspended_->startedAt<=30000){
            next.id=suspended_->id;note("goal_resumed",goalJson(next));suspended_.reset();
        }
        if(choice!="wait"&&suspended_){note("suspended_goal_abandoned",goalJson(*suspended_));count("goals_abandoned");suspended_.reset();}
        const auto focus=choice=="eat"?"food-"+target:choice=="observe"?"observe-"+target:choice;
        if(focus_!=focus){focus_=focus;focusAt_=now_;count("focus_switches");note("focus_selected",{{"id",focus_}});}
        goal_=next;note("goal_started",goalJson(next));
        startOperation(choice=="eat"?(std::abs(objects_.at(target).x-x_)<=.01?"eat":"move"):choice);
    }else note("silence_selected");
    decisionNeeded_=false;
}

void Loop::load(const Json& saved) {
    require(saved.is_object()&&saved.value("format",std::string{})=="ha.cognitive-prototype.v1"
            &&saved.dump().size()<=1024*1024,"invalid_snapshot");
    const auto& config=saved.at("settings");
    require(config.at("attention").is_boolean()&&integer(config.at("dwell_ms"),0,10000)
            &&number(config.at("switch_margin"),0,1),"invalid_snapshot_settings");
    require(!config.contains("automatic_planning")||config.at("automatic_planning").is_boolean(),"invalid_snapshot_settings");
    settings_={config.at("attention"),config.at("dwell_ms"),config.at("switch_margin"),config.value("automatic_planning",true)};
    require(!saved.contains("decision_needed")||saved.at("decision_needed").is_boolean(),"invalid_snapshot_decision");
    decisionNeeded_=saved.value("decision_needed",false);dialogueHistory_=saved.value("dialogue_history",Json::array());
    require(dialogueHistory_.is_array()&&dialogueHistory_.size()<=6,"invalid_snapshot_dialogue_history");
    for(const auto& item:dialogueHistory_)require((item.at("speaker")=="user"||item.at("speaker")=="character")&&item.at("text").is_string()&&item.at("text").get_ref<const std::string&>().size()<=1024,"invalid_snapshot_dialogue_history");
    for(const auto* name:{"now","revision","serial","focus_at","rest_after"})require(integer(saved.at(name),0,1000000000),"invalid_snapshot_clock");
    require(number(saved.at("hunger"),0,1)&&number(saved.at("x"),0,1)&&saved.at("paused").is_boolean()
            &&number(saved.at("body").at("energy"),0,1),"invalid_snapshot_body");
    require(saved.at("objects").is_array()&&saved.at("objects").size()<=16&&saved.at("last_revisions").is_object()
            &&saved.at("last_revisions").size()<=256&&saved.at("seen").is_object()&&saved.at("seen").size()<=4096
            &&saved.at("blocked").is_array()&&saved.at("blocked").size()<=4096&&saved.at("trace").is_array()
            &&saved.at("trace").size()<=64,"invalid_snapshot_limits");
    now_=saved.at("now");revision_=saved.at("revision");serial_=saved.at("serial");focusAt_=saved.at("focus_at");restAfter_=saved.at("rest_after");
    hunger_=saved.at("hunger");x_=saved.at("x");paused_=saved.at("paused");focus_=saved.at("focus");mode_=saved.at("mode");decisionKey_=saved.at("decision_key");
    require(mode_=="normal"||mode_=="malformed"||mode_=="stale"||mode_=="silent","invalid_snapshot_model");
    objects_.clear();for(const auto& o:saved.at("objects")) {
        require(id(o.at("id"))&&integer(o.at("revision"),1,1000000)&&integer(o.at("expires_at"),0,1000000000)
                &&number(o.at("x"),0,1)&&number(o.at("salience"),0,1),"invalid_snapshot_object");
        const auto name=o.at("id").get<std::string>();require(!objects_.contains(name),"duplicate_snapshot_object");
        objects_[name]={name,o.at("revision"),o.at("expires_at"),o.at("x"),o.at("salience")};
    }
    lastRevision_=saved.at("last_revisions").get<std::map<std::string,std::int64_t>>();
    for(const auto& [name,rev]:lastRevision_)require(id(Json(name))&&rev>0&&rev<=1000000,"invalid_snapshot_revision");
    for(const auto& [name,o]:objects_)require(lastRevision_.contains(name)&&lastRevision_.at(name)==o.revision,"invalid_snapshot_revision");
    seen_.clear();for(const auto& [name,digest]:saved.at("seen").items()){
        require(id(Json(name))&&digest.is_string()&&digest.get_ref<const std::string&>().size()==64,"invalid_snapshot_seen");
        seen_[name]=digest;
    }
    blocked_=saved.at("blocked").get<std::set<std::string>>();
    auto goalFrom=[&](const Json& value)->std::optional<Goal>{
        if(value.is_null())return {};
        if(value.contains("duration_ms"))exact(value,{"id","kind","target","target_revision","started_at","duration_ms"});
        else exact(value,{"id","kind","target","target_revision","started_at"});
        require(id(value.at("id"))&&(value.at("kind")=="eat"||value.at("kind")=="rest"||value.at("kind")=="observe"||value.at("kind")=="wait")&&value.at("target").is_string()
                &&integer(value.at("target_revision"),0,1000000)&&integer(value.at("started_at"),0,now_),"invalid_snapshot_goal");
        require(!value.contains("duration_ms")||integer(value.at("duration_ms"),0,30000),"invalid_snapshot_duration");
        return Goal{value.at("id"),value.at("kind"),value.at("target"),value.at("target_revision"),value.at("started_at"),value.value("duration_ms",std::int64_t{})};
    };
    goal_=goalFrom(saved.at("goal"));suspended_=goalFrom(saved.at("suspended"));operation_.reset();
    if(!saved.at("operation").is_null()){
        const auto& op=saved.at("operation");
        require(goal_.has_value()&&id(op.at("token"))&&(op.at("kind")=="move"||op.at("kind")=="eat"||op.at("kind")=="rest"||op.at("kind")=="observe"||op.at("kind")=="wait")
                &&integer(op.at("started_at"),0,now_)&&integer(op.at("deadline"),now_+1,now_+31000)&&op.at("accepted").is_boolean(),"invalid_snapshot_operation");
        operation_=Operation{op.at("token"),op.at("kind"),op.at("expected"),op.at("started_at"),op.at("deadline"),op.at("accepted")};
    }
    require(goal_.has_value()==operation_.has_value(),"invalid_snapshot_execution");
    dialogue_=saved.at("dialogue");require(dialogue_.is_null()||(dialogue_.is_object()&&id(dialogue_.at("id"))
            &&dialogue_.at("text").is_string()&&dialogue_.at("text").get_ref<const std::string&>().size()<=1024),"invalid_snapshot_dialogue");
    require(saved.at("metrics").is_object()&&saved.at("metrics").size()==metrics_.size(),"invalid_snapshot_metrics");
    for(const auto& [name,value]:metrics_.items())require(saved.at("metrics").contains(name)&&integer(saved.at("metrics").at(name),0,10000000),"invalid_snapshot_metrics");
    metrics_=saved.at("metrics");trace_=saved.at("trace");emitted_=Json::array();body_.restore(saved.at("body"),now_);
}

void Loop::restore(const Json& saved, bool restart) {
    Loop checked;checked.load(saved);
    if(restart) {
        checked.sourceEvent_="restart";
        if(checked.goal_)checked.stopGoal("unknown","restart_unconfirmed_execution",true);
        if(checked.suspended_) {checked.note("suspended_goal_abandoned",checked.goalJson(*checked.suspended_));checked.count("goals_abandoned");checked.suspended_.reset();}
        checked.focus_.clear();checked.dialogue_=nullptr;checked.decisionKey_.clear();
        checked.decisionNeeded_=false;
        checked.note("restarted",{{"offline_time_applied",false}});
    }
    const auto events=checked.emitted_;load(checked.snapshot());emitted_=events;
}
}
