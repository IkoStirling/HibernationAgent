#include <Hibernation/PluginRuntime.h>
#include <AYPlugin/IPluginLoader.h>
#include <AYSerializer/JsonSchema.h>
#include <fstream>
#include <mutex>
#include <atomic>
#include <map>
#include <set>
namespace hibernation {
namespace {
std::string utf8Path(const std::filesystem::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
PluginJson readManifest(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||in.tellg()>65536)throw std::runtime_error("plugin_manifest");std::string text(static_cast<size_t>(in.tellg()),'\0');in.seekg(0);in.read(text.data(),text.size());if(!ayt::serializer::validateJsonDocument(text,"{}"))throw std::runtime_error("plugin_manifest_json");return PluginJson::parse(text);}
}
struct PluginRuntime::Impl {
    struct Slot {std::unique_ptr<ayt::plugin::IPluginLoader> loader;IRolePlugin* plugin=nullptr;PluginJson manifest,configuration;};
    std::filesystem::path root;PluginHostContext host;mutable std::mutex mutex,eventMutex;
    std::map<std::string,std::shared_ptr<Slot>> loaded;PluginJson manifests=PluginJson::array(),saved=PluginJson::object();
    std::vector<PluginJson> events;std::atomic<unsigned> calls=0;bool stopped=false;
    std::string failureOwner;
    Impl(std::filesystem::path r,PluginHostContext h):root(std::move(r)),host(std::move(h)){
        host.emit=[this](PluginJson value){if(!value.is_object()||value.dump().size()>1024*1024)return;std::lock_guard guard(eventMutex);if(value.value("kind",std::string{})=="widget.position"){for(auto& e:events)if(e.value("kind",std::string{})=="widget.position"){e=std::move(value);return;}}if(events.size()<128)events.push_back(std::move(value));};
        if(std::filesystem::exists(root))for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_directory()){
            try{auto m=readManifest(entry.path()/"plugin.json");const auto id=m.at("id").get<std::string>(),file=m.at("library").get<std::string>();
                if(id.empty()||id.size()>64||entry.path().filename()!=std::filesystem::u8path(id)||file.find_first_of("/\\:")!=std::string::npos||m.at("api_version")!=2)continue;
                m["directory"]=utf8Path(entry.path());manifests.push_back(std::move(m));}catch(...){}
        }
    }
};
PluginRuntime::PluginRuntime(std::filesystem::path root,PluginHostContext host):impl_(std::make_unique<Impl>(std::move(root),std::move(host))){}
PluginRuntime::~PluginRuntime(){stop();}
void PluginRuntime::workspaceRoots(std::filesystem::path drafts,std::filesystem::path materials){std::lock_guard guard(impl_->mutex);impl_->host.draftsRoot=std::move(drafts);impl_->host.materialsRoot=std::move(materials);}
bool PluginRuntime::configure(const PluginJson& settings,std::string& issue){auto& a=*impl_;std::lock_guard guard(a.mutex);a.failureOwner.clear();if(a.calls||a.stopped){issue="plugin_busy";return false;}if(!settings.is_object()){issue="plugin_settings";return false;}
    std::set<std::string> known;for(const auto& m:a.manifests)known.insert(m.at("id"));for(auto it=settings.begin();it!=settings.end();++it)if(!it.value().is_object()||(!known.contains(it.key())&&it.value().value("enabled",false))){a.failureOwner=it.key();issue="plugin_unknown";return false;}
    auto dependencies=[&](const std::string& id,IRolePlugin* plugin){for(const auto* dependency:plugin->getDependencies()){if(!dependency||!known.contains(dependency)||!settings.value(dependency,PluginJson::object()).value("enabled",false)){a.failureOwner=id;issue=id+":plugin_dependency";return false;}}return true;};
    for(const auto& [id,slot]:a.loaded)if(settings.value(id,PluginJson::object()).value("enabled",false)&&!dependencies(id,slot->plugin))return false;
    // Stage newly loaded libraries. Existing plugins are removed only after all
    // additions and reconfiguration succeeded; rejected batches unload staging.
    std::map<std::string,std::shared_ptr<Impl::Slot>> staged;std::vector<std::string> rebound;
    auto rollback=[&]{for(auto it=rebound.rbegin();it!=rebound.rend();++it){std::string ignored;try{a.loaded.at(*it)->plugin->bind(a.host,a.loaded.at(*it)->configuration,ignored);}catch(...){}}};
    try{
        std::set<std::string> names;for(const auto& [id,existing]:a.loaded)if(settings.value(id,PluginJson::object()).value("enabled",false))for(const auto& t:existing->plugin->tools())names.insert(t.at("name"));
        for(const auto& m:a.manifests){const auto id=m.at("id").get<std::string>();const auto cfg=settings.value(id,PluginJson::object());if(!cfg.value("enabled",false)||a.loaded.contains(id))continue;
            a.failureOwner=id;
            auto slot=std::make_shared<Impl::Slot>();const auto path=std::filesystem::u8path(m.at("directory").get<std::string>())/std::filesystem::u8path(m.at("library").get<std::string>());
            slot->loader=ayt::plugin::IPluginLoader::create(utf8Path(path));if(!slot->loader||!slot->loader->load(utf8Path(path))){issue="plugin_load_failed";return false;}
            slot->plugin=dynamic_cast<IRolePlugin*>(slot->loader->getPlugin());
            if(!slot->plugin||slot->plugin->roleApiVersion()!=2||slot->plugin->getName()!=id){issue="plugin_contract";return false;}
            if(m.contains("dependencies")){
                if(!m.at("dependencies").is_array()||m.at("dependencies").size()>16){issue="plugin_dependency_manifest";return false;}
                std::set<std::string> declared,native;for(const auto& value:m.at("dependencies")){if(!value.is_string()||!known.contains(value.get<std::string>())||!declared.insert(value.get<std::string>()).second){issue="plugin_dependency_manifest";return false;}}
                for(const auto* value:slot->plugin->getDependencies()){if(!value){issue="plugin_dependency_manifest";return false;}native.insert(value);}
                if(declared!=native){issue="plugin_dependency_manifest";return false;}
            }
            if(!dependencies(id,slot->plugin))return false;
            if(!slot->plugin->bind(a.host,cfg,issue)){issue=id+":"+issue;return false;}
            const auto tools=slot->plugin->tools();if(!tools.is_array()||tools.size()>32){issue="plugin_tools";return false;}
            for(const auto& t:tools){if(!t.contains("name")||!t.contains("parameters")||!ayt::serializer::validateJsonSchema(t.at("parameters").dump())){issue="plugin_tools";return false;}const auto name=t.at("name").get<std::string>();const auto dot=name.find('.');if(dot==std::string::npos||name.size()>96||name.starts_with("self.")||name.starts_with("body.")||name.starts_with("activity.")||name.starts_with("computer.")||name.starts_with("capabilities.")||name.starts_with("reflection.")||name.starts_with("memory.")||name.starts_with("state.")||name.starts_with("stage.")||name.starts_with("schedule.")||name.starts_with("workspace.")||!names.insert(name).second){issue="plugin_tool_collision";return false;}}
            slot->manifest=m;slot->configuration=cfg;if(a.saved.contains(id)&&!slot->plugin->restore(a.saved.at(id))){issue="plugin_restore";return false;}staged.emplace(id,std::move(slot));
        }
        for(const auto& [id,slot]:a.loaded){const auto cfg=settings.value(id,PluginJson::object());if(!cfg.value("enabled",false))continue;a.failureOwner=id;rebound.push_back(id);if(!slot->plugin->bind(a.host,cfg,issue)){rollback();return false;}}
    }catch(...){rollback();issue="plugin_configuration_exception";return false;}
    for(const auto& id:rebound)a.loaded.at(id)->configuration=settings.at(id);
    for(auto& [id,slot]:staged)a.loaded.emplace(id,std::move(slot));
    for(auto it=a.loaded.begin();it!=a.loaded.end();)if(!settings.value(it->first,PluginJson::object()).value("enabled",false)){a.saved[it->first]=it->second->plugin->snapshot();it->second->loader->unload();it=a.loaded.erase(it);}else ++it;
    issue.clear();a.failureOwner.clear();return true;
}
bool PluginRuntime::configureStartup(const PluginJson& settings,PluginJson& failures,std::string& issue){
    auto& a=*impl_;{std::lock_guard guard(a.mutex);if(!a.loaded.empty()||a.stopped){issue="plugin_startup_only";return false;}}
    failures=PluginJson::object();auto candidate=settings;
    for(std::size_t attempt=0;attempt<=settings.size();++attempt){
        if(configure(candidate,issue))return true;
        const auto owner=a.failureOwner;if(owner.empty()||!candidate.contains(owner))return false;
        failures[owner]=issue;candidate[owner]={{"enabled",false}};
        // Nothing from rejected staged instances is a committed observation.
        (void)takeEvents();
        bool changed=true;while(changed){changed=false;for(const auto& manifest:a.manifests){
            const auto id=manifest.at("id").get<std::string>();if(!candidate.value(id,PluginJson::object()).value("enabled",false))continue;
            const auto dependencies=manifest.value("dependencies",PluginJson::array());if(!dependencies.is_array())continue;
            for(const auto& dependency:dependencies)if(dependency.is_string()&&!candidate.value(dependency.get<std::string>(),PluginJson::object()).value("enabled",false)){
                candidate[id]={{"enabled",false}};failures[id]="dependency_unavailable:"+dependency.get<std::string>();changed=true;break;
            }
        }}
    }issue="plugin_startup_limit";return false;
}
PluginJson PluginRuntime::catalog()const{auto& a=*impl_;std::lock_guard guard(a.mutex);PluginJson result=PluginJson::array();for(const auto& [id,slot]:a.loaded)for(auto tool:slot->plugin->tools()){tool["plugin"]=id;result.push_back(std::move(tool));}return result;}
PluginJson PluginRuntime::status()const{auto& a=*impl_;std::lock_guard guard(a.mutex);auto result=a.manifests;for(auto& m:result){const auto id=m.at("id").get<std::string>();m.erase("directory");m["enabled"]=a.loaded.contains(id);}return result;}
PluginJson PluginRuntime::invoke(const std::string& name,const PluginJson& args,const std::function<bool()>& cancelled){auto& a=*impl_;std::shared_ptr<Impl::Slot> selected;
    {std::lock_guard guard(a.mutex);if(a.stopped)return toolFailure("plugin_stopped");for(const auto& [id,slot]:a.loaded)for(const auto& t:slot->plugin->tools())if(t.at("name")==name){if(!ayt::serializer::validateJsonDocument(args.dump(),t.at("parameters").dump()))return toolFailure("invalid_tool_arguments");selected=slot;break;}if(!selected)return toolFailure("tool_not_available");++a.calls;}
    struct Lease{std::atomic<unsigned>& calls;~Lease(){--calls;}}lease{a.calls};
    if(cancelled&&cancelled())return toolFailure("cancelled");try{auto result=selected->plugin->invoke(name,args,cancelled);if(result.dump().size()>1024*1024)return toolFailure("tool_result_limit");return result;}catch(...){return toolFailure("plugin_execution_failed");}
}
void PluginRuntime::tick(double dt){auto& a=*impl_;std::lock_guard guard(a.mutex);for(auto& [id,s]:a.loaded)try{s->plugin->tick(dt);}catch(...){a.host.emit({{"kind","plugin.error"},{"plugin",id}});}}
void PluginRuntime::event(const PluginJson& value){auto& a=*impl_;std::lock_guard guard(a.mutex);for(auto& [id,s]:a.loaded)try{s->plugin->event(value);}catch(...){}}
std::vector<PluginJson> PluginRuntime::takeEvents(){auto& a=*impl_;std::lock_guard guard(a.eventMutex);auto result=std::move(a.events);a.events.clear();return result;}
PluginJson PluginRuntime::snapshots()const{auto& a=*impl_;std::lock_guard guard(a.mutex);auto result=a.saved;for(const auto& [id,s]:a.loaded)result[id]=s->plugin->snapshot();return result;}
PluginJson PluginRuntime::liveSnapshots()const{auto& a=*impl_;std::lock_guard guard(a.mutex);auto result=PluginJson::object();for(const auto& [id,s]:a.loaded)result[id]=s->plugin->snapshot();return result;}
bool PluginRuntime::enabled(const std::string& id)const{auto& a=*impl_;std::lock_guard guard(a.mutex);return a.loaded.contains(id);}
void PluginRuntime::restore(const PluginJson& value){auto& a=*impl_;std::lock_guard guard(a.mutex);if(!value.is_object())return;a.saved=value;for(auto& [id,s]:a.loaded)if(value.contains(id))s->plugin->restore(value.at(id));}
void PluginRuntime::stop(){auto& a=*impl_;{std::lock_guard guard(a.mutex);a.stopped=true;} // Host stops/joins the model worker before reaching this boundary.
    if(a.calls)return;std::lock_guard guard(a.mutex);for(auto& [id,s]:a.loaded)s->loader->unload();a.loaded.clear();
}
}
