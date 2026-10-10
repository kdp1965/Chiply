#include "core/Edit.h"
#include "core/Geometry.h"
#include "core/PartLibrary.h"
#include "core/WokwiJson.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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

TEST_CASE("clipboard text round-trips a fragment")
{
    Document d = ref();
    Fragment f = extractFragment(d, {"flop238", "flop239", "xor15"});
    const std::string text = fragmentToText(f);
    CHECK(text.find("\"parts\"") != std::string::npos);
    CHECK(text.find("\"connections\"") != std::string::npos);
    auto back = fragmentFromText(text);
    REQUIRE(back);
    CHECK(back->parts.size() == f.parts.size());
    CHECK(back->wires.size() == f.wires.size());
    CHECK(fragmentToText(*back) == text);
}

TEST_CASE("a whole diagram.json pastes as a fragment; junk does not")
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/tt_template_354858054593504257.diagram.json");
    std::ostringstream ss;
    ss << in.rdbuf();
    auto f = fragmentFromText(ss.str());
    REQUIRE(f);
    CHECK(f->parts.size() == 22);
    CHECK_FALSE(fragmentFromText("hello"));
    CHECK_FALSE(fragmentFromText("{\"parts\": []}"));
}

TEST_CASE("pasting into another document renumbers against it")
{
    Document src = ref();
    Fragment f = extractFragment(src, {"flop238", "flop239", "state_reg_2"});
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/tt_template_354858054593504257.diagram.json");
    std::ostringstream ss;
    ss << in.rdbuf();
    Document dst = loadWokwi(ss.str()).doc;
    auto ids = insertFragment(dst, *fragmentFromText(fragmentToText(f)), 0, 0);
    // The template has no flops: auto ids restart at flop1; custom names kept.
    CHECK(std::count(ids.begin(), ids.end(), "flop1") == 1);
    CHECK(std::count(ids.begin(), ids.end(), "state_reg_2") == 1);
}

TEST_CASE("paste name format steps the register number", "[edit]")
{
    NameFormat f("r#_*");
    REQUIRE(f.valid());
    CHECK(*f.step("r1_b31", 1) == "r2_b31");
    CHECK(*f.step("r1_b31_n1", 1) == "r2_b31_n1"); // only the number at '#'
    CHECK(*f.step("r07_x", 1) == "r08_x");         // zero padding kept
    CHECK(!f.step("and12", 1));                    // does not match
    CHECK(!NameFormat("r_*").valid());
    CHECK(!NameFormat("r#_#").valid());
    CHECK(!NameFormat("").active());

    // Copying register r1's bank into a design that has r1 (and r2): the
    // whole group steps to the first free number, r3; other ids as usual.
    Document doc;
    for (const char* id : {"r1_b0", "r1_b1", "r1_b1_n1", "r2_b0", "and1"}) {
        Part p;
        p.id = id;
        p.type = "wokwi-gate-and-2";
        doc.parts.push_back(p);
    }
    std::vector<Part> incoming;
    for (const char* id : {"r1_b0", "r1_b1", "r1_b1_n1", "and1", "label"}) {
        Part p;
        p.id = id;
        p.type = "wokwi-gate-and-2";
        incoming.push_back(p);
    }
    Part named;
    named.id = "label";
    named.type = "wokwi-text";
    doc.parts.push_back(named);
    const auto map = remapIds(incoming, usedIds(doc), f);
    CHECK(map[0].second == "r3_b0");
    CHECK(map[1].second == "r3_b1");
    CHECK(map[2].second == "r3_b1_n1");
    CHECK(map[3].second == "and2");    // auto id: next free number
    CHECK(map[4].second == "label_1"); // other names: _N as before
    // Without a format the old rules apply.
    CHECK(remapIds(incoming, usedIds(doc))[0].second == "r1_b0_1");
}

TEST_CASE("find and replace in names, with the wires", "[edit]")
{
    Document doc;
    for (const char* id : {"r1_b0", "r1_b1", "r2_b0", "x"}) {
        Part p;
        p.id = id;
        p.type = "wokwi-gate-and-2";
        doc.parts.push_back(p);
    }
    Wire w;
    w.from = {"r1_b0", "OUT"};
    w.to = {"x", "A"};
    doc.wires.push_back(w);
    std::string err;
    auto map = replaceInIds(doc, {"r1_b0", "r1_b1"}, "r1_", "r5_", &err);
    CHECK(err.empty());
    CHECK(map == std::map<std::string, std::string>{{"r1_b0", "r5_b0"}, {"r1_b1", "r5_b1"}});
    renameParts(doc, map);
    CHECK(doc.findPart("r5_b1"));
    CHECK(doc.wires[0].from.str() == "r5_b0:OUT");
    CHECK(doc.wires[0].to.str() == "x:A");
    // Collisions with parts outside the selection are refused.
    map = replaceInIds(doc, {"r5_b0"}, "r5", "r2", &err);
    CHECK(map.empty());
    CHECK(err.find("already used") != std::string::npos);
    // Swapping within the selection is fine.
    err.clear();
    map = replaceInIds(doc, {"r5_b0", "r2_b0"}, "", "z", &err);
    CHECK(map.empty()); // empty "from": nothing to do
    renameParts(doc, {{"r5_b0", "r2_b0"}, {"r2_b0", "r5_b0"}});
    CHECK(doc.wires[0].from.str() == "r2_b0:OUT");
}

