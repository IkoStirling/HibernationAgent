#include <Hibernation/MentorChatBridge.h>
#include <Hibernation/RolePlugin.h>
#ifdef HA_HAS_WEBVIEW
#include <AYPlatform/WebView.h>
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <regex>
#include <set>

namespace hibernation {
namespace {
using Clock=std::chrono::steady_clock;
bool navigationAllowed(const std::string& url){
    if(url.size()>4096||!url.starts_with("https://")||url.find_first_of("\r\n\\")!=std::string::npos||url.find('\0')!=std::string::npos)return false;
    auto host=url.substr(8,url.find_first_of("/?#",8)-8);std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});
    return host=="chatgpt.com"||host=="auth.openai.com"||host=="auth0.openai.com"||host=="accounts.google.com"||host=="login.microsoftonline.com"||host=="login.live.com"||host=="account.live.com"||host=="appleid.apple.com"||host=="account.apple.com"||host=="idmsa.apple.com";
}
bool conversationUrl(const std::string& url){static const std::regex pattern(R"(^https://chatgpt\.com/c/[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}/?$)");return std::regex_match(url,pattern);}
bool openUrl(const std::string& url){return url=="https://chatgpt.com"||url=="https://chatgpt.com/"||conversationUrl(url);}
std::string safeUrl(const std::string& url){const auto clean=url.substr(0,url.find_first_of("?#"));if(conversationUrl(clean))return clean;if(url.starts_with("https://auth.openai.com/"))return "https://auth.openai.com/";if(url.starts_with("https://auth0.openai.com/"))return "https://auth0.openai.com/";return "https://chatgpt.com/";}
bool requestId(const std::string& value){return !value.empty()&&value.size()<=128&&value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.:-")==std::string::npos;}
std::string base64(std::string_view input){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;out.reserve((input.size()+2)/3*4);for(std::size_t i=0;i<input.size();i+=3){const unsigned a=static_cast<unsigned char>(input[i]),b=i+1<input.size()?static_cast<unsigned char>(input[i+1]):0,c=i+2<input.size()?static_cast<unsigned char>(input[i+2]):0;out+=chars[a>>2];out+=chars[((a&3)<<4)|(b>>4)];out+=i+1<input.size()?chars[((b&15)<<2)|(c>>6)]:'=';out+=i+2<input.size()?chars[c&63]:'=';}return out;}
std::uint64_t signature(std::string_view text){std::uint64_t value=1469598103934665603ULL;for(unsigned char c:text){value^=c;value*=1099511628211ULL;}return value;}
// BEGIN MENTOR CHAT HOST SCRIPT. Tested verbatim by MentorChatPage.mjs.
constexpr std::string_view mentorChatPage=R"MENTORJS((function(op,arg){
'use strict';
const fail=error=>({ok:false,error}),vis=e=>!!e&&e.getClientRects().length>0&&getComputedStyle(e).visibility!=='hidden';
const list=s=>[...document.querySelectorAll(s)].filter(vis),label=e=>(e.getAttribute('aria-label')||e.innerText||'').trim();
const url=()=>/^\/c\/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\/?$/i.test(location.pathname)?'https://chatgpt.com'+location.pathname:'https://chatgpt.com/';
const stop=()=>list('button[data-testid="stop-button"],button[aria-label="Stop generating"],button[aria-label="停止生成"]').find(e=>!e.disabled);
const editor=()=>list('#prompt-textarea[contenteditable="true"],textarea#prompt-textarea,textarea[data-testid="prompt-textarea"]').find(e=>!e.disabled);
const send=()=>list('button[data-testid="send-button"],button[aria-label="Send prompt"],button[aria-label="Send message"],button[aria-label="发送提示"],button[aria-label="发送消息"]').find(e=>!e.disabled&&e.getAttribute('aria-disabled')!=='true');
const authors=role=>list('[data-message-author-role="'+role+'"]');
const text=e=>e?('value'in e?e.value:e.innerText||''):'';
const status=()=>{
 if(location.hostname!=='chatgpt.com')return {ready:false,login_required:true,work_mode_detected:false,mode_verified:false,state:'login_required',current_url:'https://'+location.hostname+'/'};
 const controls=list('button,[role="tab"],[role="switch"]'),chosen=e=>e.getAttribute('aria-pressed')==='true'||e.getAttribute('aria-selected')==='true'||e.getAttribute('aria-checked')==='true'||['active','on','checked'].includes(e.getAttribute('data-state'));
 const work=e=>/^(work|work mode|agent|agent mode|工作|工作模式|代理模式|智能体模式)$/i.test(label(e));
 const workOn=controls.some(e=>work(e)&&chosen(e))||list('[data-mode="work"],[data-chat-mode="work"]').length>0;
 const profile=list('[data-testid="accounts-profile-button"],[data-testid="profile-button"],button[aria-label="Open profile menu"],button[aria-label="打开个人资料菜单"]').length>0;
 const login=controls.some(e=>/^(log in|sign in|登录|登入)$/i.test(label(e)));
 const chatOn=controls.some(e=>/^(chat|chat mode|聊天|普通聊天|对话)$/i.test(label(e))&&chosen(e));
 const workOff=controls.some(e=>work(e)&&(e.getAttribute('aria-pressed')==='false'||e.getAttribute('aria-selected')==='false'||e.getAttribute('aria-checked')==='false'));
 const model=list('button[data-testid="model-switcher-dropdown-button"]').some(e=>/chatgpt/i.test(label(e))&&!/work|agent|工作|代理/i.test(label(e)));
 const verified=!workOn&&(chatOn||workOff),logged=profile&&!login,ready=logged&&verified&&!!editor();
 return {ready,login_required:login,identity_verified:logged,work_mode_detected:workOn,mode_verified:verified,mode_evidence:chatOn?'chat_selected':workOff?'work_explicitly_off':model?'model_selector_only_unverified':'unverified',state:workOn?'work_mode':login?'login_required':!logged?'identity_unverified':!verified?'mode_unverified':ready?'ready':'markup_changed',current_url:url(),streaming:!!stop()};
};
if(op==='status')return {ok:true,...status()};
const key='__hibernationMentorChatBridgeV1';let g=window[key];
if(op==='cancel'){
 if(!g||g.id!==arg.request_id)return {ok:true,state:'cancelled',stop_requested:false};
 const button=(g.accepted||g.phase==='submitted')&&authors('user').length<=g.u0+1?stop():null;if(button)button.click();g.phase='cancelled';g.prompt='';return {ok:true,state:'cancelled',stop_requested:!!button};
}
if(op==='prepare'){
 if(arg.ensure_new&&location.pathname!=='/')return fail('mentor_chat_new_chat_required');
 const s=status();if(s.work_mode_detected)return fail('mentor_chat_work_mode');if(s.login_required)return fail('mentor_chat_login_required');if(!s.identity_verified)return fail('mentor_chat_identity_unverified');if(!s.mode_verified)return fail('mentor_chat_mode_unverified');if(!s.ready)return fail('mentor_chat_markup_changed');if(s.streaming)return fail('mentor_chat_streaming');
 if(g&&!['complete','cancelled','failed'].includes(g.phase))return fail('mentor_chat_request_busy');const e=editor();if(text(e).trim())return fail('mentor_chat_existing_draft');
 let prompt;try{prompt=new TextDecoder('utf-8',{fatal:true}).decode(Uint8Array.from(atob(arg.prompt_b64),c=>c.charCodeAt(0)));}catch{return fail('mentor_chat_prompt_limit');}
 g=window[key]={id:arg.request_id,phase:'filled',a0:authors('assistant').length,u0:authors('user').length,prompt,accepted:false,origin:url(),last:'',changed:0};
 e.focus();if('value'in e){const setter=Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value')?.set;if(!setter)return fail('mentor_chat_markup_changed');setter.call(e,prompt);}else{
  const range=document.createRange();range.selectNodeContents(e);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);if(!document.execCommand('insertText',false,prompt)){e.replaceChildren(document.createTextNode(prompt));}
 }
 e.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:prompt}));return {ok:true,...s,state:'prepared',accepted:false};
}
if(!g||g.id!==arg.request_id)return fail('mentor_chat_context_lost');
if(op==='commit'){
 if(arg.ensure_new&&location.pathname!=='/')return fail('mentor_chat_new_chat_required');
 if(g.phase==='submitted'||g.accepted)return {ok:true,state:'waiting',accepted:g.accepted};if(g.phase!=='filled')return fail('mentor_chat_cancelled');
 const s=status();if(s.work_mode_detected)return fail('mentor_chat_work_mode');if(s.login_required)return fail('mentor_chat_login_required');if(!s.identity_verified)return fail('mentor_chat_identity_unverified');if(!s.mode_verified)return fail('mentor_chat_mode_unverified');if(s.streaming)return fail('mentor_chat_streaming');if(text(editor()).trim()!==g.prompt.trim())return fail('mentor_chat_draft_changed');
 const button=send();if(!button)return {ok:true,state:'prepared',accepted:false};g.phase='submitted';button.click();return {ok:true,state:'submitted',accepted:false};
}
if(op!=='poll')return fail('mentor_chat_operation');if(g.phase==='cancelled')return fail('mentor_chat_cancelled');if(g.phase==='complete')return {ok:true,state:'complete',accepted:true,text:g.result,url:g.url,truncated:g.truncated};
const users=authors('user'),assistants=authors('assistant');if(users.length>g.u0+1)return fail('mentor_chat_interrupted');
if(!g.accepted){const newest=users[g.u0],same=newest&&text(newest).replace(/\s+/g,' ').trim()===g.prompt.replace(/\s+/g,' ').trim();if(!same)return {ok:true,state:'waiting',accepted:false};g.accepted=true;g.prompt='';}
const current=url();if(g.origin!=='https://chatgpt.com/'&&g.origin!==current)return fail('mentor_chat_context_lost');
if(assistants.length<=g.a0||stop())return {ok:true,state:'waiting',accepted:true,url:current};const last=assistants.at(-1);
if(last.querySelector('[data-is-streaming="true"],.result-streaming,[data-testid="streaming-indicator"]'))return {ok:true,state:'waiting',accepted:true,url:current};
const blocks=[...last.querySelectorAll('.markdown,[data-testid="assistant-message"]')].filter(e=>vis(e)&&!e.closest('[data-message-type="reasoning"],[data-testid*="reasoning"],[data-testid*="thinking"],details'));
const final=blocks.at(-1);if(!final||!text(final).trim())return {ok:true,state:'waiting',accepted:true};const value=text(final).trim();
if(g.last!==value){g.last=value;g.changed=Date.now();return {ok:true,state:'waiting',accepted:true};}if(Date.now()-g.changed<1200)return {ok:true,state:'waiting',accepted:true};
let result='',bytes=0;const encode=new TextEncoder();for(const c of value){const size=encode.encode(c).length;if(bytes+size>24576)break;result+=c;bytes+=size;}
g.phase='complete';g.result=result;g.url=current;g.truncated=result.length!==value.length;return {ok:true,state:'complete',accepted:true,text:result,url:current,truncated:g.truncated};
}))MENTORJS";
// END MENTOR CHAT HOST SCRIPT.
}

