#pragma once
#include "Protocol.h"
#include <Hibernation/Vault.h>
namespace hibernation::napcat {
class Ledger {
    ayt::storage::RecordStore store_;mutable std::mutex mutex_;
    struct Doc{PluginJson value;std::int64_t revision=0;};
    Doc read(const std::string& id)const{auto rows=store_.query({{"napcat"}},{"napcat",id});if(!rows)throw std::runtime_error("napcat_ledger_read");if(rows.records.empty())return {PluginJson::object(),0};return {PluginJson::parse(rows.records.front().payload),rows.records.front().revision};}
    static ayt::storage::RecordMutation put(const std::string& id,const Doc& old,const PluginJson& value){return {"napcat",id,old.revision,1,value.dump(),"napcat"};}
    void commit(std::vector<ayt::storage::RecordMutation> changes){if(!store_.commit(changes,"napcat-transport",nowMs()))throw std::runtime_error("napcat_ledger_write");}
public:
    PluginJson incomingImage(const std::string& id)const{std::lock_guard lock(mutex_);return read("input-image:"+id).value;}
    void cacheIncomingImage(const std::string& id,const PluginJson& image){std::lock_guard lock(mutex_);const auto key="input-image:"+id;auto old=read(key);commit({put(key,old,image)});}
    PluginJson recentImages(const PluginJson& row)const{
        std::lock_guard lock(mutex_);const auto all=read("input-images:"+digest(conversationScope(row))).value.value("items",PluginJson::array());PluginJson result=PluginJson::array();
        const auto until=row.value("input_last_ms",row.at("created_ms").get<std::int64_t>());
        for(auto it=all.rbegin();it!=all.rend()&&result.size()<8;++it){const auto time=it->at("received_ms").get<std::int64_t>();if(time<=until&&time>=until-300000)result.push_back(*it);}return result;
    }
    // Freeze a bounded same-sender burst once. Original rows stay immutable for
    // callback dedup, and recovery reuses exactly this persisted batch identity.
    PluginJson batch(const std::string& id,int windowMs){
        std::lock_guard lock(mutex_);auto root=read(id);if(root.value.contains("input_text")||root.value.value("status",std::string{})!="queued")return root.value;
        auto index=read("index");std::vector<ayt::storage::RecordMutation> changes;auto& active=index.value["active"];const auto scope=conversationScope(root.value);
        std::string text=root.value.at("text");PluginJson members=PluginJson::array({root.value.at("message_id")});auto last=root.value.at("created_ms").get<std::int64_t>();
        for(auto it=active.begin();it!=active.end();){if(*it==id){++it;continue;}auto child=read(*it);const auto stamp=child.value.at("created_ms").get<std::int64_t>();
            if(windowMs<=0||conversationScope(child.value)!=scope||child.value.value("status",std::string{})!="queued"||stamp<root.value.at("created_ms").get<std::int64_t>()||stamp>root.value.at("created_ms").get<std::int64_t>()+windowMs||members.size()>=8||text.size()+1+child.value.at("text").get_ref<const std::string&>().size()>12000){++it;continue;}
            text+="\n"+child.value.at("text").get<std::string>();members.push_back(child.value.at("message_id"));last=std::max(last,stamp);child.value["status"]="batched";child.value["batch_parent"]=id;changes.push_back(put(*it,child,child.value));it=active.erase(it);
        }
        root.value["input_text"]=text;root.value["input_message_ids"]=members;root.value["input_last_ms"]=last;changes.push_back(put(id,root,root.value));changes.push_back(put("index",index,index.value));commit(std::move(changes));return root.value;
    }
    PluginJson media(const std::string& key)const{std::lock_guard lock(mutex_);return read("media:"+key).value;}
    bool startMedia(const std::string& key,const std::string& request,const std::string& source){std::lock_guard lock(mutex_);const auto id="media:"+key;const auto old=read(id);if(old.revision)return false;commit({put(id,old,{{"status","sending"},{"request_id",request},{"source_id",source}})});return true;}
    void finishMedia(const std::string& key,const PluginJson& result){std::lock_guard lock(mutex_);const auto id="media:"+key;auto old=read(id);auto value=old.value;value["status"]="completed";value["output"]=result;commit({put(id,old,value)});}
    std::string classify(const PluginJson& row)const{std::lock_guard lock(mutex_);auto before=read(row.at("id"));if(!before.revision)return "new";for(const auto* key:{"sender_id","group_id","type","text"})if(before.value.at(key)!=row.at(key))return "identity_conflict";if(before.value.contains("content_signature")&&before.value.at("content_signature")!=row.value("content_signature",std::string{}))return "identity_conflict";return "duplicate";}
    explicit Ledger(const std::filesystem::path& root){std::filesystem::create_directories(root/"runtime");std::filesystem::create_directories(root/"archive");if(!store_.open(root/"archive/records.sqlite",{openVaultCodec(root),false,true}))throw std::runtime_error("napcat_ledger_open");
        auto index=read("index");if(!index.revision)commit({put("index",index,{{"active",PluginJson::array()},{"conversations",PluginJson::array()},{"sent",0},{"failed",0},{"unknown",0},{"received",0}})});
        // A crash after entering send cannot prove whether QQ received it.
        // Preserve the outbox; never automatically duplicate an unknown send.
        const auto active=read("index").value.at("active");for(const auto& id:active){auto row=read(id);if(row.value.value("status",std::string{})=="sending")finish(id,"unknown");}
    }
    std::string enqueue(PluginJson row,int limit){std::lock_guard lock(mutex_);const auto id=row.at("id").get<std::string>();const auto before=read(id);if(before.revision){for(const auto* key:{"sender_id","group_id","type","text"})if(before.value.at(key)!=row.at(key))return "identity_conflict";if(before.value.contains("content_signature")&&before.value.at("content_signature")!=row.value("content_signature",std::string{}))return "identity_conflict";return "duplicate";}
        auto index=read("index");if(index.value.at("active").size()>=static_cast<size_t>(limit))return "queue_full";
        const auto scope=row.at("type").get<std::string>()+":"+row.at("group_id").get<std::string>()+":"+row.at("sender_id").get<std::string>();auto& scopes=index.value["conversations"];
        if(std::find(scopes.begin(),scopes.end(),scope)==scopes.end()){if(scopes.size()>=256)return "conversation_limit";scopes.push_back(scope);}
        index.value["active"].push_back(id);index.value["received"]=index.value.value("received",0)+1;std::vector<ayt::storage::RecordMutation> changes={put(id,before,row),put("index",index,index.value)};
        if(row.contains("attachments")&&!row.at("attachments").empty()){const auto key="input-images:"+digest(scope);auto old=read(key);auto items=old.value.value("items",PluginJson::array());for(const auto& image:row.at("attachments"))items.push_back(image);while(items.size()>32)items.erase(items.begin());changes.push_back(put(key,old,{{"items",items}}));}
        commit(std::move(changes));return "queued";
    }
    std::optional<PluginJson> next(){std::lock_guard lock(mutex_);const auto index=read("index");for(const auto& id:index.value.at("active")){const auto row=read(id);if(row.value.value("next_ms",std::int64_t(0))<=nowMs())return std::optional<PluginJson>{std::in_place,row.value};}return {};}
    void update(const std::string& id,const PluginJson& fields){std::lock_guard lock(mutex_);auto old=read(id);for(auto it=fields.begin();it!=fields.end();++it)old.value[it.key()]=it.value();commit({put(id,old,old.value)});}
    void finish(const std::string& id,const std::string& state){std::lock_guard lock(mutex_);auto old=read(id);old.value["status"]=state;old.value["finished_ms"]=nowMs();auto index=read("index");auto& active=index.value["active"];for(auto it=active.begin();it!=active.end();)if(*it==id)it=active.erase(it);else ++it;if(state=="sent"||state=="failed"||state=="unknown")index.value[state]=index.value.value(state,0)+1;commit({put(id,old,old.value),put("index",index,index.value)});}
    void discardOffline(const std::string& reason={}){std::lock_guard lock(mutex_);auto index=read("index");std::vector<ayt::storage::RecordMutation> changes;
        for(const auto& id:index.value.at("active")){auto old=read(id);auto value=old.value;value["status"]=value.value("status",std::string{})=="sending"?"unknown":"offline";value["finished_ms"]=nowMs();if(!reason.empty())value["offline_reason"]=reason;changes.push_back(put(id,old,value));}
        if(changes.empty())return;index.value["active"]=PluginJson::array();changes.push_back(put("index",index,index.value));commit(std::move(changes));
    }
    PluginJson status()const{std::lock_guard lock(mutex_);auto value=read("index").value;value["queued"]=value.at("active").size();value["conversation_count"]=value.at("conversations").size();value.erase("active");value.erase("conversations");return value;}
};
}
