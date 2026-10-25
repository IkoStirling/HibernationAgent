#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace hibernation {
struct ChatBubble {std::string id;bool user=false;std::wstring text;};
/// Desktop-only rounded conversation layout with independent native selections.
class ChatBubbles {
public:
    ChatBubbles();~ChatBubbles();
    std::uintptr_t create(std::uintptr_t parent,std::uintptr_t bodyFont,std::uintptr_t labelFont);
    void messages(std::vector<ChatBubble> values);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
