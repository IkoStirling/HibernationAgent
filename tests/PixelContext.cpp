#include <Hibernation/PixelContext.h>
#include <Hibernation/ObservationContext.h>
#include <Hibernation/OperationReplay.h>
#include <Hibernation/ArchiveExperience.h>
#include <Hibernation/SolitudeOpportunities.h>
#include <AYTestFixtures.h>
using namespace hibernation;
using Json=nlohmann::json;
TEST_SUITE(PixelContext)
TEST_CASE(pixel_receipts_keep_identity_without_repeating_the_grid){
    Json document={{"id","lamp"},{"title","小灯"},{"revision",2},{"width",16},{"height",16},{"palette",Json::array({"#00000000","#ffffffff"})},{"frames",Json::array({Json{{"duration_ms",100},{"pixels",std::vector<int>(256,1)}}})}};
    Json receipt={{"ok",true},{"result",{{"document",document},{"written",true},{"completed",true}}},{"source_id","actual-source"}};
    const auto small=pixelCharacterReceipt(receipt);
    CHECK(small.at("source_id")=="actual-source");CHECK(small.at("result").at("document").at("id")=="lamp");
    CHECK(small.at("result").at("document").at("palette").size()==2);
    CHECK(!small.dump().contains("pixels"));CHECK(receipt.dump().contains("pixels"));
    const auto old=referencedObservationContext(Json{{"kind","tool_observation"},{"tool","pixel.edit"},{"result",receipt.at("result")}});
    CHECK(!old.dump().contains("pixels"));CHECK(old.at("result").at("document").at("revision")==2);
    const auto brief=compactObservation(Json{{"kind","tool_observation"},{"tool","pixel.edit"},{"result",receipt.at("result")}},false);
    CHECK(brief.at("result").at("document").at("frame_count")==1);CHECK(!brief.dump().contains("palette"));
}
TEST_CASE(pixel_show_acceptance_is_not_a_completed_creative_experience){
    Json pending={{"accepted",true},{"completed",false},{"visible_requested",true}};
    CHECK(toolObservationKind("pixel.show",pending)=="tool_intent");
    CHECK(!personalCompletionObservation(Json{{"kind","tool_intent"},{"tool","pixel.show"},{"result",pending}}));
    CHECK(continuityObservationTool("pixel.edit"));CHECK(revisitableObservationTool("pixel.preview"));
    CHECK(personalCompletionObservation(Json{{"kind","tool_observation"},{"tool","pixel.edit"},{"result",{{"written",true},{"completed",true}}}}));
}
TEST_CASE(review_rewriting_does_not_repeat_a_successful_pixel_mutation){
    OperationReplay replay;Json args={{"id","lamp"},{"expected_revision",1},{"frame",0},{"commands",Json::array({{{"op","pixel"},{"x",2},{"y",2},{"color",1}}})}};
    const Json receipt={{"ok",true},{"source_id","pixel-effect"},{"result",{{"written",true},{"completed",true},{"document",{{"id","lamp"},{"revision",2}}}}}};
    replay.completed("pixel.edit",args,receipt,"call-one");replay.afterReviewRejection();const auto reused=replay.reuse("pixel.edit",args);
    CHECK(reused.has_value());CHECK(reused->at("reused_after_review")==true);CHECK(reused->at("source_id")=="pixel-effect");
    CHECK(replay.executionFacts().size()==1);CHECK(replay.reuse("pixel.edit",args)->at("ok")==false);
    CHECK(!replayProtectedOperation("pixel.preview"));
    CHECK(replayProtectedOperation("pixel.redo"));CHECK(continuityObservationTool("pixel.redo"));
    CHECK(pixelPresentationCheckpoint(Json{{"plugin","pixel-canvas"},{"kind","pixel.hidden"}}));
    CHECK(!pixelPresentationCheckpoint(Json{{"plugin","pixel-canvas"},{"kind","pixel.revised"}}));
    CHECK(!pixelPresentationCheckpoint(Json{{"plugin","other"},{"kind","pixel.hidden"}}));
}
TEST_CASE(graphic_creation_can_be_revisited_without_becoming_external_fact){
    const Json record={{"kind","tool_observation"},{"tool","pixel.edit"},{"result",{{"written",true},{"completed",true},{"document",{{"id","lamp"},{"title","A lamp"},{"width",16},{"height",16},{"revision",2},{"frames",Json::array({{{"duration_ms",100},{"pixels",std::vector<int>(256,1)}}})}}}}}};
    const auto view=archiveWorldExperience(record);CHECK(view.is_object());CHECK(view.at("result").at("document").at("content_kind")=="authored_pixel_art");CHECK(!view.dump().contains("pixels"));
    const auto key=activityConsiderationKey(record);CHECK(!key.empty());CHECK(key==activityConsiderationKey(record));
    auto pending=record;pending["kind"]="tool_intent";pending["result"]["completed"]=false;CHECK(activityConsiderationKey(pending).empty());CHECK(archiveWorldExperience(pending).is_null());
    auto operatorEvent=record;operatorEvent["kind"]="pixel.revised";CHECK(activityConsiderationKey(operatorEvent).empty());
}
TEST_SUITE_END