TEST_CASE("selected wires to parts outside the selection paste as ends that connect where they land", "[edit]")
{
    const PartLibrary& lib = PartLibrary::builtin();
    Document d = ref();
    // A wire with exactly one end on flop238.
    std::size_t wi = d.wires.size();
    for (std::size_t i = 0; i < d.wires.size() && wi == d.wires.size(); ++i)
        if ((d.wires[i].from.part == "flop238") != (d.wires[i].to.part == "flop238"))
            wi = i;
    REQUIRE(wi < d.wires.size());
    const Wire orig = d.wires[wi];
    const PinRef far = orig.from.part == "flop238" ? orig.to : orig.from;

    Fragment f = extractFragment(d, lib, {"flop238"}, {wi});
    REQUIRE(f.parts.size() == 2); // the flop and one placeholder
    const Part& ph = f.parts[1];
    CHECK(isEndPlaceholder(ph));
    CHECK(ph.type == "wokwi-junction");
    CHECK(ph.attrs["end"] == far.str());
    const auto at = pinPosition(ph, *lib.find("wokwi-junction"), "J");
    const auto want = pinPosition(d, lib, far);
    REQUIRE((at && want));
    CHECK(std::fabs(at->x - want->x) < 0.02);
    CHECK(std::fabs(at->y - want->y) < 0.02);
    REQUIRE(f.wires.size() == 1);
    CHECK(((f.wires[0].from.part == ph.id) != (f.wires[0].to.part == ph.id)));
    CHECK(f.wires[0].path == orig.path);

    // The clipboard text is plain Wokwi JSON and keeps the placeholder.
    const auto back = fragmentFromText(fragmentToText(f));
    REQUIRE(back);
    REQUIRE(back->parts.size() == 2);
    CHECK(isEndPlaceholder(back->parts[1]));

    // Pasted in place, the end lands on the original pin and connects to it.
    {
        Document e = d;
        const std::size_t parts = e.parts.size(), wires = e.wires.size();
        const auto ids = insertFragment(e, *back, 0, 0);
        REQUIRE(ids.size() == 2);
        const EndResolution r = resolveEnds(e, lib, ids, 1.5);
        CHECK(r.connected == 1);
        CHECK(r.left == 0);
        CHECK(e.parts.size() == parts + 1); // the placeholder is gone
        CHECK(e.findPart(ids[1]) == nullptr);
        REQUIRE(e.wires.size() == wires + 1);
        const Wire& w = e.wires.back();
        CHECK(((w.from.str() == far.str()) != (w.to.str() == far.str())));
        CHECK(((w.from.part == ids[0]) != (w.to.part == ids[0])));
    }
    // Pasted where nothing is, it stays as an ordinary junction.
    {
        Document g = d;
        const auto ids = insertFragment(g, *back, 5000, 5000);
        const EndResolution r = resolveEnds(g, lib, ids, 1.5);
        CHECK(r.connected == 0);
        CHECK(r.left == 1);
        const Part* j = g.findPart(ids[1]);
        REQUIRE(j);
        CHECK(j->type == "wokwi-junction");
        CHECK(!isEndPlaceholder(*j));
        CHECK(g.wires.back().to.pin == "J");
    }
    // A wire alone (both parts outside) pastes as two ends.
    {
        Fragment w = extractFragment(d, lib, {}, {wi});
        CHECK(w.parts.size() == 2);
        CHECK(w.wires.size() == 1);
        CHECK((isEndPlaceholder(w.parts[0]) && isEndPlaceholder(w.parts[1])));
        Document e = d;
        const auto ids = insertFragment(e, w, 0, 0);
        const EndResolution r = resolveEnds(e, lib, ids, 1.5);
        CHECK(r.connected == 2);
        CHECK(e.parts.size() == d.parts.size());
        CHECK(e.wires.back().from.str() == orig.from.str());
        CHECK(e.wires.back().to.str() == orig.to.str());
    }
}
