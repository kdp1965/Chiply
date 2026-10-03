#include "core/IdGen.h"
#include "core/WokwiJson.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace chiply;

namespace {

std::string readRef(const std::string& name)
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char* const kReferenceDiagrams[] = {
    "wokwi_414123795172381697.diagram.json",
    "tt_template_354858054593504257.diagram.json",
};

} // namespace

TEST_CASE("reference diagrams round-trip byte for byte")
{
    for (const char* name : kReferenceDiagrams) {
        INFO(name);
        const std::string text = readRef(name);
        LoadResult r = loadWokwi(text);
        CHECK(r.warnings.empty());
        CHECK(saveWokwi(r.doc) == text);
    }
}

TEST_CASE("reference design contents")
{
    LoadResult r = loadWokwi(readRef("wokwi_414123795172381697.diagram.json"));
    const Document& d = r.doc;
    CHECK(d.parts.size() == 1024);
    CHECK(d.wires.size() == 2042);
    CHECK(d.author() == "Ken Pettit");
    const Part* reg = d.findPart("state_reg_2");
    REQUIRE(reg);
    CHECK(reg->type == "wokwi-flip-flop-dsr");
    const Part* sw = d.findPart("sw1");
    REQUIRE(sw);
    CHECK(sw->rotate == 90);
    CHECK(d.wires[8].from == PinRef{"ttout", "EXTOUT0"});
    CHECK(d.wires[8].color == "green");
    CHECK(d.wires[8].path.source.size() == 3);
}

TEST_CASE("unknown keys and unparseable paths survive")
{
    const std::string text = R"({
  "version": 1,
  "serialMonitor": { "display": "always" },
  "parts": [ { "type": "x-new-part", "id": "n1", "left": 1, "attrs": {}, "future": [ 1, 2 ] } ],
  "connections": [ [ "n1:A", "n1:B", "red", [ "q5" ], "extra" ] ],
  "dependencies": {}
})";
    LoadResult r = loadWokwi(text);
    CHECK(r.warnings.size() == 1);
    CHECK(toJson(r.doc) == Json::parse(text));
}

TEST_CASE("editing a loaded part keeps Wokwi key order")
{
    LoadResult r = loadWokwi(R"({"parts":[{"type":"wokwi-gate-and-2","id":"and1","top":0,"left":0,"attrs":{}}],"connections":[]})");
    Part& p = r.doc.parts[0];
    p.rotate = 90;
    p.left = 9.6;
    Json j = partToJson(p);
    std::vector<std::string> keys;
    for (auto it = j.begin(); it != j.end(); ++it)
        keys.push_back(it.key());
    CHECK(keys == std::vector<std::string>{"type", "id", "top", "left", "rotate", "attrs"});
    CHECK(j["left"].dump() == "9.6");
}

TEST_CASE("new parts use canonical key order and omit rotate 0")
{
    Part p;
    p.type = "wokwi-mux-2";
    p.id = "mux1";
    p.top = 19.2;
    p.left = 0;
    CHECK(partToJson(p).dump() == R"({"type":"wokwi-mux-2","id":"mux1","top":19.2,"left":0,"attrs":{}})");
}

TEST_CASE("rename rewrites wire ends")
{
    LoadResult r = loadWokwi(readRef("wokwi_414123795172381697.diagram.json"));
    Document& d = r.doc;
    std::size_t refs = 0;
    for (const Wire& w : d.wires)
        refs += (w.from.part == "flop28") + (w.to.part == "flop28");
    REQUIRE(refs > 0);
    CHECK_FALSE(d.renamePart("flop28", "state_reg_2")); // taken
    CHECK(d.renamePart("flop28", "my_reg"));
    std::size_t after = 0;
    for (const Wire& w : d.wires)
        after += (w.from.part == "my_reg") + (w.to.part == "my_reg");
    CHECK(after == refs);
    CHECK_FALSE(d.findPart("flop28"));
}

TEST_CASE("reference ids are unique and new ids do not collide")
{
    LoadResult r = loadWokwi(readRef("wokwi_414123795172381697.diagram.json"));
    std::set<std::string> ids;
    for (const Part& p : r.doc.parts)
        CHECK(ids.insert(p.id).second);
    CHECK(nextFreeId("and", ids) == "and326");
    CHECK(nextFreeId("mux", ids) == "mux63");
}

TEST_CASE("empty document saves as a valid Wokwi file")
{
    Document d = Document::makeEmpty("Ken Pettit");
    LoadResult r = loadWokwi(saveWokwi(d));
    CHECK(r.doc.parts.empty());
    CHECK(r.doc.author() == "Ken Pettit");
    CHECK(saveWokwi(r.doc) == saveWokwi(d));
}

TEST_CASE("saving over a read-only file is refused")
{
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "chiply_readonly_test.json";
    Document d = Document::makeEmpty("t");
    saveWokwiFile(d, p.string());
    fs::permissions(p, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    CHECK_THROWS(saveWokwiFile(d, p.string()));
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write);
    CHECK_NOTHROW(saveWokwiFile(d, p.string()));
    fs::remove(p);
}