struct MentorChatBridge::Impl {
    const Config& cfg;DWORD owner=GetCurrentThreadId();bool stopped=false;std::mutex mutex;
    struct Command{std::string operation,stage;Json args;std::promise<Json> promise;std::atomic<bool> cancelled=false,answered=false,maySend=false;Clock::time_point deadline=Clock::now()+std::chrono::seconds(12),retry{};bool awaiting=false;void complete(Json value){if(!answered.exchange(true))promise.set_value(std::move(value));}};
    struct Request{std::uint64_t hash=0;std::size_t bytes=0;std::string state="preparing";Json result=Json::object();};
    std::deque<std::shared_ptr<Command>> queue;std::shared_ptr<Command> active;std::map<std::string,Request> requests;std::set<std::string> used;std::string current;
    Json cached={{"ready",false},{"login_required",false},{"work_mode_detected",false},{"mode_verified",false},{"state","closed"},{"current_url","https://chatgpt.com/"}};Clock::time_point nextRefresh{};
#ifdef HA_HAS_WEBVIEW
    HWND window=nullptr;std::unique_ptr<ayt::platform::WebView> view;std::string desired="https://chatgpt.com/";bool navigated=false,ensureNew=false,visibleWanted=false;
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(self){if(message==WM_CLOSE){self->visibleWanted=false;ShowWindow(hwnd,SW_HIDE);return 0;}if(message==WM_SIZE&&self->view){self->view->bounds(std::max(1,int(LOWORD(l))),std::max(1,int(HIWORD(l))));return 0;}if(message==WM_GETMINMAXINFO){auto* limits=reinterpret_cast<MINMAXINFO*>(l);limits->ptMinTrackSize={540,420};return 0;}}return DefWindowProcW(hwnd,message,w,l);}
    Json open(const Json& args){
        const auto target=args.value("url",std::string("https://chatgpt.com/"));if(!openUrl(target))return toolFailure("mentor_chat_url");
        visibleWanted=args.value("visible",true);ensureNew=args.value("ensure_new",false);if(ensureNew&&conversationUrl(target))return toolFailure("mentor_chat_arguments");
        if(window&&view){if(ensureNew&&!current.empty())return toolFailure("mentor_chat_request_busy");if(ensureNew&&conversationUrl(safeUrl(view->source()))){desired="https://chatgpt.com/";navigated=false;cached["ready"]=false;cached["state"]="loading";}else if(conversationUrl(target)&&safeUrl(view->source())!=target){if(!current.empty())return toolFailure("mentor_chat_request_busy");desired=target;navigated=false;}ShowWindow(window,visibleWanted?SW_SHOW:SW_HIDE);view->visible(true);return toolSuccess({{"opened",true},{"state",cached.value("state","loading")},{"current_url",safeUrl(view->source())}});}
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"HibernationMentorChatV1";cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
        window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"导师 · ChatGPT（实验性网页通道）",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,900,760,nullptr,nullptr,cls.hInstance,this);if(!window)return toolFailure("mentor_chat_window_failed");
        desired=target;view=std::make_unique<ayt::platform::WebView>();if(!view->initialize(reinterpret_cast<std::uintptr_t>(window),cfg.dataRoot/"runtime/browser/mentor-chat",navigationAllowed)){view->close();view.reset();DestroyWindow(window);window=nullptr;return toolFailure("mentor_chat_not_available");}RECT rect{};GetClientRect(window,&rect);view->bounds(rect.right,rect.bottom);view->visible(true);ShowWindow(window,visibleWanted?SW_SHOW:SW_HIDE);cached["state"]="loading";return toolSuccess({{"opened",true},{"state","loading"},{"login","user_managed_in_page"}});
    }
    void finish(const std::shared_ptr<Command>& command,Json value){if(command->operation=="mentor_chat.send"&& !value.value("ok",false)){const auto id=command->args.value("request_id",std::string{});if(requests.contains(id)){requests[id].state=command->maySend?"uncertain":"failed";if(!command->maySend&&current==id)current.clear();}}command->complete(std::move(value));}
    void script(const std::shared_ptr<Command>& command,const std::string& operation,Json args){
        const auto code=std::string(mentorChatPage)+"("+Json(operation).dump()+","+args.dump()+")";if(code.size()>32768){finish(command,toolFailure("mentor_chat_prompt_limit"));return;}command->awaiting=true;
        view->script(code,[this,command,operation](bool ok,std::string output){command->awaiting=false;if(stopped||command->answered||command->cancelled)return;auto value=Json::parse(output,nullptr,false);if(!ok||!value.is_object()||!value.value("ok",false)){const auto reason=value.is_object()?value.value("error",std::string("mentor_chat_page_failed")):"mentor_chat_page_failed";finish(command,toolFailure(reason.starts_with("mentor_chat_")?reason.c_str():"mentor_chat_page_failed"));return;}
            value.erase("ok");if(operation=="status"){value["current_url"]=safeUrl(view->source());cached=value;command->complete(toolSuccess(value));return;}
            const auto id=command->args.value("request_id",std::string{});
            if(command->operation=="mentor_chat.send"){
                if(operation=="prepare"){command->stage="commit";command->retry=Clock::now()+std::chrono::milliseconds(80);return;}
                if(operation=="commit"&&value.value("state",std::string{})=="prepared"){command->retry=Clock::now()+std::chrono::milliseconds(80);return;}
                if(operation=="commit"){ensureNew=false;command->stage="accept";command->retry=Clock::now()+std::chrono::milliseconds(80);return;}
                if(!value.value("accepted",false)){command->retry=Clock::now()+std::chrono::milliseconds(100);return;}
            }
            if(command->operation=="mentor_chat.poll"||command->operation=="mentor_chat.send"){auto& request=requests.at(id);request.state=value.value("state",std::string("waiting"));value["request_id"]=id;if(value.contains("url")){const auto url=value.at("url").get<std::string>();if(!conversationUrl(url))value.erase("url");}if(value.value("state",std::string{})=="complete"){if(!value.contains("text")||!value.at("text").is_string()||value.at("text").get_ref<const std::string&>().size()>24576){finish(command,toolFailure("mentor_chat_result_limit"));return;}request.result=value;if(current==id)current.clear();}}
            command->complete(toolSuccess(value));
        });
    }
    void execute(const std::shared_ptr<Command>& command){
        const auto& op=command->operation;const auto& args=command->args;
        if(op=="mentor_chat.open"){command->complete(open(args));return;}if(!view||!view->ready()||!navigated||view->loading()){command->complete(toolFailure("mentor_chat_not_ready",true));return;}
        if(op=="mentor_chat.status"){script(command,"status",{});return;}
        const auto id=args.value("request_id",std::string{});if(op=="mentor_chat.cancel"){if(requests.contains(id))requests[id].state="cancelled";if(current==id)current.clear();script(command,"cancel",{{"request_id",id}});return;}
        if(op=="mentor_chat.poll"){if(!requests.contains(id)){command->complete(toolFailure("mentor_chat_request_unknown"));return;}if(requests.at(id).state=="complete"){command->complete(toolSuccess(requests.at(id).result));return;}if(requests.at(id).state=="cancelled"){command->complete(toolFailure("mentor_chat_cancelled"));return;}script(command,"poll",{{"request_id",id}});return;}
        if(op!="mentor_chat.send"){command->complete(toolFailure("mentor_chat_operation"));return;}
        if(command->stage.empty()){
            if(ensureNew&&conversationUrl(safeUrl(view->source()))){desired="https://chatgpt.com/";navigated=false;cached["ready"]=false;cached["state"]="loading";command->complete(toolFailure("mentor_chat_not_ready",true));return;}
            const auto& prompt=args.at("prompt").get_ref<const std::string&>();if(requests.contains(id)&&used.contains(id)){const auto& old=requests.at(id);if(old.hash!=signature(prompt)||old.bytes!=prompt.size()){command->complete(toolFailure("mentor_chat_request_conflict"));return;}if(old.state=="complete")command->complete(toolSuccess(old.result));else command->complete(toolFailure("mentor_chat_request_already_sent"));return;}if(used.contains(id)){command->complete(toolFailure("mentor_chat_request_already_sent"));return;}
            if(!current.empty()||used.size()>=4096){command->complete(toolFailure("mentor_chat_request_busy"));return;}while(requests.size()>=32){auto old=std::find_if(requests.begin(),requests.end(),[&](const auto& item){return item.first!=current&&item.second.state!="preparing"&&item.second.state!="waiting";});if(old==requests.end()){command->complete(toolFailure("mentor_chat_request_busy"));return;}requests.erase(old);}
            requests[id]={signature(prompt),prompt.size(),"preparing",Json::object()};current=id;command->stage="prepare";const auto encoded=base64(prompt);script(command,"prepare",{{"request_id",id},{"prompt_b64",encoded},{"ensure_new",ensureNew}});return;
        }
        if(command->stage=="commit"){used.insert(id);command->maySend=true;requests.at(id).state="waiting";script(command,"commit",{{"request_id",id},{"ensure_new",ensureNew}});return;}script(command,"poll",{{"request_id",id}});
    }
