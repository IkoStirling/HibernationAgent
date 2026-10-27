#pragma once
#include <map>
#include <string>
namespace hibernation::napcat {
// QQ face identifiers/names from NapCatQQ face_config.json (2026-10-27).
// https://github.com/NapNeko/NapCatQQ/blob/main/packages/napcat-core/external/face_config.json
inline std::string qqFaceText(const std::string& id){
    static const std::map<std::string,std::string> names={
        {"0","惊讶"},
        {"1","撇嘴"},
        {"2","色"},
        {"3","发呆"},
        {"4","得意"},
        {"5","流泪"},
        {"6","害羞"},
        {"7","闭嘴"},
        {"8","睡"},
        {"9","大哭"},
        {"10","尴尬"},
        {"11","发怒"},
        {"12","调皮"},
        {"13","呲牙"},
        {"14","微笑"},
        {"15","难过"},
        {"16","酷"},
        {"18","抓狂"},
        {"19","吐"},
        {"20","偷笑"},
        {"21","可爱"},
        {"22","白眼"},
        {"23","傲慢"},
        {"24","饥饿"},
        {"25","困"},
        {"26","惊恐"},
        {"27","流汗"},
        {"28","憨笑"},
        {"29","悠闲"},
        {"30","奋斗"},
        {"76","赞"},
        {"96","冷汗"},
        {"97","擦汗"},
        {"98","抠鼻"},
        {"99","鼓掌"},
        {"100","糗大了"},
        {"101","坏笑"},
        {"102","左哼哼"},
        {"103","右哼哼"},
        {"104","哈欠"},
        {"105","鄙视"},
        {"106","委屈"},
        {"107","快哭了"},
        {"109","左亲亲"},
        {"111","可怜"},
        {"114","篮球"},
        {"182","笑哭"},
        {"212","托腮"},
        {"240","喷脸"},
        {"264","捂脸"},
        {"277","汪汪"},
    };
    const auto found=names.find(id);return "[QQ表情 id="+id+(found==names.end()?std::string{}:" 名称="+found->second)+"]";
}
}
