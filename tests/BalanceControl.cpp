#include <Hibernation/BalanceControl.h>
#include <AYTestFixtures.h>
using namespace hibernation;
TEST_SUITE(BalanceControl)
TEST_CASE(provider_availability_requires_valid_explicit_funded_response){
    CHECK(balance::available(R"({"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"0.02"}]})")==true);
    CHECK(balance::available(R"({"is_available":false,"balance_infos":[{"currency":"CNY","total_balance":"9.00"}]})")==false);
    CHECK(balance::available(R"({"is_available":false,"balance_infos":[]})")==false);
    for(const auto* value:{"{}","null","[]",R"({"is_available":true,"balance_infos":[]})",R"({"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"0.00"}]})",R"({"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"NaN"}]})",R"({"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"1e10"}]})"})CHECK(!balance::available(value).has_value());
}
TEST_CASE(manual_offline_is_independent_of_automatic_latch){
    CHECK(balance::manual({{"offline",true}}));CHECK(!balance::manual({{"offline",true},{"manual_offline",false},{"balance_offline",true}}));
    CHECK(balance::offline({{"manual_offline",false},{"balance_offline",true}}));CHECK(balance::offline({{"manual_offline",true},{"balance_offline",false}}));
    Profile p;p.model.protocol=ayt::inference::Protocol::ChatCompletions;p.model.origin="https://api.deepseek.com";CHECK(balance::supported(p));p.model.origin="https://api.deepseek.com.evil.invalid";CHECK(!balance::supported(p));
}
TEST_SUITE_END
