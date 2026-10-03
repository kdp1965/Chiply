#include "core/IdGen.h"

#include <catch2/catch_test_macros.hpp>

using namespace chiply;

TEST_CASE("split trailing number")
{
    CHECK(splitTrailingNumber("and326") == std::pair<std::string, long>{"and", 326});
    CHECK(splitTrailingNumber("state_reg_2") == std::pair<std::string, long>{"state_reg_", 2});
    CHECK(splitTrailingNumber("auto_clear").second == -1);
}

TEST_CASE("prefixes follow Wokwi")
{
    CHECK(idPrefixForType("wokwi-gate-and-2") == "and");
    CHECK(idPrefixForType("wokwi-flip-flop-dsr") == "flop");
    CHECK(idPrefixForType("wokwi-mux-2") == "mux");
    CHECK(idPrefixForType("board-tt-block-input") == "input");
}

TEST_CASE("auto ids vs user names")
{
    CHECK(isAutoId("flop12"));
    CHECK(isAutoId("pwr3"));
    CHECK_FALSE(isAutoId("state_reg_2"));
    CHECK_FALSE(isAutoId("and"));
    CHECK_FALSE(isAutoId("cmp_eq_reg"));
}

TEST_CASE("next free id is above the highest in use")
{
    std::set<std::string> used{"and1", "and2", "and7", "or3", "andy9"};
    CHECK(nextFreeId("and", used) == "and8");
    CHECK(nextFreeId("mux", used) == "mux1");
}

TEST_CASE("instance names must be legal Verilog")
{
    CHECK(isValidInstanceName("state_reg_2"));
    CHECK(isValidInstanceName("_x"));
    CHECK_FALSE(isValidInstanceName("2bad"));
    CHECK_FALSE(isValidInstanceName("has-dash"));
    CHECK_FALSE(isValidInstanceName("module"));
    CHECK_FALSE(isValidInstanceName("and"));
    CHECK_FALSE(isValidInstanceName(""));
}
