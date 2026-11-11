void mediaLibraryTests(const std::filesystem::path& root){
    std::filesystem::create_directories(root);const std::string gif="GIF89a"+std::string(40,'g');
    {std::ofstream f(root/"sleep.gif",std::ios::binary);f<<gif;}
    Json catalog={{"version",1},{"items",Json::array({{{"id","sleep"},{"file","sleep.gif"},{"name","困了"},{"meaning","想休息、睡觉的表情"},{"tags",Json::array({"困","睡觉"})},{"situations",Json::array({"晚安"})},{"sticker",true}}})}};
    const auto write=[&]{std::ofstream f(root/"catalog.json",std::ios::binary);f<<catalog.dump();};write();MediaLibrary library(root.generic_string());
    const auto found=library.search("我有点困",6);check(found.at("items").size()==1&&found.at("items")[0].at("id")=="sleep","semantic tags find curated sticker without reading pixels");check(found.dump().find("sleep.gif")==std::string::npos&&found.at("image_delivered_to_model")==false,"search never reveals paths or image payloads");
    const auto image=library.image("sleep");check(image.at("image").at("file")=="base64://"+mediaBase64(gif)&&image.at("image").at("sub_type")==1,"GIF bytes retained and sticker presentation requested");
    check(mediaBase64("f")=="Zg=="&&mediaBase64("fo")=="Zm8="&&mediaBase64("foo")=="Zm9v","binary encoding has correct padding");
    const auto collection=root/"collected";MediaLibrary::initializeCollection(collection);MediaLibrary personal(collection.generic_string());
    const auto saved=personal.collect(gif,{{"name","自己的困困表情"},{"meaning","困了，想休息"},{"tags",Json::array({"困"})},{"situations",Json::array({"晚安"})},{"sticker",true}});
    check(saved.at("saved")==true&&!saved.at("duplicate").get<bool>(),"received original can be collected");
    const auto duplicate=personal.collect(gif,{{"name","重复"},{"meaning","重复"}});check(duplicate.at("duplicate")==true&&duplicate.at("id")==saved.at("id"),"content addressed collection consumes one slot per original");
    MediaLibrary reopened(collection.generic_string());check(reopened.search("晚安").at("items").size()==1&&reopened.image(saved.at("id")).at("image").at("file")=="base64://"+mediaBase64(gif),"saved meanings and unchanged GIF survive reopen");
    {Ledger limits(root/"limits");check(limits.startMedia("one","turn","library:first"),"first curated send reserves its attempt");check(!limits.startMedia("one","turn","library:first"),"duplicate send does not reserve twice");check(limits.startMedia("two","turn","library:second")&&!limits.startMedia("three","turn","library:third"),"at most two library sends per input even before receipts");check(limits.startMedia("four","next-turn","library:first"),"new user input has a separate bounded allowance");}
    bool blocked=false;try{library.image("not-found");}catch(...){blocked=true;}check(blocked,"unknown library id fails closed");
    catalog["items"][0]["file"]="../escape.gif";write();blocked=false;try{MediaLibrary bad(root.generic_string());bad.image("sleep");}catch(...){blocked=true;}check(blocked,"catalog cannot select an outside file");
    catalog["items"][0]["file"]="sleep.gif";catalog["items"].push_back(catalog["items"][0]);write();blocked=false;try{MediaLibrary bad(root.generic_string());}catch(...){blocked=true;}check(blocked,"ambiguous ids rejected");
    catalog["items"].erase(catalog["items"].end()-1);write();{std::ofstream f(root/"sleep.gif",std::ios::binary);f<<"not an image";}blocked=false;try{MediaLibrary bad(root.generic_string());bad.image("sleep");}catch(...){blocked=true;}check(blocked,"non-image data is not sent as a sticker");
    std::cout<<"PASS media library text search, GIF preservation, path boundary, invalid catalog and non-image rejection\n";
}
