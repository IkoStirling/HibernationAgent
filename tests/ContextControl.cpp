#include <Hibernation/ContextControl.h>
#include <Hibernation/OperationReplay.h>
#include <Hibernation/Review.h>
#include <AYTestFixtures.h>
using namespace hibernation;
using Json=nlohmann::json;
TEST_SUITE(ContextControlPolicy)
TEST_CASE(presets_are_bounded_and_legacy_can_be_restored){
    const auto normal=ContextControl::read(Json::object());CHECK(normal.enabled);CHECK(normal.historyLimit(24000,0)==12000);CHECK(normal.historyLimit(24000,18000)==18000);CHECK(normal.historyLimit(24000,50000)==24000);
    CHECK(ContextControl::read({{"preset","economy"}}).historyBytes==7000);
    const auto full=ContextControl::read({{"preset","full"}});CHECK(!full.enabled);CHECK(full.reviewReadOnlyTools);CHECK(full.reviewAttempts==2);CHECK(full.historyLimit(24000,0)==24000);
    CHECK(!ContextControl::read({{"enabled",false}}).imageOnce);
    for(const auto& value:std::vector<Json>{{{"preset","unknown"}},{{"historyBytes",-1}},{{"historyBytes",1e10}},{{"reviewAttempts",0}},{{"imageOnce","false"}},{{"unknown",true}}}){bool rejected=false;try{ContextControl::read(value);}catch(...){rejected=true;}CHECK(rejected);}
}
TEST_CASE(only_explicit_read_queries_skip_semantic_review){
    Json batch={{"kind","call"},{"calls",Json::array({{{"tool","computer.screenshot"}},{{"tool","memory.read"}}})}};CHECK(readOnlyModelBatch(batch));
    batch["calls"].push_back({{"tool","workspace.write"}});CHECK(!readOnlyModelBatch(batch));
    CHECK(!readOnlyModelTool("plugin.read"));CHECK(!readOnlyModelTool("napcat.capture_and_send"));CHECK(replayProtectedOperation("napcat.capture_and_send"));
}
TEST_CASE(review_projection_preserves_current_input_and_referenced_evidence){
    const Json event={{"event_id","current"},{"input",std::string(8000,'u')},{"channel_identity",{{"trusted",true}}},{"authorized_context",Json::array({{{"id","old"},{"data",{{"text",std::string(8000,'h')}}}},{{"id","proof"},{"data",{{"text",std::string(8000,'e')}}}},{{"id","current"},{"data",{{"text","unaltered"}}}}})}};
    const Json candidate={{"calls",Json::array({{{"args",{{"source_ids",Json::array({"proof"})}}}}})}};
    const auto projected=reviewEventContext(event.dump(),candidate,1024);CHECK(projected.at("input")==event.at("input"));CHECK(projected.at("channel_identity")==event.at("channel_identity"));CHECK(projected.at("authorized_context").size()==2);CHECK(projected.at("authorized_context")[0].at("id")=="proof");CHECK(projected.at("review_context_omitted_ids")==Json::array({"old"}));CHECK(event.at("authorized_context").size()==3);
}
TEST_CASE(old_receipts_preserve_execution_and_source_facts_without_mutating_original){
    const Json receipt={{"ok",true},{"source_id","evidence"},{"call_id","a"},{"result",{{"completed",false},{"queued",true},{"text",std::string(20000,'x')}}}};
    const auto compact=priorReceiptContext(receipt,1024);CHECK(compact.dump().size()<=1024);CHECK(compact.at("source_id")=="evidence");CHECK(!compact.at("result").at("completed").get<bool>());CHECK(compact.at("result").at("queued").get<bool>());CHECK(receipt.at("result").at("text").get<std::string>().size()==20000);
    std::vector<ayt::inference::Message> messages={{"tool",receipt.dump()},{"user",Json{{"input",std::string(20000,'u')}}.dump()}};const auto user=messages.back().text;compactPriorToolMessages(messages,1024);CHECK(messages[0].text.size()<=1024);CHECK(messages[1].text==user);
}
TEST_CASE(review_keeps_corrections_after_the_mistaken_claim_with_their_speakers){
    const Json history=Json::array({
        {{"id","user-old"},{"authorship",recordAuthorship("user_message")},{"data",{{"text","I find those apples too sour."}}}},
        {{"id","mistake"},{"authorship",recordAuthorship("assistant_message")},{"data",{{"text","I was the one who disliked them."}}}},
        {{"id","correction"},{"authorship",recordAuthorship("user_message")},{"data",{{"text","No, that was me; our friend likes sour apples."}}}},
        {{"id","current"},{"authorship",recordAuthorship("user_message")},{"data",{{"text","What did you like about them?"}}}}
    });
    const Json event={{"event_id","current"},{"authorized_context",history}};
    const auto result=reviewEventContext(event.dump(),Json{{"text","I remember"}},8000);
    CHECK(result.at("authorized_context")==history);
    const auto trimmed=reviewEventContext(event.dump(),Json{{"carry_forward",Json::array({{{"source_ids",Json::array({"correction"})}}})}},0);
    CHECK(trimmed.at("authorized_context").size()==2);
    CHECK(trimmed.at("authorized_context")[0]==history[2]);CHECK(trimmed.at("authorized_context")[1]==history[3]);
    CHECK(event.at("authorized_context")==history);
}
TEST_CASE(request_footprints_measure_bytes_without_claiming_prices_or_tokens){
    ayt::inference::Request request;request.messages={{"system","abc"},{"user","xyz",{{"data:image/png;base64,AA==","auto"}}}};request.tools={{"test","desc","{}",false}};
    const auto size=modelRequestFootprint(request);CHECK(size.at("text_bytes")==6);CHECK(size.at("image_count")==1);CHECK(size.at("tool_schema_bytes")==10);CHECK(size.at("token_estimate").is_null());
}
TEST_SUITE_END