#endif
    explicit Impl(const Config& value):cfg(value){}
};
MentorChatBridge::MentorChatBridge(const Config& cfg):impl_(std::make_unique<Impl>(cfg)){}
MentorChatBridge::~MentorChatBridge(){stop();}
Json MentorChatBridge::invoke(const std::string& operation,const Json& args,const std::function<bool()>& cancelled){
    auto& self=*impl_;if(!args.is_object())return toolFailure("mentor_chat_arguments");if(operation!="mentor_chat.open"&&operation!="mentor_chat.status"&&operation!="mentor_chat.send"&&operation!="mentor_chat.poll"&&operation!="mentor_chat.cancel")return toolFailure("mentor_chat_operation");
    for(auto it=args.begin();it!=args.end();++it)if((operation=="mentor_chat.open"&&it.key()!="url"&&it.key()!="ensure_new"&&it.key()!="visible")||(operation=="mentor_chat.status")||(operation=="mentor_chat.send"&&it.key()!="request_id"&&it.key()!="prompt")||((operation=="mentor_chat.poll"||operation=="mentor_chat.cancel")&&it.key()!="request_id"))return toolFailure("mentor_chat_arguments");
    if(args.contains("visible")&&!args.at("visible").is_boolean())return toolFailure("mentor_chat_arguments");if(args.contains("ensure_new")&&!args.at("ensure_new").is_boolean())return toolFailure("mentor_chat_arguments");if(args.contains("url")&&(!args.at("url").is_string()||!openUrl(args.at("url"))))return toolFailure("mentor_chat_url");if(args.contains("request_id")&&(!args.at("request_id").is_string()||!requestId(args.at("request_id"))))return toolFailure("mentor_chat_arguments");
    if(operation=="mentor_chat.send"){if(!args.contains("request_id")||!args.contains("prompt")||!args.at("prompt").is_string())return toolFailure("mentor_chat_arguments");const auto& prompt=args.at("prompt").get_ref<const std::string&>();if(prompt.empty()||prompt.size()>24576||prompt.find('\0')!=std::string::npos||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,prompt.data(),int(prompt.size()),nullptr,0)<=0)return toolFailure("mentor_chat_prompt_limit");}
    if(operation=="mentor_chat.poll"&&!args.contains("request_id"))return toolFailure("mentor_chat_arguments");if(cancelled&&cancelled())return toolFailure("mentor_chat_cancelled");
