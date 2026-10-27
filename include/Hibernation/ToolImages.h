#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace hibernation {
struct ToolImagePart {std::string uri;int frame=-1;};
inline bool boundedImageBase64(const std::string& bytes){
    if(bytes.empty()||bytes.size()>2*1024*1024||bytes.size()%4||bytes.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")!=std::string::npos)return false;
    const auto padding=bytes.find('=');return padding==std::string::npos||(bytes.size()-padding<=2&&bytes.find_first_not_of('=',padding)==std::string::npos);
}
// Only these declared image slots carry pixels. Remove every slot, including
// rejected ones, before a receipt becomes model text. Archives retain originals.
inline std::vector<ToolImagePart> detachToolImages(nlohmann::json& result,bool vision,std::size_t capacity,std::size_t firstPart=0){
    using Json=nlohmann::json;std::vector<ToolImagePart> parts;
    const auto detach=[&](Json& holder,int index){
        if(!holder.is_object()||!holder.contains("image"))return;
        auto image=std::move(holder["image"]);holder.erase("image");
        const auto mime=image.is_object()&&image.contains("mime_type")&&image.at("mime_type").is_string()?image.at("mime_type").get<std::string>():std::string{};
        const auto bytes=image.is_object()&&image.contains("base64")&&image.at("base64").is_string()?image.at("base64").get<std::string>():std::string{};
        const bool valid=(mime=="image/jpeg"||mime=="image/png"||mime=="image/webp")&&boundedImageBase64(bytes);
        const bool attached=vision&&valid&&parts.size()<capacity;holder["image_attached"]=attached;holder["vision_available"]=vision;
        if(attached){holder["image_part_index"]=firstPart+parts.size();parts.push_back({"data:"+mime+";base64,"+bytes,index});}
        else holder["image_unavailable_reason"]=!vision?"vision_not_enabled":!valid?"invalid_tool_image":"per_batch_image_limit";
    };
    detach(result,-1);
    if(result.is_object()&&result.contains("frames")&&result.at("frames").is_array())for(std::size_t i=0;i<result.at("frames").size();++i)detach(result["frames"][i],static_cast<int>(i));
    return parts;
}
}
