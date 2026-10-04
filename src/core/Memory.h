#pragma once
// Built-in RAM and ROM (PLAN.md 7.3): behavioural memories that simulate
// natively and export as Verilog. Extension parts (Extended mode).
//
// The size is part of the type, because it decides the pins:
//   chiply-ram-<depth>x<width>  pins clk (clock), we, a0.., d0.. -> q0..
//                               write on the rising clk edge when we = 1;
//                               reading follows the address at once
//   chiply-rom-<depth>x<width>  pins a0.. -> q0..
// depth is a power of two from 2 to 256, width 1 to 16. Any such type is
// created on first use; Add Part offers RAM 16x8 and ROM 16x8 and the
// Inspector changes the size.
//
// ROM contents: attr "data" (hex words) or "file" (a file of hex words,
// relative to the design's folder), in $readmemh style: words separated by
// spaces, commas or newlines, // and /* */ comments, "@<hex>" moves the
// address. Missing words are 0. RAM starts like the flip-flops (random,
// zero or unknown); an ASIC RAM has no initial contents.
#include "core/Document.h"
#include "core/PartLibrary.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chiply {

std::optional<MemoryInfo> parseMemoryType(const std::string& type);
std::string memoryType(bool rom, int depth, int width);
bool isMemoryType(const std::string& type);
PartDef memoryPartDef(const MemoryInfo& m);

// Words (one per address, depth of them) from $readmemh-style text.
std::vector<std::uint32_t> parseMemoryText(const std::string& text, const MemoryInfo& m, std::string* error);

// A ROM part's contents from its attrs; `baseDir` resolves a relative
// "file". On a problem, *error says what and the words read so far are
// returned (the rest 0).
std::vector<std::uint32_t> memoryContents(const Part& part, const MemoryInfo& m, const std::string& baseDir,
                                          std::string* error);

} // namespace chiply
