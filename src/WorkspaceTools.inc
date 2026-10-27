    Json workspaceSearch(const Json& args,const std::function<bool()>& cancelled){
        const auto mount=args.at("mount").get<std::string>(),start=args.value("path",std::string{}),query=args.at("query").get<std::string>();
        if(!start.empty()&&!safeRelative(start))return toolFailure("workspace_path");
        std::vector<std::string> initial;std::string issue;if(!workspace.list("character",mount,start,initial,issue,128))return toolFailure("workspace_list_denied");
        struct Directory{std::string path;int depth;std::vector<std::string> names;};std::vector<Directory> pending={{start,0,std::move(initial)}};
        Json matches=Json::array();std::size_t visited=0,scanned=0,bytes=0,skipped=0;bool truncated=false;const auto limit=args.value("limit",std::size_t(12));
        for(std::size_t at=0;at<pending.size()&&!truncated;++at){
            const auto dir=pending[at];for(const auto& name:dir.names){
                if(cancelled&&cancelled())return toolFailure("cancelled");if(++visited>256||scanned>=64||bytes>=262144||matches.size()>=limit){truncated=true;break;}
                const auto path=dir.path.empty()?name:dir.path+"/"+name;std::string text;
                const bool readable=path.ends_with(".md")||path.ends_with(".txt")||path.ends_with(".json")||path.ends_with(".csv");
                if(readable&&workspace.read("character",mount,path,text,issue)){
                    ++scanned;if(text.size()>262144-bytes){truncated=true;break;}bytes+=text.size();if(!utf8WorkspaceText(text)){++skipped;continue;}
                    std::size_t offset=0;while((offset=text.find(query,offset))!=std::string::npos){
                        auto begin=offset>80?offset-80:0;while(begin&&((static_cast<unsigned char>(text[begin])&0xc0)==0x80))--begin;auto end=std::min(text.size(),offset+query.size()+160);while(end<text.size()&&end>begin&&((static_cast<unsigned char>(text[end])&0xc0)==0x80))--end;
                        matches.push_back({{"path",path},{"offset",offset},{"line",1+std::count(text.begin(),text.begin()+static_cast<std::ptrdiff_t>(offset),'\n')},{"excerpt",text.substr(begin,end-begin)},{"content_sha256",contentDigest(text)}});
                        offset+=query.size();if(matches.size()>=limit){truncated=true;break;}
                    }
                }else if(dir.depth<3){std::vector<std::string> names;if(workspace.list("character",mount,path,names,issue,128))pending.push_back({path,dir.depth+1,std::move(names)});else ++skipped;}
            }
        }
        return toolSuccess({{"mount",mount},{"query",query},{"matches",matches},{"files_scanned",scanned},{"bytes_scanned",bytes},{"skipped_entries",skipped},{"truncated",truncated}});
    }
    Json workspaceEdit(const Json& args){
        const auto path=args.at("path").get<std::string>();if(!safeRelative(path))return toolFailure("workspace_path");if(!(path.ends_with(".md")||path.ends_with(".txt")||path.ends_with(".json")))return toolFailure("workspace_text_only");
        std::string original,issue;if(!workspace.read("character","drafts",path,original,issue))return toolFailure("workspace_read_denied");
        if(!utf8WorkspaceText(original))return toolFailure("workspace_not_text");if(contentDigest(original)!=args.at("expected_sha256").get<std::string>())return toolFailure("workspace_revision_conflict");
        const auto old=args.at("old_text").get<std::string>(),replacement=args.at("new_text").get<std::string>();const auto found=original.find(old);
        if(found==std::string::npos)return toolFailure("workspace_edit_not_found");if(original.find(old,found+1)!=std::string::npos)return toolFailure("workspace_edit_ambiguous");
        auto edited=original;edited.replace(found,old.size(),replacement);if(edited.empty()||edited.size()>16384||!utf8WorkspaceText(edited))return toolFailure("workspace_edit_size_or_encoding");
        // Recheck immediately before the engine's atomic publication. As with
        // Workspace itself, this is a cooperative file boundary, not an OS CAS.
        std::string current;if(!workspace.read("character","drafts",path,current,issue)||current!=original)return toolFailure("workspace_revision_conflict");
        if(!workspace.write("character","drafts",path,edited,issue))return toolFailure("workspace_write_denied");
        return toolSuccess({{"path",path},{"written",true},{"replacements",1},{"content_sha256",contentDigest(edited)}});
    }
