#include "core/JsonFormat.h"

#include <catch2/catch_test_macros.hpp>

using namespace chiply;

TEST_CASE("numbers print like JavaScript")
{
    CHECK(formatNumber(864.0) == "864");
    CHECK(formatNumber(-205.3) == "-205.3");
    CHECK(formatNumber(21.01) == "21.01");
    CHECK(formatNumber(-0.0) == "0");
    CHECK(formatNumber(0.5) == "0.5");
}

TEST_CASE("round2 keeps two decimals")
{
    CHECK(round2(21.0149) == 21.01);
    CHECK(round2(9.6 * 3) == 28.8);
    CHECK(formatNumber(round2(0.1 + 0.2)) == "0.3");
}

TEST_CASE("short values stay on one line, long ones break")
{
    Json j = Json::parse(R"({"a":[{"type":"x","id":"y","attrs":{}}],"b":{}})");
    CHECK(prettyPrint(j) == "{ \"a\": [ { \"type\": \"x\", \"id\": \"y\", \"attrs\": {} } ], \"b\": {} }");
    CHECK(prettyPrint(j, 20) == "{\n"
                                "  \"a\": [\n"
                                "    {\n"
                                "      \"type\": \"x\",\n"
                                "      \"id\": \"y\",\n"
                                "      \"attrs\": {}\n"
                                "    }\n"
                                "  ],\n"
                                "  \"b\": {}\n"
                                "}");
}

TEST_CASE("trailing comma counts toward the width")
{
    // Line is exactly 20 chars without the comma, 21 with it.
    Json j = Json::parse(R"([["aaaaaaaa","bbbb"],1])");
    std::string s = prettyPrint(j, 20);
    CHECK(s.find("    \"aaaaaaaa\",") != std::string::npos);
}