#ifndef HA_HAS_WEBVIEW
    return toolFailure("mentor_chat_not_available");
#else
    if(!self.cfg.desktopEnabled)return toolFailure("mentor_chat_desktop_required");auto command=std::make_shared<Impl::Command>();command->operation=operation;command->args=args;
    if(GetCurrentThreadId()==self.owner){
        if(self.stopped)return toolFailure("mentor_chat_closed");if(operation=="mentor_chat.open")return self.open(args);
        if(operation=="mentor_chat.status"){self.nextRefresh={};return toolSuccess(self.cached);}
        if(operation=="mentor_chat.cancel"){const auto id=args.value("request_id",self.current);if(self.requests.contains(id))self.requests[id].state="cancelled";if(self.current==id)self.current.clear();command->args={{"request_id",id}};if(self.active&&self.active->operation=="mentor_chat.send"){self.active->cancelled=true;self.active->complete(toolFailure("mentor_chat_cancelled"));}std::lock_guard lock(self.mutex);while(self.queue.size()>=16){self.queue.back()->complete(toolFailure("mentor_chat_queue_unavailable"));self.queue.pop_back();}self.queue.push_front(command);return toolSuccess({{"state","cancelled"},{"request_id",id},{"stop_requested",true}});}
        return toolFailure("mentor_chat_worker_required");
    }
    auto future=command->promise.get_future();{std::lock_guard lock(self.mutex);if(self.stopped||self.queue.size()>=16)return toolFailure("mentor_chat_queue_unavailable");self.queue.push_back(command);}
    while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready){if((cancelled&&cancelled())||Clock::now()>command->deadline){command->cancelled=true;return toolFailure(command->maySend?"mentor_chat_send_uncertain":"mentor_chat_cancelled");}}return future.get();
