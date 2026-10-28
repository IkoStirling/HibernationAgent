#pragma once
#include "PixelDocument.h"
#include <optional>
namespace hibernation::pixel {
// Saved presentation is a request to reopen a local draft, never pixel data or
// evidence of a new creation. The caller must load and match the actual revision.
inline PluginJson desktopRestoreRequest(const PluginJson& saved,const std::string& workspaceRoot,bool enabled,bool desktop,std::string& issue){
    const auto skip=[&](const char* reason)->PluginJson{issue=reason;return nullptr;};
    try{
        if(!enabled)return skip("pixel_restore_disabled");if(!desktop)return skip("pixel_restore_headless");
        if(!saved.is_object()||!saved.contains("workspace_root")||!saved.at("workspace_root").is_string()||workspaceRoot.empty()||saved.at("workspace_root")!=workspaceRoot)return skip("pixel_restore_workspace_mismatch");
        if(!saved.contains("surface")||!saved.at("surface").is_object())return skip("pixel_restore_metadata_invalid");const auto& surface=saved.at("surface");
        if(!surface.contains("visible")||!surface.at("visible").is_boolean()||surface.at("visible")!=true)return skip("pixel_restore_not_visible");
        if(!surface.contains("document_id")||!surface.at("document_id").is_string()||!validId(surface.at("document_id").get<std::string>())||!surface.contains("revision")||!surface.at("revision").is_number_integer()||surface.at("revision").get<std::int64_t>()<1||surface.at("revision").get<std::int64_t>()>1000000000)return skip("pixel_restore_metadata_invalid");
        if(!surface.contains("scale")||!surface.at("scale").is_number_integer()||surface.at("scale").get<std::int64_t>()<1||surface.at("scale").get<std::int64_t>()>8)return skip("pixel_restore_metadata_invalid");
        PluginJson request={{"id",surface.at("document_id")},{"revision",surface.at("revision")},{"scale",surface.at("scale")}};
        if(surface.contains("left")&&surface.contains("top")){
            const auto position=[](const PluginJson& number)->std::optional<std::int64_t>{if(!number.is_number_integer())return {};if(number.is_number_unsigned()){const auto value=number.get<std::uint64_t>();return value<=1000000?std::optional<std::int64_t>(static_cast<std::int64_t>(value)):std::nullopt;}const auto value=number.get<std::int64_t>();return value>=-1000000&&value<=1000000?std::optional<std::int64_t>(value):std::nullopt;};
            const auto left=position(surface.at("left")),top=position(surface.at("top"));if(left&&top){request["left"]=*left;request["top"]=*top;}
        }
        issue.clear();return request;
    }catch(...){return skip("pixel_restore_metadata_invalid");}
}
}
