// App-owned presentation index. It neither grants character memory access nor
// changes the recent context index. Each archived message has one stable link.
void ensureChatHistory(bool reconcile=false){
    const auto head=get("ha.chat-index","head");if(head.revision&&!reconcile)return;
    std::vector<RecordMutation> updates;std::string previous=head.value.value("last",std::string{});std::size_t count=head.value.value("count",std::size_t(0));
    for(const auto& row:readIds(archiveScope,get("ha.index","recent",Json::array()).value)){
        const auto value=Json::parse(row.payload);const auto kind=value.value("kind",std::string{});
        if((kind!="user_message"&&kind!="assistant_message")||!desktopDialogueRecord(value)||get("ha.chat-links",row.id).revision)continue;
        updates.push_back({"ha.chat-links",row.id,0,1,Json{{"previous",previous}}.dump(),"legacy-chat-index"});previous=row.id;++count;
    }
    if(updates.empty()&&head.revision)return;updates.push_back({"ha.chat-index","head",head.revision,1,Json{{"last",previous},{"count",count},{"legacy_window",true}}.dump(),"legacy-chat-index"});commit(std::move(updates),"chat-index-initialize");
}
void appendChatHistoryBatch(std::vector<RecordMutation>& updates,const std::vector<std::string>& messages){
    if(messages.empty())return;
    ensureChatHistory();const auto head=get("ha.chat-index","head");
    auto previous=head.value.value("last",std::string{});auto count=head.value.value("count",std::size_t(0));
    for(const auto& message:messages){updates.push_back({"ha.chat-links",message,0,1,Json{{"previous",previous}}.dump(),message});previous=message;++count;}
    updates.push_back(mutation("ha.chat-index","head",head,Json{{"last",previous},{"count",count},{"legacy_window",true}},messages.front()));
}
void appendChatHistory(std::vector<RecordMutation>& updates,const std::string& message){appendChatHistoryBatch(updates,{message});}
Json chatMessage(const std::string& key,const Json& data){
    const bool user=data.value("kind",std::string{})=="user_message";Json message={{"id",key},{"role",user?"user":"assistant"},{"text",data.value("text",std::string{})},{"created_at",data.value("created_at",std::int64_t(0))},{"attachments",data.value("attachments",Json::array())}};
    if(!user){const auto it=speechPresentation.find(key);if(it!=speechPresentation.end())message["playback"]=it->second;else if(!historicalMessages.contains(key)&&automaticSpeech(data.value("presentation",Json::object())))message["playback"]={{"state","waiting"},{"visible_characters",0}};}
    for(const auto* field:{"reply_bundle_id","message_index","message_count"})if(data.contains(field))message[field]=data.at(field);
    return message;
}
LocalReply chatPage(const Json& body){
    if(!body.is_object()||body.size()>2||(body.contains("limit")&&(!body["limit"].is_number_integer()||body["limit"].get<double>()<1||body["limit"].get<double>()>48))||(body.contains("before")&&(!body["before"].is_string()||body["before"].get<std::string>().size()>80)))return error(400,"chat_page_arguments");
    for(const auto& [key,value]:body.items())if(key!="limit"&&key!="before")return error(400,"chat_page_arguments");
    ensureChatHistory();const auto head=get("ha.chat-index","head");auto cursor=body.value("before",std::string{});
    if(cursor.empty())cursor=head.value.value("last",std::string{});
    else{const auto link=get("ha.chat-links",cursor);if(!link.revision)return error(404,"chat_cursor_missing");cursor=link.value.value("previous",std::string{});}
    Json values=Json::array();const auto limit=body.value("limit",24);for(int i=0;i<limit&&!cursor.empty();++i){const auto row=get(archiveScope,cursor);const auto link=get("ha.chat-links",cursor);if(!row.revision||!link.revision)return error(503,"chat_index_incomplete");values.push_back(chatMessage(cursor,row.value));cursor=link.value.value("previous",std::string{});}
    std::reverse(values.begin(),values.end());return reply(200,{{"messages",values},{"has_more",!cursor.empty()},{"next_before",values.empty()?std::string{}:values.front().at("id").get<std::string>()},{"latest_id",head.value.value("last",std::string{})},{"total",head.value.value("count",std::size_t(0))}});
}
LocalReply importChatMedia(const std::filesystem::path& path){
    const auto extension=path.extension().u8string();const auto mime=chatMediaMime({reinterpret_cast<const char*>(extension.data()),extension.size()});if(mime.empty())return error(400,"chat_media_type");
    std::error_code issue;if(!std::filesystem::is_regular_file(path,issue)||issue)return error(400,"chat_media_file");
    const auto size=std::filesystem::file_size(path,issue);const auto maximum=mime.starts_with("image/")?8*1024*1024:64*1024*1024;if(issue||!size||size>maximum)return error(400,"chat_media_size");
    const auto bytes=readFile(path,maximum);if(bytes.size()!=size||!chatMediaSignature(mime,bytes))return error(400,"chat_media_format");
    Json thumbnail=nullptr;if(mime.starts_with("image/")){const auto prepared=prepareImageObservation(bytes);if(!prepared.value("ok",false))return error(400,"chat_media_decode");thumbnail=prepared.at("result").at("image");}
    const auto key=id("media");const auto filename=path.filename().u8string();Json metadata={{"id",key},{"name",std::string(reinterpret_cast<const char*>(filename.data()),filename.size()).substr(0,240)},{"mime_type",mime},{"bytes",size},{"chunks",(size+384*1024-1)/(384*1024)},{"thumbnail",thumbnail},{"created_at",serviceNowMs()}};
    std::vector<RecordMutation> updates;for(std::size_t offset=0,part=0;offset<bytes.size();offset+=384*1024,++part){const auto count=std::min<std::size_t>(384*1024,bytes.size()-offset);std::vector<std::uint8_t> buffer(bytes.begin()+offset,bytes.begin()+offset+count);updates.push_back({"ha.chat-media",key+"."+std::to_string(part),0,1,Json{{"base64",imageBase64(buffer)}}.dump(),key});}
    updates.push_back({"ha.chat-media",key,0,1,metadata.dump(),"user-selected-file"});commit(std::move(updates),key);metadata.erase("thumbnail");return reply(201,metadata);
}
LocalReply chatMedia(const Json& body){
    if(!body.is_object()||!body.contains("message_id")||!body["message_id"].is_string()||!body.contains("id")||!body["id"].is_string())return error(400,"chat_media_arguments");
    const auto messageId=body.at("message_id").get<std::string>(),key=body.at("id").get<std::string>();if(!validRequestId(messageId)||!validRequestId(key))return error(400,"chat_media_arguments");
    const auto message=get(archiveScope,messageId);if(!get("ha.chat-links",messageId).revision||!message.revision)return error(404,"chat_media_missing");const auto attachments=message.value.value("attachments",Json::array());if(std::none_of(attachments.begin(),attachments.end(),[&](const auto& attachment){return attachment.value("id",std::string{})==key;}))return error(404,"chat_media_missing");
    const auto media=get("ha.chat-media",key);if(!media.revision)return error(404,"chat_media_missing");
    if(!body.contains("part"))return reply(200,media.value);
    if(!body["part"].is_number_integer()||body["part"].get<int>()<0||body["part"].get<int>()>=media.value.at("chunks").get<int>())return error(400,"chat_media_part");return reply(200,get("ha.chat-media",key+"."+std::to_string(body["part"].get<int>())).value);
}
