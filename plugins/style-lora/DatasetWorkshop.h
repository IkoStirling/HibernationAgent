#pragma once
#include <AYInference/Inference.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace hibernation::expression {
using Json=nlohmann::json;
using ModelCall=std::function<ayt::inference::Result(const ayt::inference::ModelProfile&,const ayt::inference::Request&)>;
/// Operator-only candidate authoring. No archive-memory reads, training or tools.
class DatasetWorkshop {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit DatasetWorkshop(std::filesystem::path root,ModelCall call={});
    ~DatasetWorkshop();
    Json invoke(const std::string& action,const Json& args);
    Json summary()const;
    void cancel();
    void stop();
};
std::string datasetDigest(const Json& value);
}
