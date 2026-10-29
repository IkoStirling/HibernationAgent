#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace hibernation {
// Concrete native outcomes only. Queue acceptance and empty/failed searches
// never become an experienced collection; a batch produces one episode.
inline std::optional<std::string> suppliesExperience(const nlohmann::json& event){
    using Json=nlohmann::json;try{
        const auto plugin=event.value("plugin",std::string{}),kind=event.value("kind",std::string{});
        if(plugin=="habitat"&&kind=="habitat.collected"&&!event.contains("gather_id")){
            const auto& item=event.at("item");if(!item.at("id").is_string())return std::nullopt;
            return "角色把桌面上的一件食材或食物收进了背包，保留了原来的来源和新鲜期限。";
        }
        if(plugin=="habitat"&&kind=="habitat.gather_finished"){
            const auto& g=event.at("gather");const int n=g.at("collected");const auto& results=g.at("results");
            if(n<=0||n>8||!results.is_array()||results.size()!=static_cast<std::size_t>(n))return std::nullopt;
            const auto destination=g.at("destination").get<std::string>();if(destination!="backpack"&&destination!="pantry")return std::nullopt;
            return "角色这次收集了"+std::to_string(n)+"份桌面食材，放进了"+(destination=="pantry"?"食堂仓库。":"背包。")+(g.value("completed",false)?"":"原先计划的数量没有收集齐。");
        }
        if(plugin=="canteen"&&kind=="canteen.inventory_consumed"&&event.value("completed",false)&&event.at("item_id").is_string())
            return "角色从自己的库存中吃掉了一份可食用的虚拟食物。";
    }catch(const Json::exception&){}return std::nullopt;
}
inline bool completedSuppliesExperience(const nlohmann::json& event){
    if(!suppliesExperience(event))return false;
    return event.value("kind",std::string{})!="habitat.gather_finished"||event.at("gather").value("completed",false);
}
}
