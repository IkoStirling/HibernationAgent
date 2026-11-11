#include <Hibernation/ReplyPlayback.h>
#include <AYTestFixtures.h>
using Json=nlohmann::json;
using namespace hibernation;
namespace {Json parts(){return Json::array({{{"reply_id","a"},{"offset",0},{"characters",2}},{{"reply_id","b"},{"offset",4},{"characters",3}}});}}
TEST_SUITE(ReplyPlayback)
TEST_CASE(reply_bundle_playback_keeps_later_messages_waiting){
    const auto state=replyPlaybackProjection(parts(),{{"state","speaking"},{"visible_characters",1},{"speech_id","voice"}});
    CHECK(state.at("a").at("state")=="speaking");CHECK(state.at("a").at("visible_characters")==1);CHECK(state.at("b").at("state")=="waiting");CHECK(state.at("b").at("visible_characters")==0);CHECK(state.at("b").at("speech_id")=="voice");
}
TEST_CASE(reply_bundle_playback_advances_one_continuous_voice_across_boundaries){
    const auto state=replyPlaybackProjection(parts(),{{"state","speaking"},{"visible_characters",5}});
    CHECK(state.at("a").at("state")=="completed");CHECK(state.at("a").at("visible_characters")==2);CHECK(state.at("b").at("state")=="speaking");CHECK(state.at("b").at("visible_characters")==1);
}
TEST_CASE(reply_bundle_completion_and_interruption_do_not_invent_spoken_tail){
    const auto done=replyPlaybackProjection(parts(),{{"state","completed"},{"visible_characters",0}});CHECK(done.at("b").at("visible_characters")==3);CHECK(done.at("a").at("state")=="completed");
    const auto stopped=replyPlaybackProjection(parts(),{{"state","interrupted"},{"visible_characters",2}});CHECK(stopped.at("a").at("state")=="completed");CHECK(stopped.at("b").at("state")=="interrupted");CHECK(stopped.at("b").at("visible_characters")==0);
}
TEST_CASE(reply_bundle_progress_rejects_negative_cursor_and_counts_unicode){
    CHECK(replyPlaybackCharacters("小爱😀")==3);const auto state=replyPlaybackProjection(parts(),{{"state","speaking"},{"visible_characters",-1}});CHECK(state.at("a").at("visible_characters")==0);CHECK(state.at("b").at("state")=="waiting");
}
TEST_SUITE_END