#endif
}
void MentorChatBridge::tick(){
#ifdef HA_HAS_WEBVIEW
    auto& self=*impl_;if(self.stopped||GetCurrentThreadId()!=self.owner)return;if(self.view&&self.ensureNew&&self.current.empty()&&self.view->ready()&&!self.view->loading()&&conversationUrl(safeUrl(self.view->source()))){self.desired="https://chatgpt.com/";self.navigated=false;self.cached["ready"]=false;}
    if(self.view&&!self.navigated&&self.view->ready()){self.navigated=self.view->navigate(self.desired);self.cached["state"]="loading";}
    if(self.active){auto command=self.active;if(command->cancelled||Clock::now()>command->deadline){if(command->operation=="mentor_chat.send"){const auto id=command->args.value("request_id",std::string{});if(self.requests.contains(id))self.requests[id].state=command->maySend?"uncertain":"failed";if(!command->maySend&&self.current==id)self.current.clear();}command->complete(toolFailure(command->maySend?"mentor_chat_send_uncertain":"mentor_chat_cancelled"));}if(command->answered)self.active.reset();else if(!command->awaiting&&Clock::now()>=command->retry)self.execute(command);}
    if(!self.active){std::lock_guard lock(self.mutex);while(!self.queue.empty()){auto command=self.queue.front();self.queue.pop_front();if(command->cancelled||command->answered)continue;self.active=command;break;}}
    if(self.active&&!self.active->awaiting&&!self.active->answered&&Clock::now()>=self.active->retry)self.execute(self.active);
    if(!self.active&&self.view&&self.view->ready()&&self.navigated&&!self.view->loading()&&Clock::now()>=self.nextRefresh){self.nextRefresh=Clock::now()+std::chrono::seconds(1);self.active=std::make_shared<Impl::Command>();self.active->operation="mentor_chat.status";self.execute(self.active);}
#endif
}
void MentorChatBridge::stop(){if(!impl_)return;auto& self=*impl_;{std::lock_guard lock(self.mutex);self.stopped=true;for(auto& command:self.queue)command->complete(toolFailure("mentor_chat_closed"));self.queue.clear();}if(self.active)self.active->complete(toolFailure("mentor_chat_closed"));self.active.reset();
#ifdef HA_HAS_WEBVIEW
    if(self.view)self.view->close();self.view.reset();if(self.window){DestroyWindow(self.window);self.window=nullptr;}
#endif
}
}
