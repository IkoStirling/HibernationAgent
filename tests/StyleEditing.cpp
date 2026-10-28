#include <Hibernation/StyleEditing.h>
#include <Hibernation/PluginCharacterContext.h>
#include <AYTestFixtures.h>
using namespace hibernation;
TEST_SUITE(StyleEditing)
TEST_CASE(style_management_is_not_a_character_capability){
    CHECK(operatorStyleTool("style_lora.train"));CHECK(operatorStyleTool("style_lora.preview"));CHECK(!operatorStyleTool("atelier.read"));
    CHECK(pluginCharacterContext("style-lora",{{"adapter_id","PRIVATE_TRAINING_VERSION"},{"issue","trace"}}).empty());
}
TEST_CASE(style_edits_preserve_protected_literals_without_pretending_to_prove_meaning){
    CHECK(styleLiteralIntegrity("我不清楚，不敢瞎说。","我不清楚。"));
    CHECK(styleLiteralIntegrity("还要2分钟。","还要2分钟，先等一下。"));
    CHECK(!styleLiteralIntegrity("还要2分钟。","已经等了3分钟。"));
    CHECK(!styleLiteralIntegrity("https://example.org/a","https://example.org/b"));
    CHECK(!styleLiteralIntegrity("还没完成。",""));
    // This deliberately still needs semantic review, not a keyword blacklist.
    CHECK(styleLiteralIntegrity("还没完成。","已经完成了。"));
}
}
