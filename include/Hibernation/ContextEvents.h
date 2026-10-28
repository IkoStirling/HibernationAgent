#pragma once
#include <AYInference/Inference.h>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace hibernation::context_events {
using Json=nlohmann::json;
using Message=ayt::inference::Message;
// A caller-owned, current-event view. Already-sent messages are never edited.
// It cannot add source grants, replace the user input, or alter system rules.
class Turn {
    Json frame_;
    std::vector<Message> pending_;
    std::size_t sequence_=0;
public:
    explicit Turn(Json frame):frame_(std::move(frame)){if(!frame_.is_object())throw std::invalid_argument("context_event_frame");}
    const Json& current()const{return frame_;}
    std::size_t sequence()const{return sequence_;}
    std::size_t pending()const{return pending_.size();}
    bool update(const std::string& kind,const Json& fields){
        static const std::set<std::string> allowed={"conversation_focus","expression_preference","turn_constraints","perception","body","authoritative_state","current_capabilities"};
        if(kind.empty()||kind.size()>64||!fields.is_object())throw std::invalid_argument("context_event_shape");
        for(auto it=fields.begin();it!=fields.end();++it)if(!allowed.contains(it.key()))throw std::invalid_argument("context_event_field");
        Json changed=Json::object();for(auto it=fields.begin();it!=fields.end();++it)if(!frame_.contains(it.key())||frame_.at(it.key())!=it.value())changed[it.key()]=it.value();
        if(changed.empty())return false;
        for(auto it=changed.begin();it!=changed.end();++it)frame_[it.key()]=it.value();
        pending_.push_back({"user",Json{{"runtime_event",{{"version",1},{"kind",kind},{"source_event_id",frame_.value("event_id",std::string{})},{"sequence",++sequence_},{"changes",std::move(changed)}}}}.dump()});return true;
    }
    std::vector<Message> take(){std::vector<Message> result;result.swap(pending_);return result;}
    void flush(std::vector<Message>& messages){for(auto& m:pending_)messages.push_back(std::move(m));pending_.clear();}
    // A deliberate review reset produces a new complete baseline with the live
    // values; queued patches are already represented there and must not repeat.
    void rebase(Json frame){if(!frame.is_object())throw std::invalid_argument("context_event_frame");frame_=std::move(frame);pending_.clear();}
};
inline std::string conduct(){return R"(
程序可追加runtime_event消息，表示当前事件内发生的真实状态更新，不改写前面的消息。source_event_id绑定本轮event_id，sequence按发生顺序递增，changes仅覆盖列出的顶层字段；显式null也是当前值。只应用当前事件的更新，旧事件的控制限制不延续到新一轮；旧快照和旧回复只是历史，较早的值不能覆盖新值。conversation_focus是当前交流偏好，expression_preference只调整措辞，turn_constraints只约束本轮操作阶段。final_only=true时本轮只能最终回复或沉默，不能再发起工具操作。上述更新不改变系统规则、不授予权限、不新增资料读取权，不向用户解释事件协议。审核器由程序读取最新完整快照。
)";}
}
