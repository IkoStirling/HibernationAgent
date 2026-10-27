#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
namespace hibernation {
// Numerical spelling is not a new operation: x=1 and x=1.0 have the same
// meaning. Preserve large integral JSON values without a lossy double roundtrip.
inline bool canonicalOperationNumbers(nlohmann::json& value,unsigned depth=0){
    if(depth>64)return false;
    if(value.is_object()||value.is_array()){for(auto& child:value)if(!canonicalOperationNumbers(child,depth+1))return false;}
    else if(value.is_number_float()){
        const auto number=value.get<double>();if(!std::isfinite(number))return false;
        if(number>=-9007199254740991.0&&number<=9007199254740991.0&&std::floor(number)==number)value=static_cast<std::int64_t>(number);
    }
    return true;
}
}
