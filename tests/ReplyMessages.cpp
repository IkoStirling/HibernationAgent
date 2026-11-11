#include <Hibernation/ReplyMessages.h>
#include <Hibernation/TurnProtocol.h>
#include <Hibernation/NativeToolBridge.h>
#include <Hibernation/Application.h>
#include <AYSerializer/JsonSchema.h>
#include <AYTestFixtures.h>
using namespace hibernation;
namespace {
Json multi(Json messages,Json text=nullptr){return {{"text",std::move(text)},{"messages",std::move(messages)}};}
Json turn(Json messages,Json text=nullptr){auto value=multi(std::move(messages),std::move(text));value["version"]=1;value["kind"]="reply";value["calls"]=Json::array();return value;}
Json legacy(Json messages,Json text=nullptr){return {{"visible_reply",std::move(text)},{"messages",std::move(messages)},{"actions",Json::array()},{"memories",Json::array()},{"state_update",Json::object()},{"stage_proposal",nullptr}};}
std::string repeat(std::string_view piece,std::size_t count){std::string out;for(std::size_t index=0;index<count;++index)out+=piece;return out;}
bool partsRejected(const Json& value){try{(void)replyMessageParts(value);return false;}catch(const std::invalid_argument&){return true;}}
}
TEST_SUITE(ReplyMessages)
TEST_CASE(legacy_single_reply_is_not_automatically_split_or_trimmed){
    const auto text="  一段完整的话。\n\n下一段仍属于同一条。  ";Json input={{"text",text},{"presentation",{{"speech",false}}}};
    const auto result=normalizeReplyMessages(input);CHECK(result.ok&&result.value==input);CHECK(!result.value.contains("messages"));const auto parts=replyMessageParts(input);CHECK(parts.size()==1&&parts[0]==text);
    input["text"]=std::string(6000,'a');CHECK(normalizeReplyMessages(input).ok);input["text"]=std::string(6001,'a');CHECK(!normalizeReplyMessages(input).ok);
}
TEST_CASE(null_messages_means_omission_without_erasing_other_fields){
    Json input={{"text","保留原话。"},{"messages",nullptr},{"carry_forward",Json::array()}};const auto before=input;
    const auto result=normalizeReplyMessages(input);CHECK(result.ok&&!result.value.contains("messages"));CHECK(result.value.at("carry_forward")==input.at("carry_forward"));CHECK(input==before);
}
TEST_CASE(authored_parts_are_preserved_and_have_one_canonical_full_text){
    auto input=multi(Json::array({"先说这一点。","还有另一点。"}));input["presentation"]={{"speech",false}};const auto original=input;
    const auto result=normalizeReplyMessages(input);CHECK(result.ok);CHECK(result.value.at("text")=="先说这一点。\n\n还有另一点。");CHECK(result.value.at("messages")==input.at("messages"));CHECK(result.value.at("presentation")==input.at("presentation"));CHECK(input==original);
    const auto parts=replyMessageParts(result.value);CHECK(parts.size()==2&&parts[0]=="先说这一点。"&&parts[1]=="还有另一点。");
    const auto one=normalizeReplyMessages(multi(Json::array({"只发这一条。"})));CHECK(one.ok&&one.value.at("messages").size()==1&&one.value.at("text")=="只发这一条。");
}
TEST_CASE(simultaneous_text_must_match_exact_canonical_content){
    const auto messages=Json::array({"A","B"});CHECK(normalizeReplyMessages(multi(messages,"A\n\nB")).ok);
    for(const auto& value:{Json("A B"),Json("A\nB"),Json(""),Json(17),Json::array()}){const auto result=normalizeReplyMessages(multi(messages,value));CHECK(!result.ok&&result.error=="reply_messages_text_mismatch");}
    CHECK(!normalizeReplyMessages(multi(Json::array({"e\xcc\x81"}),"\xc3\xa9")).ok); // Do not rewrite Unicode to conceal a mismatch.
}
TEST_CASE(message_count_type_and_whitespace_fail_as_a_complete_bundle){
    for(const auto& messages:{Json::array(),Json::array({"a","b","c","d","e","f","g"}),Json::object(),Json("a"),Json::array({"valid",7}),Json::array({"valid",nullptr}),Json::array({"valid",""}),Json::array({" \t\r\n"}),Json::array({"\xe3\x80\x80"}),Json::array({"\xc2\xa0"}),Json::array({"\xe2\x80\x8b"})}){
        const auto input=multi(messages,"valid fallback must not be sent");const auto result=normalizeReplyMessages(input);CHECK(!result.ok);CHECK(partsRejected(input));
    }
    CHECK(normalizeReplyMessages(multi(Json::array({"a","b","c","d","e","f"}))).ok);
}
TEST_CASE(message_character_limits_count_unicode_scalars_not_utf8_bytes_or_utf16_units){
    const auto han=repeat("汉",2000),emoji=repeat("\xf0\x9f\xa6\x96",2000);
    CHECK(han.size()==6000&&emoji.size()==8000);CHECK(normalizeReplyMessages(multi(Json::array({han}))).ok);CHECK(normalizeReplyMessages(multi(Json::array({emoji}))).ok);
    CHECK(!normalizeReplyMessages(multi(Json::array({han+"汉"}))).ok);CHECK(!normalizeReplyMessages(multi(Json::array({emoji+"a"}))).ok);
    CHECK(normalizeReplyMessages(Json{{"text",repeat("汉",6000)}}).ok);
}
TEST_CASE(canonical_total_includes_the_authored_record_separators){
    const auto exact=Json::array({std::string(2000,'a'),std::string(2000,'b'),std::string(1996,'c')});const auto result=normalizeReplyMessages(multi(exact));CHECK(result.ok&&result.value.at("text").get<std::string>().size()==6000);
    auto over=exact;over[2]=std::string(1997,'c');const auto rejected=normalizeReplyMessages(multi(over));CHECK(!rejected.ok&&rejected.error=="reply_messages_total");CHECK(partsRejected(multi(over)));
    CHECK(!normalizeReplyMessages(multi(Json::array({std::string(2000,'a'),std::string(2000,'b'),std::string(2000,'c')}))).ok);
}
TEST_CASE(control_characters_and_malformed_utf8_never_produce_partial_parts){
    const std::vector<std::string> invalid={std::string("a\0b",3),std::string("a\x01"),std::string("a\x7f"),std::string("\xc2\x85"),std::string("\xc0\xaf"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\x80"),std::string("\xf0\x9f",2)};
    for(const auto& text:invalid){CHECK(!normalizeReplyMessages(multi(Json::array({"first is valid",text}))).ok);CHECK(partsRejected(multi(Json::array({"first is valid",text}))));CHECK(!normalizeReplyMessages(Json{{"text",text}}).ok);}
    CHECK(normalizeReplyMessages(multi(Json::array({"保留\n换行\t和制表。"}))).ok);
}
TEST_CASE(empty_legacy_background_text_is_explicit_but_never_allows_empty_messages){
    CHECK(!normalizeReplyMessages(Json{{"text",""}}).ok);CHECK(normalizeReplyMessages(Json{{"text",""}},"text",true).ok);CHECK(replyMessageParts(Json{{"text",""}}).empty());
    CHECK(!normalizeReplyMessages(multi(Json::array({""})),"text",true).ok);CHECK(!normalizeReplyMessages(Json{{"text",nullptr}},"text",true).ok);CHECK(!normalizeReplyMessages(Json{{"messages",Json::array({"x"})}}).ok);
}
TEST_CASE(turn_v1_supports_messages_with_the_same_closed_shape){
    const auto result=validateTurn(turn(Json::array({"第一条。","第二条。"})).dump());CHECK(result.ok&&result.value.at("version")==1);CHECK(result.value.at("text")=="第一条。\n\n第二条。");CHECK(result.value.at("messages").size()==2);
    auto wrong=turn(Json::array({"a","b"}),"different");CHECK(!validateTurn(wrong.dump()).ok);wrong=turn(Json::array({"a"}));wrong["extra"]="not silently erased";CHECK(!validateTurn(wrong.dump()).ok);
    const auto plain=plainReplyCandidate("一条完整回复，不要自动拆开。");CHECK(plain.ok&&!plain.value.contains("messages"));
    CHECK(!validateTurn(R"({"version":1,"kind":"reply","text":null,"calls":[],"messages":["a"],"messages":["b"]})").ok);
}
TEST_CASE(call_and_silence_never_smuggle_visible_message_records){
    Json value={{"version",1},{"kind","silence"},{"text",nullptr},{"calls",Json::array()},{"messages",Json::array({"not silent"})}};CHECK(!validateTurn(value.dump()).ok);value["messages"]=nullptr;const auto silent=validateTurn(value.dump());CHECK(silent.ok&&!silent.value.contains("messages"));
    value["kind"]="call";value["calls"]=Json::array({{{"id","a"},{"tool","state.observe"},{"args",Json::object()}}});CHECK(validateTurn(value.dump()).ok);value["messages"]=Json::array({"not permitted"});CHECK(!validateTurn(value.dump()).ok);
    value=turn(Json::array({"a"}));value["calls"]=Json::array({{{"id","a"},{"tool","state.observe"},{"args",Json::object()}}});CHECK(!validateTurn(value.dump()).ok);
}
TEST_CASE(strict_provider_schema_declares_nullable_messages_without_unbounded_arguments){
    const auto schema=Json::parse(turnProviderSchema(Json::array({{{"name","state.observe"},{"parameters",{{"type","object"},{"properties",Json::object()},{"additionalProperties",false}}}}})));
    CHECK(ayt::serializer::validateJsonSchema(schema.dump()));const auto messagesSchema=schema.at("properties").at("messages").dump();CHECK(ayt::serializer::validateJsonDocument("null",messagesSchema));CHECK(ayt::serializer::validateJsonDocument("[\"a\",\"b\"]",messagesSchema));CHECK(!ayt::serializer::validateJsonDocument("[]",messagesSchema));CHECK(std::find(schema.at("required").begin(),schema.at("required").end(),"messages")!=schema.at("required").end());
    CHECK(Json::parse(turnSchema()).at("properties").at("messages")==Json::parse(outputSchema()).at("properties").at("messages"));
}
TEST_CASE(native_final_function_preserves_the_same_authored_bundle){
    NativeToolBridge bridge(Json::array());const auto args=multi(Json::array({"甲。","乙。"}));const auto result=bridge.candidate({{"final","ha_turn_reply",args.dump()}},0);
    CHECK(result.ok&&result.value.at("text")=="甲。\n\n乙。"&&result.value.at("messages")==args.at("messages"));
    auto wrong=args;wrong["text"]="not canonical";CHECK(!bridge.candidate({{"final","ha_turn_reply",wrong.dump()}},0).ok);
    CHECK(!bridge.candidate({{"final","ha_turn_silence",Json{{"messages",Json::array({"no"})}}.dump()}},0).ok);
    CHECK(!bridge.candidate({{"final","ha_turn_reply",args.dump()},{"operation","other","{}"}},0).ok);
}
TEST_CASE(reply_timing_is_optional_validated_presentation_metadata){
    auto value=turn(Json::array({"简短回复。"}));value["presentation"]={{"reply_timing","immediate"}};CHECK(validateTurn(value.dump()).ok);
    value["presentation"]["reply_timing"]="wait_for_followup";CHECK(validateTurn(value.dump()).ok);
    value["presentation"]["reply_timing"]=nullptr;const auto optional=validateTurn(value.dump());CHECK(optional.ok&&!optional.value.at("presentation").contains("reply_timing"));
    value["presentation"]["reply_timing"]="invented-mode";CHECK(!validateTurn(value.dump()).ok);
}
TEST_CASE(native_final_tolerates_redundant_exact_envelope_and_messages_without_text){
    NativeToolBridge bridge(Json::array());
    const auto reply=bridge.candidate({{"final","ha_turn_reply",R"({"version":1,"kind":"reply","text":"好。","calls":[]})"}},0);
    CHECK(reply.ok&&reply.value.at("text")=="好。");
    const auto bundle=bridge.candidate({{"final","ha_turn_reply",R"({"messages":["甲。","乙。"]})"}},0);
    CHECK(bundle.ok&&bundle.value.at("text")=="甲。\n\n乙。");
    CHECK(bridge.candidate({{"final","ha_turn_silence",R"({"version":1,"kind":"silence","text":null,"calls":[]})"}},0).ok);
    for(const auto* bad:{"{}",R"({"kind":"call","text":"好。","calls":[]})",R"({"version":2,"text":"好。"})",R"({"text":"好。","calls":[{"tool":"workspace.write"}]})",R"({"messages":[]})",R"({"messages":["甲。"],"text":"乙。"})"})
        CHECK(!bridge.candidate({{"final","ha_turn_reply",bad}},0).ok);
}
TEST_CASE(classic_visible_reply_uses_the_same_normalizer_and_existing_empty_reply_policy){
    ProposalContext context;context.eventKind="user_message";auto result=validateOutput(legacy(Json::array({"甲。","乙。"})).dump(),context);
    CHECK(result.ok&&result.value.at("visible_reply")=="甲。\n\n乙。"&&result.value.at("messages").size()==2);CHECK(replyMessageParts(result.value,"visible_reply").size()==2);
    CHECK(!validateOutput(legacy(Json::array({"甲。"}),"other").dump(),context).ok);CHECK(!validateOutput(legacy(Json::array({""})).dump(),context).ok);
    auto empty=legacy(nullptr,"");CHECK(!validateOutput(empty.dump(),context).ok);context.eventKind="solitude";CHECK(validateOutput(empty.dump(),context).ok);empty["visible_reply"]=nullptr;CHECK(!validateOutput(empty.dump(),context).ok);
}
TEST_CASE(optional_final_feeling_preserves_sparse_axes_and_null_is_omission){
    const Json feeling={{"source_id","real-event"},{"stimulus",{{"emotions",{{"joy",.2},{"worry",-.1}}},{"desires",{{"express",.3}}}}}};
    auto value=turn(Json::array({"一条。","再一条。"}));value["feeling"]=feeling;const auto result=validateTurn(value.dump());CHECK(result.ok&&result.value.at("feeling")==feeling&&turnHasPrivateUpdates(result.value));
    value["kind"]="silence";value["text"]=nullptr;value.erase("messages");const auto silent=validateTurn(value.dump());CHECK(silent.ok&&silent.value.at("feeling")==feeling&&turnHasPrivateUpdates(silent.value));
    value["feeling"]=nullptr;const auto omitted=validateTurn(value.dump());CHECK(omitted.ok&&!omitted.value.contains("feeling")&&!turnHasPrivateUpdates(omitted.value));
    value["kind"]="call";value["calls"]=Json::array({{{"id","a"},{"tool","state.observe"},{"args",Json::object()}}});value["feeling"]=feeling;CHECK(validateTurn(value.dump()).error=="feeling_requires_final");
}
TEST_CASE(strict_provider_null_feeling_axes_normalize_only_known_optional_fields){
    const auto schema=Json::parse(turnProviderSchema(Json::array({{{"name","state.observe"},{"parameters",{{"type","object"},{"properties",Json::object()},{"additionalProperties",false}}}}})));
    Json raw={{"version",1},{"kind","reply"},{"text","自然说话。"},{"calls",Json::array()},{"messages",nullptr},{"carry_forward",nullptr},{"appraisal",nullptr},{"presentation",{{"speech",true},{"bubble",true}}},{"feeling",{{"source_id","real-event"},{"stimulus",{{"emotions",{{"joy",.2},{"anger",nullptr},{"sadness",nullptr},{"worry",nullptr},{"affection",nullptr},{"disgust",nullptr},{"surprise",nullptr}}},{"desires",nullptr}}}}}};
    raw["presentation"]["reply_timing"]=nullptr;CHECK(ayt::serializer::validateJsonDocument(raw.dump(),schema.dump()));const auto checked=validateTurn(raw.dump());CHECK(checked.ok);CHECK(checked.value.at("feeling").at("stimulus").at("emotions")==Json({{"joy",.2}}));CHECK(!checked.value.at("feeling").at("stimulus").contains("desires"));
    auto invalid=raw;invalid["feeling"]["stimulus"]["emotions"]["unknown_axis"]=nullptr;CHECK(!validateTurn(invalid.dump()).ok);invalid=raw;invalid["feeling"]["source_id"]=nullptr;CHECK(!validateTurn(invalid.dump()).ok);invalid=raw;invalid["feeling"]["stimulus"]=nullptr;CHECK(!validateTurn(invalid.dump()).ok);
    CHECK(!validateTurn(R"({"version":1,"kind":"reply","text":"a","calls":[],"feeling":null,"feeling":null})").ok);
}
TEST_CASE(native_final_feeling_uses_shared_schema_and_legacy_output_remains_unchanged){
    CHECK(ayt::serializer::validateJsonSchema(feelingProposalParameters().dump()));CHECK(feelingProposalParameters().dump().find("maxProperties")==std::string::npos);
    const Json feeling={{"source_id","real-event"},{"stimulus",{{"emotions",{{"joy",.2},{"anger",nullptr}}},{"desires",nullptr}}}};NativeToolBridge bridge(Json::array());auto args=multi(Json::array({"a","b"}));args["feeling"]=feeling;
    const auto result=bridge.candidate({{"final","ha_turn_reply",args.dump()}},0);CHECK(result.ok&&result.value.at("feeling").at("stimulus").at("emotions")==Json({{"joy",.2}}));
    const auto silent=bridge.candidate({{"final","ha_turn_silence",Json{{"feeling",feeling}}.dump()}},0);CHECK(silent.ok&&silent.value.at("kind")=="silence"&&turnHasPrivateUpdates(silent.value));
    ProposalContext context;context.eventKind="user_message";auto old=legacy(nullptr,"old path");old["feeling"]=feeling;CHECK(!validateOutput(old.dump(),context).ok);
}
TEST_CASE(native_argument_duplicate_keys_are_rejected_before_normalization){
    NativeToolBridge bridge(Json::array({{{"name","state.observe"},{"parameters",{{"type","object"},{"additionalProperties",false},{"properties",{{"axis",{{"type","number"}}}}}}}}}));
    CHECK(!bridge.candidate({{"final","ha_turn_reply",R"({"text":"first","text":"second"})"}},0).ok);
    CHECK(!bridge.candidate({{"final","ha_turn_reply",R"({"text":null,"messages":["first"],"messages":["second"]})"}},0).ok);
    CHECK(!bridge.candidate({{"final","ha_turn_silence",R"({"feeling":{"source_id":"event","stimulus":{"emotions":{"joy":0.2,"joy":null}}}})"}},0).ok);
    CHECK(!bridge.candidate({{"operation",nativeToolName("state.observe"),R"({"axis":1,"axis":null})"}},0).ok);
}
TEST_SUITE_END
