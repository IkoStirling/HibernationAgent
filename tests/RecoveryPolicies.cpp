#include <Hibernation/ActivityReuse.h>
#include <Hibernation/DesktopActivityArea.h>
#include <Hibernation/RecentDialogue.h>
#include <AYTestFixtures.h>
using namespace hibernation;
TEST_SUITE(RecoveryPolicies)
TEST_CASE(peak_reuse_keeps_only_completed_local_intentions_and_validates_current_body){
    Json item={{"kind","move_to"},{"seconds",0},{"x",.6},{"y",.5},{"id","old-action"},{"status","completed"},{"ended_ms",1000},{"from_state","idle"},{"token","old-token"}};
    auto library=retainActivity(emptyActivityLibrary(),item,"skin1",1000);CHECK(library.at("items").size()==1);CHECK(!library.at("items")[0].at("command").contains("id"));CHECK(!library.at("items")[0].at("command").contains("token"));
    auto failed=item;failed["status"]="timed_out";CHECK(retainActivity(emptyActivityLibrary(),failed,"skin1",1000).at("items").empty());
    for(const auto* kind:{"create_drop","creative_note","interact","reference_search","puzzle_press","computer.screenshot","napcat.send_image"}){auto sideEffect=item;sideEffect["kind"]=kind;CHECK(!reusableActivity(sideEffect));}
    Json widget={{"visible",true},{"state","idle"},{"animation",{{"backend","png_sequence"},{"phase","loop"}}}};
    const auto available=Json::array({"move_to","pose","eat_nearest","look_nearest","wait"});
    CHECK(reusableCandidates(library,widget,Json::object(),available,"skin1",2000,1).size()==1);
    CHECK(reusableCandidates(library,widget,Json::object(),available,"other-skin",2000,1).empty());
    CHECK(reusableCandidates(library,widget,Json::object(),available,"skin1",8LL*86400000,1).empty());
    auto transient=widget;transient["animation"]["phase"]="end";CHECK(reusableCandidates(library,transient,Json::object(),available,"skin1",2000,1).empty());
    transient=widget;transient["state"]="resting";CHECK(reusableCandidates(library,transient,Json::object(),available,"skin1",2000,1).empty());
    library["last_used"]=library.at("items")[0].at("key");CHECK(reusableCandidates(library,widget,Json::object(),available,"skin1",2000,1).empty());
    for(int i=0;i<70;++i){item["x"]=i/100.0;item["ended_ms"]=1000+i;library=retainActivity(library,item,"skin1",2000);}CHECK(library.at("items").size()==48);
    Json eat={{"kind","eat_nearest"},{"seconds",0},{"status","completed"},{"ended_ms",1000}};library=retainActivity(emptyActivityLibrary(),eat,"skin1",1000);
    Json world={{"objects",Json::array({Json{{"kind","apple"}}})},{"appetite",.7},{"target_appetite",.7}};
    CHECK(reusableCandidates(library,widget,world,available,"skin1",2000,1).size()==1);world["target_appetite"]=.1;CHECK(reusableCandidates(library,widget,world,available,"skin1",2000,1).empty());
}
TEST_CASE(desktop_generation_and_character_targets_share_inset_monitor_work_areas){
    // Negative origin and an L-shaped desktop: the unused rectangle is not a monitor.
    const std::vector<RECT> work={{-1280,0,0,984},{0,200,1920,1240}};
    const auto areas=activityAreas(work,-1280,0,3200,1280,168);CHECK(areas.size()==2);CHECK(areas[0].left==16&&areas[0].right==1096);CHECK(areas[1].top==216&&areas[1].bottom==1056);
    std::mt19937 random(791);bool left=false,right=false;
    for(int i=0;i<1000;++i){const auto p=sampleActivityPoint(areas,random);const auto constrained=constrainActivityPoint(areas,p.first,p.second);CHECK(p==constrained);if(p.first<1280)left=true;else right=true;CHECK(p.first<1280||p.second>=216);}CHECK(left&&right);
    const auto gap=constrainActivityPoint(areas,1800,20);CHECK(gap.second==216);const auto edge=constrainActivityPoint(areas,9999,9999);CHECK(edge.first<=3016&&edge.second<=1056);
}
TEST_CASE(recent_request_reply_survive_before_older_fruit_history_and_keep_chronology){
    Json rows=Json::array({Json{{"id","old"},{"data",{{"kind","assistant_message"},{"text",std::string(6500,'f')},{"request_id","old-request"}}}},Json{{"id","u"},{"data",{{"kind","user_message"},{"text","拍张照片发给我"},{"request_id","selfie"}}}},Json{{"id","a"},{"data",{{"kind","assistant_message"},{"text","已经截图"},{"request_id","selfie"}}}},Json{{"id","now"},{"data",{{"kind","user_message"},{"text","没有收到图片"},{"request_id","complaint"}}}}});
    const auto pair=latestDialoguePair(rows,"now");CHECK(pair==std::set<std::string>({"u","a"}));
    std::vector<ayt::inference::ContextFragment> fragments;for(const auto& row:rows){const auto id=row.at("id").get<std::string>();fragments.push_back({id,row.at("data").dump(),"archive@1",id=="now"?100:pair.contains(id)?98:71});}
    auto context=ayt::inference::assembleContext(fragments,700);CHECK(context.selected.size()==3);CHECK(context.omitted.size()==1&&context.omitted[0]=="old");chronologicalRecentContext(context.selected,Json::array({"old","u","a","now"}));CHECK(context.selected[0].id=="u"&&context.selected[1].id=="a"&&context.selected[2].id=="now");
    Json longReply={{"kind","assistant_message"},{"text",std::string(9000,'x')}};CHECK(boundedCausalDialogue(longReply).at("context_excerpt")==true);CHECK(longReply.at("text").get<std::string>().size()==9000);
}
TEST_SUITE_END
