#include <Hibernation/Application.h>
#include <Hibernation/ContextControl.h>
#include <AYSerializer/JsonSchema.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <AYNetwork/HttpClient.h>

namespace hibernation {
std::string outputSchema() {
    return R"({"type":"object","additionalProperties":false,"required":["visible_reply","actions","memories","state_update","stage_proposal"],"properties":{
      "visible_reply":{"type":"string","maxLength":6000},
      "actions":{"type":"array","maxItems":4,"items":{"type":"object","additionalProperties":false,"required":["kind","path","content"],"properties":{"kind":{"enum":["write_draft","read_material"]},"path":{"type":"string","minLength":1,"maxLength":240},"content":{"type":"string","maxLength":8192}}}},
      "memories":{"type":"array","maxItems":8,"items":{"type":"object","additionalProperties":false,"required":["kind","text","source_ids"],"properties":{"kind":{"enum":["conversation","story","outer","inference","summary"]},"text":{"type":"string","minLength":1,"maxLength":1024},"source_ids":{"type":"array","minItems":1,"maxItems":8,"items":{"type":"string","minLength":1,"maxLength":200}}}}},
      "state_update":{"type":"object","additionalProperties":false,"properties":{"attention":{"type":"string","maxLength":240},"open_questions":{"type":"array","maxItems":8,"items":{"type":"string","maxLength":240}}}},
      "stage_proposal":{"anyOf":[{"type":"null"},{"type":"object","additionalProperties":false,"required":["stage_id","reason","evidence_ids"],"properties":{"stage_id":{"type":"string","maxLength":64},"reason":{"type":"string","minLength":1,"maxLength":1024},"evidence_ids":{"type":"array","minItems":1,"maxItems":8,"items":{"type":"string","minLength":1,"maxLength":200}}}}]}
    }})";
}
bool safeRelative(std::string_view path) {
    if(path.empty()||path.size()>240||path.front()=='/'||path.back()=='/'||path.find_first_of("\\:*?\"<>|\0",0,9)!=std::string_view::npos) return false;
    for(unsigned char c:path) if(c<32) return false;
    size_t from=0;
    while(from<path.size()) {
        const auto end=path.find('/',from);const auto part=path.substr(from,end==path.npos?path.size()-from:end-from);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' '||part.starts_with(".system")) return false;
        auto stem=std::string(part.substr(0,part.find('.')));
        std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
        if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
            (stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='1'&&stem[3]<='9')) return false;
        if(end==path.npos) break;from=end+1;
    }
    return true;
}
ValidatedOutput validateOutput(std::string_view text,const ProposalContext& context) {
    if(!ayt::serializer::validateJsonDocument(text,outputSchema())) return {false,"output_schema",{}};
    auto value=Json::parse(text);
    const auto visible=value.at("visible_reply").get<std::string>();if(context.eventKind=="user_message" && visible.find_first_not_of(" \t\r\n")==std::string::npos) return {false,"empty_reply",{}};
    if(std::any_of(visible.begin(),visible.end(),[](unsigned char c){return c<32&&c!='\t'&&c!='\r'&&c!='\n';}))return {false,"reply_control_character",{}};
    for(const auto& action:value.at("actions")) {
        const auto path=action.at("path").get<std::string>();
        if(!safeRelative(path)) return {false,"workspace_path",{}};
        if(action.at("kind")=="write_draft" && (action.at("content").get<std::string>().empty()||action.at("content").get<std::string>().size()>16384||
            !(path.ends_with(".md")||path.ends_with(".txt")||path.ends_with(".json")))) return {false,"draft_policy",{}};
        if(action.at("kind")=="read_material" && !action.at("content").get<std::string>().empty()) return {false,"read_policy",{}};
    }
    for(const auto& memory:value.at("memories")) {
        const auto kind=memory.at("kind").get<std::string>();std::set<std::string> ids;
        for(const auto& id:memory.at("source_ids")) {
            auto key=id.get<std::string>();if(!context.sources.contains(key)||!ids.insert(key).second) return {false,"memory_source",{}};
        }
        if((kind=="conversation" && context.eventKind!="user_message") || (kind=="story" && context.eventKind!="story_end") ||
            (kind=="outer" && context.eventKind!="outer_event" && context.eventKind!="task_complete")) return {false,"memory_category",{}};
        if((kind=="conversation"||kind=="story"||kind=="outer") && !ids.contains(context.eventId)) return {false,"experience_source",{}};
    }
    if(!value.at("stage_proposal").is_null()) {
        const auto& stage=value.at("stage_proposal");std::set<std::string> evidence;
        if(context.nextStage.empty()||stage.at("stage_id")!=context.nextStage) return {false,"stage_transition",{}};
        for(const auto& id:stage.at("evidence_ids")) {
            auto key=id.get<std::string>();auto it=context.sources.find(key);
            if(it==context.sources.end()||!evidence.insert(key).second||
                (it->second.kind!="user_message"&&it->second.kind!="story_end"&&it->second.kind!="outer_event"&&it->second.kind!="task_complete")) return {false,"stage_evidence",{}};
        }
        if(evidence.size()<context.minimumEvidence) return {false,"stage_evidence_count",{}};
    }
    return {true,"ok",std::move(value)};
}
std::string readFile(const std::filesystem::path& path,std::size_t maxBytes) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input||input.tellg()<0||static_cast<std::uint64_t>(input.tellg())>maxBytes) throw std::runtime_error("file_unavailable_or_limit");
    std::string data(static_cast<size_t>(input.tellg()),'\0');input.seekg(0);if(!data.empty()&&!input.read(data.data(),static_cast<std::streamsize>(data.size()))) throw std::runtime_error("file_read");return data;
}
Config loadConfig(const std::filesystem::path& path) {
    const auto text=readFile(path,64*1024);
    if(!ayt::serializer::validateJsonDocument(text,"{}")) throw std::runtime_error("config_json");
    const auto j=Json::parse(text);
    const std::set<std::string> allowed={"schemaVersion","dataRoot","characterRoot","uiRoot","port","credentialEnvironment","contextBytes","recentMessageCount","maxMemoriesInContext","solitudeIntervalMs","idleBeforeSolitudeMs","reflectOnActivities","maxStartsPerHour","profiles","pluginRoot","plugins","draftsRoot","materialsRoot","remoteStorage","maxToolRounds","review","computer","frontend","contextControl"};
    for(auto it=j.begin();it!=j.end();++it) if(!allowed.contains(it.key())) throw std::runtime_error("config_unknown_field");
    if(j.at("schemaVersion")!=1) throw std::runtime_error("config_version");
    Config c;c.contextControl=j.value("contextControl",Json::object());ContextControl::read(c.contextControl);c.frontend=j.value("frontend",c.frontend);if(!c.frontend.is_object())throw std::runtime_error("frontend_config");for(auto it=c.frontend.begin();it!=c.frontend.end();++it)if((it.key()!="startupAnimation"&&it.key()!="reducedMotion")||!it.value().is_boolean())throw std::runtime_error("frontend_config");const auto base=std::filesystem::absolute(path).parent_path();
    auto integer=[](const Json& object,const char* key,std::int64_t fallback,std::int64_t minimum,std::int64_t maximum){
        auto it=object.find(key);if(it==object.end())return fallback;if(!it->is_number_integer())throw std::runtime_error("config_integer");
        if(it->is_number_unsigned()&&it->get<std::uint64_t>()>static_cast<std::uint64_t>(maximum))throw std::runtime_error("config_range");
        const auto n=it->get<std::int64_t>();if(n<minimum||n>maximum)throw std::runtime_error("config_range");return n;
    };
    auto resolve=[&](const char* key){auto p=std::filesystem::u8path(j.at(key).get<std::string>());return std::filesystem::absolute(p.is_absolute()?p:base/p).lexically_normal();};
    c.dataRoot=resolve("dataRoot");c.characterRoot=resolve("characterRoot");c.uiRoot=resolve("uiRoot");
    c.configPath=std::filesystem::absolute(path);c.projectRoot=c.characterRoot.parent_path();
    auto optionalPath=[&](const char* key,const std::filesystem::path& fallback){if(!j.contains(key)||j.at(key)=="")return fallback;auto p=std::filesystem::u8path(j.at(key).get<std::string>());return std::filesystem::absolute(p.is_absolute()?p:base/p).lexically_normal();};
    c.pluginRoot=optionalPath("pluginRoot",c.projectRoot/"out/build/plugins");c.draftsRoot=optionalPath("draftsRoot",c.dataRoot/"drafts");c.materialsRoot=optionalPath("materialsRoot",c.dataRoot/"materials");
    c.pluginRootExplicit=j.contains("pluginRoot")&&j.at("pluginRoot")!="";
    c.plugins=j.value("plugins",Json::object());if(!c.plugins.is_object()||c.plugins.size()>32||c.plugins.dump().size()>16384)throw std::runtime_error("plugin_settings");
    auto environmentName=[](const std::string& value){if(value.empty())return true;if(value.size()>128||(!std::isalpha(static_cast<unsigned char>(value[0]))&&value[0]!='_'))return false;for(unsigned char x:value)if(!std::isalnum(x)&&x!='_')return false;std::string upper=value;std::transform(upper.begin(),upper.end(),upper.begin(),[](unsigned char x){return static_cast<char>(std::toupper(x));});return !std::set<std::string>{"PATH","HOME","TEMP","TMP","USERPROFILE","SYSTEMROOT","COMSPEC","CODEX_HOME"}.contains(upper);};
    for(const auto& plugin:c.plugins){if(!plugin.is_object())throw std::runtime_error("plugin_settings");for(auto it=plugin.begin();it!=plugin.end();++it){auto name=it.key();std::transform(name.begin(),name.end(),name.begin(),[](unsigned char x){return static_cast<char>(std::tolower(x));});if(name=="apikey"||name=="api_key"||name=="password"||name=="authorization"||name=="accesstoken"||name=="secret")throw std::runtime_error("plaintext_plugin_secret");}if(!environmentName(plugin.value("credentialEnvironment",std::string{})))throw std::runtime_error("credential_environment");}
    c.computer=j.value("computer",c.computer);if(!c.computer.is_object()||c.computer.dump().size()>16384)throw std::runtime_error("computer_config");
    for(auto it=c.computer.begin();it!=c.computer.end();++it)if(it.key()!="enabled"&&it.key()!="allowScreenCapture"&&it.key()!="applications"&&it.key()!="readRoots")throw std::runtime_error("computer_field");
    for(const auto* field:{"enabled","allowScreenCapture"})if(c.computer.contains(field)&&!c.computer.at(field).is_boolean())throw std::runtime_error("computer_boolean");
    auto identifier=[](const Json& value){if(!value.is_string())return false;const auto id=value.get<std::string>();return !id.empty()&&id.size()<=40&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos;};
    if(c.computer.contains("applications")){auto& apps=c.computer["applications"];if(!apps.is_array()||apps.size()>32)throw std::runtime_error("computer_applications");std::set<std::string> ids;for(auto& app:apps){if(!app.is_object()||!identifier(app.at("id"))||!ids.insert(app.at("id")).second||!app.at("name").is_string()||app.at("name").get<std::string>().size()>160)throw std::runtime_error("computer_application");for(auto it=app.begin();it!=app.end();++it)if(it.key()!="id"&&it.key()!="name"&&it.key()!="path"&&it.key()!="builtin")throw std::runtime_error("computer_application_field");if(app.contains("builtin")){if(app.contains("path")||(app.at("builtin")!="notepad"&&app.at("builtin")!="calculator"))throw std::runtime_error("computer_application_builtin");}else{const auto text=app.at("path").get<std::string>();if(text.empty()||text.size()>2048||text.find_first_of("\r\n")!=std::string::npos||text.find('\0')!=std::string::npos)throw std::runtime_error("computer_application_path");auto path=std::filesystem::u8path(text);if(!path.is_absolute()||path.extension()!=L".exe")throw std::runtime_error("computer_application_path");}}}
    auto& roots=c.computer["readRoots"];if(roots.is_null())roots=Json::array();if(!roots.is_array()||roots.size()>8)throw std::runtime_error("computer_roots");std::set<std::string> ids={"materials","drafts"};for(auto& root:roots){if(!root.is_object()||!identifier(root.at("id"))||!ids.insert(root.at("id")).second)throw std::runtime_error("computer_root");for(auto it=root.begin();it!=root.end();++it)if(it.key()!="id"&&it.key()!="path")throw std::runtime_error("computer_root_field");const auto text=root.at("path").get<std::string>();if(text.empty()||text.size()>2048||text.find_first_of("\r\n")!=std::string::npos||text.find('\0')!=std::string::npos)throw std::runtime_error("computer_root_path");auto path=std::filesystem::u8path(text);path=std::filesystem::absolute(path.is_absolute()?path:base/path).lexically_normal();auto native=path.u8string();root["path"]=std::string(reinterpret_cast<const char*>(native.data()),native.size());}
    c.remoteStorage=j.value("remoteStorage",c.remoteStorage);if(!c.remoteStorage.is_object()||c.remoteStorage.value("enabled",false)||c.remoteStorage.dump().size()>4096)throw std::runtime_error("remote_sync_deferred");
    for(auto it=c.remoteStorage.begin();it!=c.remoteStorage.end();++it)if(it.key()!="kind"&&it.key()!="url"&&it.key()!="enabled")throw std::runtime_error("remote_settings");
    const auto remoteKind=c.remoteStorage.value("kind",std::string("none"));if(remoteKind!="none"&&remoteKind!="github"&&remoteKind!="webdav")throw std::runtime_error("remote_kind");if(!c.remoteStorage.value("url",std::string{}).empty()&&!c.remoteStorage.at("url").is_string())throw std::runtime_error("remote_url");
    c.maxToolRounds=static_cast<std::size_t>(integer(j,"maxToolRounds",6,1,12));
    c.port=static_cast<int>(integer(j,"port",0,0,65535));c.credentialEnvironment=j.value("credentialEnvironment",c.credentialEnvironment);
    if(c.credentialEnvironment.empty()||!environmentName(c.credentialEnvironment))throw std::runtime_error("credential_environment");
    auto sameEnvironment=[](std::string a,std::string b){for(auto* s:{&a,&b})std::transform(s->begin(),s->end(),s->begin(),[](unsigned char x){return static_cast<char>(std::toupper(x));});return a==b;};
    for(const auto& plugin:c.plugins)if(sameEnvironment(plugin.value("credentialEnvironment",std::string{}),c.credentialEnvironment))throw std::runtime_error("credential_environment_collision");
    c.contextBytes=static_cast<std::size_t>(integer(j,"contextBytes",24000,4096,128*1024));c.recentMessageCount=static_cast<std::size_t>(integer(j,"recentMessageCount",12,0,32));c.maxMemoriesInContext=static_cast<std::size_t>(integer(j,"maxMemoriesInContext",16,0,64));
    c.reflectOnActivities=j.value("reflectOnActivities",false);c.solitudeIntervalMs=integer(j,"solitudeIntervalMs",900000,1000,7*86400000LL);c.idleBeforeSolitudeMs=integer(j,"idleBeforeSolitudeMs",120000,0,86400000);c.maxStartsPerHour=static_cast<int>(integer(j,"maxStartsPerHour",600,1,1000));
    if(c.port<0||c.port>65535||c.contextBytes<4096||c.contextBytes>128*1024||c.recentMessageCount>32||c.maxMemoriesInContext>64||c.solitudeIntervalMs<1000||c.solitudeIntervalMs>7*86400000LL||c.idleBeforeSolitudeMs<0||c.idleBeforeSolitudeMs>86400000||c.maxStartsPerHour<1||c.maxStartsPerHour>1000) throw std::runtime_error("config_limit");
    std::set<std::string> profileIds;
    for(const auto& p:j.at("profiles")) {
        const std::set<std::string> fields={"id","label","protocol","enabled","origin","basePath","model","credentialEnvironment","supportsJsonSchema","supportsJsonObject","supportsStreaming","supportsVision","supportsToolCalls","reasoningEffort","legacyMaxTokens","timeoutMs","maxOutputTokens","proxyOrigin","useSystemProxy"};
        for(auto it=p.begin();it!=p.end();++it) if(!fields.contains(it.key())) throw std::runtime_error("profile_unknown_field");
        Profile profile;profile.id=p.at("id").get<std::string>();profile.label=p.at("label").get<std::string>();profile.enabled=p.value("enabled",true);
        if(profile.id.empty()||profile.id=="plugin-tts"||profile.id.size()>64||!std::all_of(profile.id.begin(),profile.id.end(),[](unsigned char x){return std::isalnum(x)||x=='-'||x=='_';})||!profileIds.insert(profile.id).second||profile.label.size()>256) throw std::runtime_error("profile_id");
        const auto protocol=p.at("protocol").get<std::string>();
        if(protocol=="mock") profile.model.protocol=ayt::inference::Protocol::Mock;
        else if(protocol=="chat-completions") profile.model.protocol=ayt::inference::Protocol::ChatCompletions;
        else if(protocol=="responses") profile.model.protocol=ayt::inference::Protocol::Responses;
        else throw std::runtime_error("profile_protocol");
        profile.model.origin=p.value("origin","");profile.model.basePath=p.value("basePath","/v1");profile.model.model=p.value("model","");profile.model.credentialEnvironment=p.value("credentialEnvironment","");
        if(!environmentName(profile.model.credentialEnvironment))throw std::runtime_error("credential_environment");
        if(sameEnvironment(profile.model.credentialEnvironment,c.credentialEnvironment))throw std::runtime_error("credential_environment_collision");
        profile.model.supportsJsonSchema=p.value("supportsJsonSchema",false);profile.model.supportsJsonObject=p.value("supportsJsonObject",false);profile.model.supportsStreaming=p.value("supportsStreaming",true);profile.model.legacyMaxTokens=p.value("legacyMaxTokens",false);
        profile.model.supportsVision=p.value("supportsVision",false);
        profile.model.supportsToolCalls=p.value("supportsToolCalls",false);profile.model.reasoningEffort=p.value("reasoningEffort",std::string{});if(!std::set<std::string>{"","none","minimal","low","medium","high","xhigh","max"}.contains(profile.model.reasoningEffort))throw std::runtime_error("profile_reasoning_effort");
        profile.model.proxyOrigin=p.value("proxyOrigin",std::string{});profile.model.useSystemProxy=p.value("useSystemProxy",true);
        if(!profile.model.proxyOrigin.empty()&&(!profile.model.proxyOrigin.starts_with("http://")||!ayt::net::http::validOrigin(profile.model.proxyOrigin)))throw std::runtime_error("profile_proxy");
        const auto timeout=integer(p,"timeoutMs",30000,100,120000);profile.maxOutputTokens=static_cast<std::uint32_t>(integer(p,"maxOutputTokens",2048,1,8192));
        if(timeout<100||timeout>120000||!profile.maxOutputTokens||profile.maxOutputTokens>8192) throw std::runtime_error("profile_limit");
        profile.model.timeout=std::chrono::milliseconds(timeout);profile.model.maxRequestBytes=profile.model.supportsVision?2*1024*1024:512*1024;profile.model.maxResponseBytes=2*1024*1024;c.profiles.push_back(std::move(profile));
    }
    if(c.profiles.empty()||c.profiles.size()>16||!std::any_of(c.profiles.begin(),c.profiles.end(),[](const auto& p){return p.enabled&&p.model.protocol==ayt::inference::Protocol::Mock;})) throw std::runtime_error("mock_required");
    const auto review=j.value("review",Json::object());if(!review.is_object())throw std::runtime_error("review_config");
    for(auto it=review.begin();it!=review.end();++it)if(it.key()!="enabled"&&it.key()!="profileId"&&it.key()!="maxRewrites"&&it.key()!="retainConversationFocus")throw std::runtime_error("review_field");
    c.reviewEnabled=review.value("enabled",false);c.retainConversationFocus=review.value("retainConversationFocus",false);c.reviewProfileId=review.value("profileId",std::string{});c.maxReviewRewrites=static_cast<std::size_t>(integer(review,"maxRewrites",2,0,3));
    if(c.reviewEnabled&&std::none_of(c.profiles.begin(),c.profiles.end(),[&](const auto& p){return p.id==c.reviewProfileId&&p.enabled&&p.model.protocol!=ayt::inference::Protocol::Mock;}))throw std::runtime_error("review_profile");
    return c;
}
}
