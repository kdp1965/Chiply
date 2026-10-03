#include "core/Edit.h"
#include "core/WokwiJson.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace chiply;

namespace {
Document ref()
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/wokwi_414123795172381697.diagram.json", std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadWokwi(ss.str()).doc;
}
Part part(const std::string& type, const std::string& id)
{
    Part p;
    p.type = type;
    p.id = id;
    return p;
}
} // namespace

TEST_CASE("auto ids get the next free numbers, in order")
{
    std::set<std::string> used{"and1", "and2", "and325", "flop9"};
    auto m = remapIds({part("wokwi-gate-and-2", "and1"), part("wokwi-gate-and-2", "and2"),
                       part("wokwi-flip-flop-d", "flop1")},
                      used);
    CHECK(m[0].second == "and326");
    CHECK(m[1].second == "and327");
    CHECK(m[2].second == "flop10");
}

TEST_CASE("auto ids are renumbered even when their number is free")
{
    auto m = remapIds({part("wokwi-gate-or-2", "or7")}, {"or1", "or9"});
    CHECK(m[0].second == "or10");
}

TEST_CASE("user names are kept when free and suffixed on clash")
{
    std::set<std::string> used{"state_reg_2", "state_reg_2_1"};
    auto m = remapIds({part("wokwi-flip-flop-dsr", "state_reg_2"), part("wokwi-flip-flop-dsr", "auto_clear")}, used);
    CHECK(m[0].second == "state_reg_2_2");
    CHECK(m[1].second == "auto_clear");
}

TEST_CASE("duplicates inside the fragment are resolved")
{
    auto m = remapIds({part("x", "blk"), part("x", "blk")}, {});
    CHECK(m[0].second == "blk");
    CHECK(m[1].second == "blk_1");
}

TEST_CASE("duplicating a group keeps internal wires and renumbers")
{
    Document d = ref();
    const std::size_t parts = d.parts.size(), wires = d.wires.size();
    Fragment f = extractFragment(d, {"flop238", "flop239"});
    CHECK(f.parts.size() == 2);
    const std::size_t internal = f.wires.size();
    REQUIRE(internal >= 1);
    auto ids = insertFragment(d, f, 19.2, 19.2);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] != "flop238");
    CHECK(d.parts.size() == parts + 2);
    CHECK(d.wires.size() == wires + internal);
    for (std::size_t i = wires; i < d.wires.size(); ++i) {
        CHECK((d.wires[i].from.part == ids[0] || d.wires[i].from.part == ids[1]));
        CHECK((d.wires[i].to.part == ids[0] || d.wires[i].to.part == ids[1]));
    }
    CHECK(d.findPart(ids[0])->left == d.findPart("flop238")->left + 19.2);
    removeLast(d, 2, internal);
    CHECK(saveWokwi(d) == saveWokwi(ref()));
}

TEST_CASE("remove and restore puts everything back exactly")
{
    Document d = ref();
    const std::string before = saveWokwi(d);
    Removed r = removeItems(d, {"flop238", "and1"}, {0, 5});
    CHECK_FALSE(d.findPart("flop238"));
    for (const Wire& w : d.wires) {
        CHECK(w.from.part != "flop238");
        CHECK(w.to.part != "flop238");
    }
    CHECK(r.wires.size() >= 2);
    restoreItems(d, r);
    CHECK(saveWokwi(d) == before);
}
