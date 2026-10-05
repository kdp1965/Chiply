# Chiply — Build Plan

Status: draft for review (2026-10-02)
Author: Claude, for Ken Pettit

Chiply is a native C++ / Qt desktop schematic editor that reads and writes Wokwi
`diagram.json` files unchanged, reproduces the Wokwi logic-design look and feel
(the Tiny Tapeout flavor of Wokwi), and adds what Wokwi lacks: naming of gates
and flops, a real netlist and Verilog export, a built-in simulator, and
user-defined custom blocks.

Everything in section 2 was verified against your reference design
(https://wokwi.com/projects/414123795172381697), the official Tiny Tapeout
Wokwi template, the Wokwi docs, the `wokwi-elements` source, and the Tiny
Tapeout support tools checked out on this machine. Copies of the reference
files are in `reference/` so they can be used as test fixtures:

| File | What it is |
|---|---|
| `reference/wokwi_414123795172381697.diagram.json` | Your design: 1024 parts, 2042 wires |
| `reference/tt_um_wokwi_414123795172381697.v` | Wokwi's own Verilog export of it (the golden netlist) |
| `reference/tt_template_354858054593504257.diagram.json` | Official Tiny Tapeout Wokwi template (22 parts) |
| `reference/cells.v` | Tiny Tapeout's Wokwi cell library (`and_cell`, `dff_cell`, ...) |

---

## 1. Goals and non-goals

Goals, in priority order:

1. Open any Wokwi `diagram.json`, render it recognizably like Wokwi, save it back without disturbing anything you did not touch.
2. Wokwi-parity editing: add/move/rotate/delete parts, draw and reroute wires on the 0.1 inch grid, wire colors by number keys, zoom and pan, undo/redo.
3. Beyond Wokwi: name gates and flops, marquee select, copy/paste with wires and routes preserved and ids renumbered to fit the target file, and several files open in tabs so blocks can be developed on their own and pasted into a larger design.
4. Netlist extraction and Verilog export that matches what Wokwi's exporter produces (same `cells.v` primitives, same port list), so the Tiny Tapeout flow accepts it directly.
5. A built-in event-driven logic simulator, interactive like Wokwi (buttons, switches, clock, LEDs, 7-segment), plus VCD output. No PDK, no gate-level/standard-cell simulation and no external tools (Verilator, Icarus) are required to simulate; it works out of the box, as Wokwi does.
6. Custom blocks: a Verilog module behind a schematic symbol, usable like any gate.

Non-goals: microcontroller firmware simulation (a Raspberry Pi Pico can be placed and wired so files round-trip, but running its firmware is a separate project), Wokwi's custom-chip C API, breadboards, analog parts. Chiply is for digital logic schematics.

---

## 2. What Wokwi does (reference facts)

### 2.1 File format

```json
{
  "version": 1,
  "author": "Ken Pettit",
  "editor": "wokwi",
  "parts": [
    { "type": "wokwi-gate-and-2", "id": "and1", "top": 749, "left": 297.4, "rotate": 270, "attrs": {} },
    { "type": "wokwi-flip-flop-dsr", "id": "state_reg_2", "top": 931.2, "left": 163.2, "attrs": {} },
    { "type": "wokwi-text", "id": "text2", "top": 892.8, "left": 192, "attrs": { "text": "Head" } }
  ],
  "connections": [
    [ "ttout:EXTOUT0", "sevseg1:A", "green", [ "h21.01", "v-28.8", "h96" ] ],
    [ "pwr1:VCC", "sw1:8a", "red", [ "v0" ] ]
  ],
  "dependencies": {}
}
```

Rules observed:

- Part keys are written in the order `type, id, top, left, rotate, attrs`. `rotate` is omitted when 0. `attrs` is always present, `{}` when empty. An optional `hide: true` exists.
- `top`/`left` are the part's unrotated top-left corner in CSS pixels (96 dpi). Numbers are written with at most 2 decimals, integers without a decimal point.
- A connection is `[from, to, color, path]`. Pin references are `"partId:PIN"`. Color is a CSS color name (`green`, `limegreen`, `gold`, `violet`, ...) or `""` to hide the wire.
- `path` is a list of `"h<px>"` / `"v<px>"` moves starting at the source pin. An optional `"*"` splits source-side moves from target-side moves (target-side moves are applied in reverse from the target pin). Whatever gap remains is closed by Wokwi with orthogonal legs, by a fixed rule (3.5). Your design uses no `"*"` and wires are source-anchored, which is what Wokwi's editor writes.
- The part `id` is the name: Wokwi's Verilog export uses it as the instance name (`dffsr_cell state_reg_2 (...)`). So "naming a flop" is "editing its id", and ids must be legal Verilog identifiers.
- `wokwi-text` is the annotation part (`attrs.text`).

### 2.2 Grid and coordinates

- Grid pitch is 0.1 inch = 9.6 px. Fine grid (Alt/Ctrl while dragging) is 4.8 px. Shift disables snapping. `G` toggles grid display.
- In your design, 832 of 1024 part positions and 2024 of 2042 first wire segments sit exactly on the 9.6 px lattice. The logic-gate pins therefore sit on the grid relative to the part origin; the mm-based physical parts (7-segment, DIP switch) do not, which is where the `h21.01`-style fractional first segments come from.
- Rotation is 0 / 90 / 180 / 270, clockwise, about the center of the part's outline (CSS `transform-origin: center`). Your design has 96 parts at 90° and 48 at 270°, all rendered correctly.

### 2.3 Editor behavior and keys (from the Wokwi docs)

| Action | Wokwi |
|---|---|
| Add part | blue `+` button or `A`; part appears at (0,0) |
| Select / multi-select | click; Shift+click adds |
| Move | drag; arrow keys nudge (Shift+arrows = larger step) |
| Rotate 90° CW | `R` |
| Duplicate | `D` |
| Delete | `Delete` |
| Copy / Paste | Ctrl/Cmd+C, Ctrl/Cmd+V; wires between copied parts are copied too |
| Undo / Redo | Ctrl+Z / Ctrl+Y |
| Zoom | `+` / `-`, mouse wheel |
| Fit | `F` |
| Draw wire | click source pin, click bend points, click target pin; Esc or right-click cancels |
| Delete wire | select, then trash icon or `Delete`; double-click also deletes |
| Wire color | `0` black, `1` brown, `2` red, `3` orange, `4` gold, `5` green, `6` blue, `7` violet, `8` gray, `9` white, `C` cyan, `L` limegreen, `M` magenta, `P` purple, `Y` yellow. Works while drawing or with a wire selected |
| Default wire color | black from GND pins, red from VCC pins, green otherwise |
| Edit attrs / id | select part, use the small toolbar that appears above it (rotate / edit / delete) |

### 2.4 Logic parts, pin names, Verilog cells

Pin names below were read directly from your design's connections; the Verilog mapping is from `cells.v` and Wokwi's export.

| Wokwi part type | Pins | Verilog cell (cells.v) |
|---|---|---|
| `wokwi-gate-and-2` | A, B, OUT | `and_cell(a,b,out)` |
| `wokwi-gate-or-2` | A, B, OUT | `or_cell` |
| `wokwi-gate-xor-2` | A, B, OUT | `xor_cell` |
| `wokwi-gate-nand-2` | A, B, OUT | `nand_cell` |
| `wokwi-gate-nor-2` | A, B, OUT | `nor_cell` |
| `wokwi-gate-xnor-2` | A, B, OUT | `xnor_cell` |
| `wokwi-gate-not` | IN, OUT | `not_cell(in,out)` |
| `wokwi-gate-buffer` | IN, OUT | `buffer_cell` |
| `wokwi-mux-2` | A, B, SEL, OUT | `mux_cell(a,b,sel,out)`, `out = sel ? b : a` |
| `wokwi-flip-flop-d` | D, CLK, Q, NOTQ | `dff_cell(clk,d,q,notq)` |
| `wokwi-flip-flop-sr` | S, CLK, R, Q, NOTQ | none in `cells.v` (Wokwi simulates it; not exportable) |
| `wokwi-junction` | J | none; a net junction (Wokwi beta part, prefix `j`) |
| `board-tt-block-input-8` | IN0..7 / EXTIN0..7 (no clock or reset) | `ui_in[7:0]` |
| `wokwi-flip-flop-dr` | D, CLK, R, Q, NOTQ | `dffr_cell` (async reset) |
| `wokwi-flip-flop-dsr` | D, CLK, S, R, Q, NOTQ | `dffsr_cell` (async set/reset, reset wins) |
| `board-tt-block-input` | IN0..IN7, CLK, RST_N (design side); EXTIN0..7, EXTCLK, EXTRST_N (stimulus side); attr `verilogRole: input` | `ui_in[7:0]`, `clk`, `rst_n` |
| `board-tt-block-output` | OUT0..7 (design side); EXTOUT0..7 (display side); attr `verilogRole: output` | `uo_out[7:0]` |
| `board-tt-block-bidirectional-io` | IN, OUT, OE; attrs `verilogRole: bidirectional`, `verilogBit: n` | `uio_in[n]`, `uio_out[n]`, `uio_oe[n]` |
| `wokwi-vcc` / `wokwi-gnd` | VCC / GND | constant `1'b1` / `1'b0` |
| `wokwi-clock-generator` | CLK; attr `frequency` (Hz, e.g. `"10000"`) | stimulus only |
| `wokwi-pushbutton` | 1.l, 1.r, 2.l, 2.r; attrs `color, label, bounce, key` | stimulus only |
| `wokwi-slide-switch` | 1, 2, 3; attr `value` | stimulus only |
| `wokwi-dip-switch-8` | 1a..8a, 1b..8b | stimulus only |
| `wokwi-7segment` | A..G, DP, COM.1, COM.2; attrs `common, color` | display only |
| `wokwi-led` | A, C; attr `color` | display only |
| `wokwi-resistor` | 1, 2; attr `value` | treated as a short |
| `wokwi-text` | none; attr `text` | annotation |
| `wokwi-logic-analyzer` | D0..D7, GND; attrs channel names, buffer size, trigger, filename | capture to VCD in simulation |
| `wokwi-pi-pico` | GP0..GP28, GND.n, 3V3, VSYS, VBUS, RUN, ... (take the exact list from Wokwi's `wokwi-pi-pico` reference) | place and wire only; no firmware simulation |

The EXT* pins are the "outside the chip" side of the TT blocks. Wokwi's Verilog export ignores everything on the EXT side; it only exists for simulation stimulus and display. Chiply does the same.

### 2.5 Verilog export shape and the Tiny Tapeout flow

Wokwi's exporter (`https://wokwi.com/api/projects/<id>/verilog`) produces, for your design, a 6061-line module `tt_um_wokwi_414123795172381697` with ports `ui_in, uo_out, uio_in, uio_out, uio_oe, ena, clk, rst_n`, one `wire netN` per net, constants for VCC/GND nets, `assign uo_out[k] = netN`, and one cell instance per part named by the part id. Unconnected outputs are left empty (`.notq ()`) under a `PINCONNECTEMPTY` lint-off. Tiny Tapeout's `tt_tool.py` fetches exactly this file plus `cells.v` and `diagram.json`.

Consequence: if Chiply writes an equivalent `.v`, a Chiply design can be submitted to Tiny Tapeout as `language: Verilog` with sources `[tt_um_<name>.v, cells.v]`, with no Wokwi upload at all. That is also the path for designs that use custom blocks, which Wokwi can never load.

### 2.6 Reference design statistics

| | |
|---|---|
| Parts | 1024 (325 AND, 230 OR, 207 DFF, 62 MUX, 49 DFF-SR, 44 text, 34 GND, 27 XOR, 23 NOT, ...) |
| Wires | 2042; longest path 11 segments; 825 single-segment |
| Colors used | green, limegreen, magenta, black, red, blue, gold, white, orange, violet |
| Named parts | `state_reg_0..2`, `loop_reg_0..2`, `loop_active_reg`, `debug_reg`, `cmp_eq_reg`, `auto_clear`, `split_cnt` |

This is the performance target: the editor must stay smooth at this size.

### 2.7 Where the geometry comes from (resolved in M2)

- **Logic parts** (gates, buffer, MUX, the four flip-flop variants, VCC, GND, clock generator, junction, logic analyzer, Pico): pin positions and outline sizes come from the part definitions in Wokwi's public diagram-editor JavaScript, where they are given in millimetres (96/25.4 px per mm). Only these numbers are used; the artwork is Chiply's own.
- **Tiny Tapeout blocks**: pins and sizes from the `board.json` files in the public `wokwi-boards` repository. That repository has no license file, so again only coordinates are used and Chiply draws its own block art.
- **Physical parts** (pushbutton, slide switch, DIP switch, resistor, LED, 7-segment): `pinInfo` and SVG sizes from `wokwi-elements` (MIT). The 7-segment element places its pins with 3.78 px/mm while its outline uses 96/25.4; Chiply reproduces that quirk.
- **Rotation**: Wokwi applies `transform: rotate(Ndeg)` with the default CSS origin, i.e. about the center of the element's layout box, whose size the browser rounds to whole pixels (a gate's 37.8 px height becomes 38). Chiply uses the rounded size for the pivot.
- **Verification**: a unit test routes all 2042 wires of the reference design and checks the gaps Wokwi leaves after the recorded bends; 95.5 % are whole half-grid steps (the rest involve the 7-segment and DIP switch, which are off-grid in Wokwi too). A wrong pin or pivot would break this immediately.
- **Wokwi's own ERC**: the editor has a small electrical rule check with four issues: input pin not driven, multi-driven net, short circuit (VCC tied to GND), clock driven by combinational logic. It uses the same per-pin direction table that 3.7 describes. Chiply's DRC (5.2) covers all four.

---

## 3. Architecture

### 3.1 Tech stack

| Component | Choice | Why |
|---|---|---|
| Language | C++20 | |
| GUI | Qt 6 (Widgets, Gui, Svg, Core), Graphics View framework | Mature scene graph with item indexing, zoom/pan, item selection; right fit for ~1k parts / 2k wires |
| Build | CMake ≥ 3.21 + Ninja, dependencies via FetchContent | Already installed here (CMake 4.3.2) |
| JSON | `nlohmann::ordered_json` | Preserves key order and lets us control number formatting; `QJsonObject` sorts keys alphabetically, which would rewrite every Wokwi file |
| Tests | Catch2 v3 | |
| Simulation | Built-in event-driven simulator (plain C++, part of Chiply; section 6). Verilator is an optional extra backend later (M9), never required | |
| Platforms | macOS (your Intel Mac first), Linux; Windows later | |

Licensing: Qt open source (LGPLv3, dynamic link), `wokwi-elements` art (MIT, keep attribution), `cells.v` (Tiny Tapeout, Apache-2.0). Chiply itself is BSD 3-Clause (decided; `LICENSE` is already in the repo). BSD 3-Clause is compatible with the MIT `wokwi-elements` art and the Apache-2.0 `cells.v`, both of which keep their own notices.

### 3.2 Layering and repository layout

```
chiply/
  CMakeLists.txt
  cmake/                    # FindVerilator.cmake, deps.cmake
  src/core/                 # plain C++20 + nlohmann/json, no Qt at all
    Document.h/.cpp         # parts, wires, metadata, dirty tracking, change signals
    Part.h  Wire.h  PinRef.h
    PartLibrary.h/.cpp      # loads part definitions (JSON) -> PartType registry
    SymbolDef.h             # declarative symbol: strokes, paths, pins, label anchors
    Geometry.h/.cpp         # grid, rotation transform, pin world position
    WirePath.h/.cpp         # h/v/"*" mini-language <-> polyline, normalization
    WokwiJson.h/.cpp        # load/save with fidelity rules (3.6)
    IdRemapper.h/.cpp       # renumber pasted ids into a target document's namespace (4.7)
    Netlist.h/.cpp          # pins -> nets (union-find)
    Drc.h/.cpp              # switchable checks over the netlist; produces Violations with locations (5.2)
    VerilogWriter.h/.cpp    # cells.v-style netlist, TT wrapper
  src/sim/
    Netlist.h/.cpp          # (shared with export) nets, drivers, loads, switch groups
    Kernel.h/.cpp           # event queue, 4-state values, delta cycles, primitives
    Board.h/.cpp            # stimulus/display parts: TT blocks, buttons, switches, clock, LED, 7-seg
    Vcd.h/.cpp              # trace writer (logic analyzer, probes)
    SimBackend.h            # co-simulation interface: owned pins, advance(dt), exchange pin states;
                            # the built-in kernel is the first backend, optional Verilator (M9) and a
                            # future RP2040 emulator plug in the same way
    Stimulus.cpp            # buttons, switches, clock generator -> input nets
  src/ui/
    MainWindow, DocumentTabs (QTabWidget), EditorSession (one open file: Document + SchematicScene + SchematicView + QUndoStack + selection + view state)
    SchematicView (QGraphicsView), SchematicScene
    PartItem, WireItem, PinItem, VertexHandle, SegmentHandle, PartToolbar
    tools/  SelectTool, PanTool, WireTool, PlaceTool   (one active interaction state machine)
    PartPalette (the "+" dialog), Inspector (id/attrs dock), ViolationsPane (right dock, click-to-navigate), SimPanel
    commands/  QUndoCommand subclasses (Add, Delete, Move, Rotate, Reroute, Recolor, Rename, SetAttr, Paste)
    Clipboard.cpp           # Wokwi-JSON fragment in/out; IdRemapper (core) renumbers ids into the target document
  src/cli/chiply-cli.cpp    # headless: export-verilog, check, run-truthtable
  resources/parts/*.json    # part definitions incl. calibrated pin offsets
  resources/symbols/        # logic symbols (path data) + wokwi-elements SVGs
  resources/verilog/cells.v
  tests/                    # unit tests + fixtures (reference/ files)
  reference/                # already present
  docs/
```

The core library has no dependency on the GUI, so the CLI and the tests exercise the same code the editor uses.

### 3.3 Core data model

```cpp
struct Pin      { QString name; QPointF pos; Side side; PinDir dir; };      // part-local px, rotation 0
struct PartType { QString type; QVector<Pin> pins; SymbolDef symbol;
                  AttrSchema attrs; CellMapping verilog; Category category; };

struct Part     { QString id, type; double top = 0, left = 0; int rotate = 0;
                  ordered_json attrs; bool hide = false; ordered_json unknownKeys; };

struct Seg      { enum Axis { H, V } axis; double len; };
struct Wire     { PinRef a, b; QString color; QVector<Seg> path; int starIndex = -1; };

struct Document { QString author, editor; int version = 1;
                  QVector<Part> parts; QVector<Wire> wires;
                  ordered_json dependencies, unknownTopLevel; };
```

Chiply-only data (net labels, custom block library paths, view state) goes in a sidecar `name.chiply.json` next to the diagram, never inside `diagram.json`, so the Wokwi file stays byte-for-byte what Wokwi would write (decided).

### 3.4 Geometry and rotation

- Scene units are Wokwi pixels; 1 grid = 9.6 px. Rendering scales with the view transform only.
- Pin world position = part origin + Rotate(rotate, pivot) · pinLocal. The pivot is the outline center, implemented once in `Geometry.cpp`.
- Snapping: parts snap by their first pin, not their corner (done), so parts whose pins sit between grid lines relative to their outline (junction, Tiny Tapeout blocks) still get pins on the grid; for gates and flip-flops this is the same as snapping the corner. Earlier wording: parts snap by their origin; wire vertices snap to the grid; Shift disables snapping; Alt/Ctrl uses the 4.8 px fine grid. Pins are not snapped (they are where the symbol puts them), which is exactly why Wokwi writes a fractional first segment for mm-based parts.

### 3.5 Wires

- A wire's polyline is computed exactly as Wokwi's editor does it (read from its code): endpoints rounded to 2 decimals; source moves walked from the source pin; without `"*"`, the remaining gap is closed on the axis of the last move first (horizontal if there are no moves), then the other axis; with `"*"`, the target moves are walked from the target pin starting with the last item, and if both axes still differ one leg on the axis of the first move after `"*"` joins the two halves. Finally, consecutive moves on the same axis are merged by adding them, so a recorded overshoot plus the closing step back collapse into one move (this is what keeps Wokwi's wires from overshooting). Corners are drawn with a 4 px radius. Implemented in `routePolyline()`; a unit test compares all 2042 wires of the reference design against the paths wokwi.com actually rendered (captured with headless Chrome into `reference/*.rendered_wires.json`).
- Moving the source part moves the whole route (path is relative to the source pin); moving the target part only stretches the tail. This mirrors Wokwi exactly, so what you see is what Wokwi will show.
- Edits produce a new `path`; normalization merges collinear and zero-length segments and keeps everything on grid. If a loaded wire has a `"*"`, Chiply keeps the `"*"` form untouched until the wire is edited, then writes the source-anchored form.
- Manhattan only: all segments are horizontal or vertical, like Wokwi's mini-language.

### 3.6 Save fidelity rules

1. Load → save with no edits is JSON-equal to the input (test on all three reference files).
2. Part and connection order preserved; key order `type, id, top, left, rotate, attrs`; `rotate` only when non-zero; `attrs` always present.
3. Number formatting: integer → no decimal point; otherwise round to 2 decimals and strip trailing zeros.
4. Unknown top-level keys (`serialMonitor`, future ones), unknown part attrs and unknown part types are preserved verbatim. Unknown part types render as a gray box with pins inferred from their connections, so a file with parts Chiply does not know still opens and saves safely.
5. New ids follow the Wokwi convention `<prefix><n>` (`and326`, `flop245`, `mux63`), next free number per prefix. Ids are unique per document; each open file is its own id namespace, and anything that enters a document from outside (paste, duplicate, import) goes through the renumbering rules in 4.7.

### 3.7 Part library and symbols (declarative)

Each part type is a JSON file. Besides geometry and the symbol, every pin carries its electrical direction; this is the metadata the DRC, the netlister and the simulator all key off.

```json
{
  "type": "wokwi-gate-and-2",
  "category": "Logic",
  "label": "AND Gate",
  "size": [W, H],
  "pins": [
    { "name": "A",   "x": 0,  "y": Y_A, "side": "left",  "dir": "in"  },
    { "name": "B",   "x": 0,  "y": Y_B, "side": "left",  "dir": "in"  },
    { "name": "OUT", "x": W,  "y": Y_O, "side": "right", "dir": "out" }
  ],
  "symbol": { "strokes": [ { "path": "M ... Z", "stroke": "#c800c8", "width": 2 } ], "leads": true },
  "attrs": {},
  "verilog": { "cell": "and_cell", "ports": { "A": "a", "B": "b", "OUT": "out" } }
}
```

`side` is where the pin sits on the symbol; `dir` is what it does electrically:

| `dir` | Meaning | Netlist / DRC role |
|---|---|---|
| `in` | consumes a signal | needs exactly one driver on its net (unconnected-input check) |
| `out` | drives a signal | counts as a driver (multiple-driver check) |
| `inout` | bidirectional (custom block ports declared so) | counts as a driver; flagged in multiple-driver messages as "bidirectional" so you can judge |
| `power` | VCC / GND | driver of a constant |
| `passive` | contact with no direction (resistor ends, switch and button contacts) | transparent: the netlister merges the nets on both sides when the part is closed or is a resistor; never a driver |
| extra flag `"clock": true` on an `in` pin | clock input | draws the clock triangle; the simulator and Verilog writer use it for `clk` |

Directions for the parts in 2.4, from the design's point of view:

| Part family | `in` | `out` | other |
|---|---|---|---|
| Gates, buffer, NOT | A, B, IN | OUT | |
| MUX | A, B, SEL | OUT | |
| Flip-flops | D, CLK (clock), S, R | Q, NOTQ | |
| `board-tt-block-input` | EXTIN0..7, EXTCLK, EXTRST_N (fed by stimulus) | IN0..7, CLK (clock), RST_N (they drive the design) | |
| `board-tt-block-output` | OUT0..7 | EXTOUT0..7 (feed displays) | |
| `board-tt-block-bidirectional-io` | OUT, OE | IN | |
| VCC, GND | | | VCC, GND = `power` |
| Clock generator | | CLK | |
| Pushbutton, slide switch, DIP switch, resistor | | | all contacts `passive` |
| LED, 7-segment | A, C, A..G, DP, COM.* (loads) | | |
| Text | | | no pins |
| Custom blocks (section 7) | ports declared `in` | ports declared `out` | ports declared `inout`; `clock: true` per port |

The Inspector's pin list shows the direction with an arrow icon, and the pin tooltip reads e.g. `D (input, clock)`. Unknown part types (3.6) have no direction metadata, so their pins are treated as `passive` and reported by the `unknown-part` check.

Calibrated numbers replace the placeholders. Physical parts reference an SVG from `wokwi-elements` and carry that repo's `pinInfo` verbatim, with the direction column added by hand. Custom blocks (section 7) are the same schema, generated from `block.json`.

### 3.8 Rendering and performance

- `PartItem` paints from the cached `QPainterPath`s of its symbol; `ItemCoordinateCache` makes 1k parts cheap. `WireItem` is a path item whose bounding rect is the route; the scene's BSP index handles hit testing.
- Readability: hover text defaults to 18 pt and View → Hover Text Size offers 13 / 18 / 24 / 30 pt (persisted). Later UI text (pin labels, part names, Violations pane, Inspector) gets the same kind of size setting rather than fixed small fonts.
- Hover (done): a dotted outline around the part under the cursor and its id as tooltip; on a pin, a blue pin marker and an immediate `part:PIN` tooltip, as in Wokwi. Wires only react within a few pixels of the drawn line, so they never steal hover from parts they loop around. Selected part: blue outline plus the floating mini toolbar (rotate / edit / delete).
- Wire colors are the CSS names from the file, mapped through `QColor(name)` (Qt knows the SVG color names, so `limegreen`, `gold`, `violet` just work).
- **Themes**: a sun / moon button at the right end of the toolbar shows the current mode (sun = light, moon = dark; distinct shapes, outlined) and switches it with one click (done). View → Theme also offers System (follow macOS), Light and Dark; the choice is remembered in preferences; `--theme` overrides it for one run. Light is the Wokwi look (white canvas, magenta symbols, black leads). Dark matches Wokwi's dark mode (see below). The window chrome follows the same choice. All canvas colors come from one `CanvasColors` table, so every item type added later (symbols, wires, handles, violation highlights) reads its colors there instead of hard-coding them.
- Wire colors are drawn exactly as named in the file in both themes, as Wokwi does (`green` stays #008000 and `black` stays black on the dark canvas). The dark canvas uses Wokwi's dark palette: background #333333, leads #aaaaaa, symbol outlines #d478e2, text #cccccc.
- While dragging, only the moving items and their attached wires repaint; everything else is static. Target: 60 fps panning at the reference size on your machine.

---

## 4. UI specification (Wokwi parity, plus the extras)

### 4.1 Window layout

Menu bar; a top toolbar with `+` Add part, zoom controls, Fit, Grid, Run/Pause/Step (sim); a tab bar with one tab per open file (4.10); the schematic canvas of the active tab in the center; on the right, two stacked docks: the Inspector (id, type, attrs, pin list with directions) that updates with the selection, and the Violations pane (5.2) whose entries navigate the canvas when clicked; a bottom dock for the simulation console and waveforms; a status bar with the cursor's x, y (diagram px), the mode, the DRC counts, the selection and the zoom. Moving parts, drawing a wire, dragging wire handles, placing or pasting parts and the marquee all scroll the canvas when the cursor nears or passes its edge, and the carried item keeps following the cursor. Open and Save start in the folder used last (a preference). White canvas, light-gray 0.1 inch dot grid, Wokwi-like.

### 4.2 Navigation

| Gesture | Action |
|---|---|
| Mouse wheel / trackpad scroll | Zoom, centered on the cursor |
| Shift + wheel | Pan horizontally; Ctrl/Cmd + wheel also pans vertically |
| Left-drag on empty canvas, middle-drag, or Space + left-drag | Pan, as in Wokwi (changed 2026-10-03) |
| Shift + left-drag | Marquee select, as in Wokwi (4.3) |
| Arrow keys | With parts selected: move them one grid step (Shift: five steps), as in Wokwi. With nothing selected: move the diagram in the arrow's direction by a tenth of the viewport (Shift: a full viewport). Ctrl/Cmd+arrows always pan |
| `+` / `-`, `F` | Zoom in/out, fit to contents |
| `G`, Shift, Alt/Ctrl | Grid toggle, snap off, fine snap |

### 4.3 Selecting objects

Selectable objects are parts (gates, flops, TT blocks, switches, displays, power symbols), text annotations, and wires. One selection model serves all of them.

- **Click** on an object selects it alone, except in a pin's hit region (4.5 px around the pin, where the blue pin marker shows): a click there never selects the part, it starts a new wire from that pin (4.6, milestone M5). Click on empty canvas clears the selection. `Esc` clears it too. Ctrl/Cmd+A selects everything.
- **Shift+click** toggles an object in or out of the current selection (Wokwi's multi-select). Shift+press followed by a drag of more than 4 px pans instead, so the two never conflict.
- **Click on a wire** selects the wire and shows its editing handles (4.6). Wires are hit-tested with a tolerance of a few screen pixels so thin wires are easy to grab at any zoom.
- **Marquee**: Shift+drag (as in Wokwi; it may start on a part), or Ctrl/Cmd+drag on empty canvas to add; a translucent blue rubber-band rectangle follows the cursor. On release, every part and text annotation whose bounding box is fully inside the rectangle is selected, and every wire whose entire route is inside is selected. Alt while releasing switches to "crossing" mode (anything the rectangle touches), which is handy for grabbing long wires. Ctrl/Cmd+marquee adds the enclosed items to the selection. A plain drag on empty canvas pans, as in Wokwi. Dragging past the viewport edge auto-scrolls so a marquee can cover more than one screen.
- **Implicit wires**: a wire whose two ends are both on selected parts is treated as part of the selection for move, copy, delete and duplicate even if it was not explicitly selected. It is drawn highlighted so you can see what will travel with the parts. A wire with only one end on a selected part is drawn dashed to warn that it will stretch.
- **Feedback**: selected parts get a blue outline; a single selected part also shows Wokwi's mini toolbar (rotate / edit / delete) above it; a multi-selection shows a dotted group bounding box with the item count. The Inspector shows the one part's id and attrs, or a summary ("12 parts, 9 wires") with the operations that apply to all of them (color, delete, rotate).
- Selection survives zoom and pan, and is cleared when the document is reloaded.

### 4.4 Moving objects by click-drag

- **Start**: press the left button on any part or annotation and move more than 4 screen pixels. If the object was not already selected it becomes the sole selection (or joins it with Shift), so a single click-drag on an unselected gate just moves that gate, and a click-drag on any member of a marquee selection moves the whole selection. No "move tool" is needed; this matches Wokwi.
- **Rigid group move**: all selected parts, annotations and implicit wires move as one body. The grabbed part's origin snaps to the 9.6 px grid and the same delta is applied to every other selected object, so relative positions and all internal wire routes are preserved exactly. Shift disables snapping; Alt/Ctrl uses the 4.8 px fine grid; fine snapping of the grabbed part still moves the whole group by one delta.
- **Selected wire segments** (added 2026-10-04): when a marquee fully encloses some segments of a wire it does not select as a whole, those segments are selected too (drawn with the selection halo). Moving the selection by a plain translation (drag or arrow keys) moves their corners rigidly with the parts, together with wire ends on moving parts; only the unselected stretches in between are re-routed elastically. So for a column of flops whose outputs run through a short horizontal, a vertical and then a long horizontal, enclosing the short and vertical pieces and moving left or right stretches only the long horizontals. The selected segments follow repeated moves and undo (they are kept relative to a selected part), and are dropped when the selection is cleared or the design changes structurally. Rotation keeps the plain elastic rule.
- **Attached wires** (done, differs from Wokwi by design): a wire with one end on a moving part keeps its route; only the segment nearest that part along the move direction stretches. For a horizontal move the first horizontal segment from the part stretches and any vertical segments before it (e.g. one leaving the pin at 90°) slide sideways with the part; likewise for vertical moves. A wire with no segment on that axis (a straight perpendicular run) gets a jog half-way. Wires with both ends moving move rigidly. The new routes are saved as ordinary Wokwi paths and belong to the same undo step. Applies to drags (live), arrow nudges and rotation. Wokwi itself translates the recorded path with the source part and stretches only the final leg, which makes long runs shift and overlap.
- **Cursor and feedback**: the cursor changes to a move cursor; the group box and the Inspector's position fields update while dragging; dragging near a viewport edge auto-scrolls.
- **Cancel and commit**: `Esc` during a drag puts everything back. Release commits the move as a single undo step (one Ctrl+Z reverts the whole group), and the mini toolbar reappears if one part is selected.
- **Alt+drag** (or Ctrl+drag on Linux/Windows) duplicates the selection and drags the copy, using the paste mechanism in 4.7 (new ids, routes preserved). This is the fastest way to build the repeated rows your reference design is made of.
- **Keyboard moves** (done): with parts selected, arrow keys move them one grid step and Shift+arrows five, with the same wire behavior and one undo step per press (a held key's repeats merge into one step). With nothing selected the arrows pan (4.2).
- **What is not a move**: dragging a wire's vertex or segment handle edits that wire (4.6); dragging a wire's body does nothing in Wokwi and does nothing here, except when the wire's parts are selected and it moves with them. Dragging a pin starts a new wire (4.6), never a move; the pin hit area wins over the part body within a few pixels of the pin.
- **Performance**: during a drag only the moving items and their attached wires are re-laid-out and repainted, incrementally per mouse-move; nothing else in the scene is touched, which keeps a 100-part group drag smooth on the 1024-part reference design.

### 4.5 Parts

- **Add**: `+` or `A` opens a searchable palette with categories Logic, Tiny Tapeout, Input, Output, Power, Annotation, Custom. Choosing a part attaches it to the cursor; click places it (Wokwi drops at (0,0); attaching to the cursor is the one deliberate improvement here). Esc cancels.
- **Select and move**: sections 4.3 and 4.4.
- **Rotate / duplicate / delete**: `R`, `D`, `Delete` as in 2.3, applied to the whole selection; rotating a multi-selection rotates each part about its own pivot (Wokwi semantics), with a menu option to rotate the group about its center.
- **Mini toolbar** above a selected part with rotate, edit, delete icons, as Wokwi shows.
- **Naming**: the id is shown as a small label next to the part (toggle "Show names" globally; Wokwi hides ids except in the edit dialog). Rename by F2, double-click on the label, or the Inspector. Validation: unique, matches `[A-Za-z_][A-Za-z0-9_]*`, rejects Verilog keywords. Renaming rewrites every wire that references the id (done, one undo step). Anything else that stores part ids must follow renames too: the sidecar's net labels, DRC waivers and per-part notes (5.2), and clipboard fragments are unaffected because they carry their own ids.
- **Attributes**: the Inspector shows the attrs defined by the part's schema (`frequency`, `label`, `key`, `verilogBit`, `text`, ...) with proper editors, and a raw JSON view for anything unknown.

### 4.6 Wires

- **Drawing**: click a pin (anywhere in its hit region, which takes priority over selecting the part), each further click adds a bend, click a pin to finish; Esc or right-click cancels. The pending segment previews as an L-bend that follows the cursor; the preview's elbow orientation flips when the cursor crosses the diagonal, which is how Wokwi's editor feels. A net with three ends is normally two wires from the same pin. Wokwi also has a beta `wokwi-junction` part (a dot with one pin `J`); Chiply supports it as a part (a click on a junction starts a wire from it, a drag moves it, since the whole junction is its pin), and a later option can drop a junction where a wire is started from the middle of another wire.
- **Handles stay reachable** (done): if a segment's midpoint is off-screen but part of the segment is visible, its yellow handle slides along the segment to the nearest visible point, 24 px inside the window edge, and follows scrolling and zooming. Long wires can be adjusted without zooming out to find the handle (a pain point in Wokwi).
- **Editing a selected wire**: yellow segment handles with a dark outline (done; Wokwi uses small purple dots, Chiply makes them larger and yellow for visibility) and round handles on every vertex (drag to move the corner; neighbors stay orthogonal), bar handles at the middle of each segment (drag to slide that segment perpendicular), a trash icon, and Delete. Double-click on a wire deletes it, exactly as in Wokwi (done). Orange square handles sit on every corner: dragging one moves the corner and both attached segments stay orthogonal (a pin neighbour gets a jog) (done). Ctrl/Cmd+drag on a segment splits it at that point and pulls the far half sideways into a step, the split sliding along with the cursor; a Ctrl/Cmd+click without dragging still toggles selection (done). This is how "Ctrl+click adds a vertex" works for orthogonal wires, where a lone vertex on a straight line would have no effect. On macOS it is Cmd, because Qt maps Cmd to its Ctrl modifier and the OS turns a physical Ctrl+click into a right-click. With several wires selected, color keys and Delete apply to all of them.
- **Colors**: the key map in 2.3, active while drawing or with wires selected; default color by source pin function. Also a color swatch popup on the wire toolbar.
- **Re-anchoring** (done): a selected wire shows cyan handles on its ends; dragging one previews the wire stretching elastically to the cursor, dropping it on another pin reconnects the wire there (one undo step), dropping it anywhere else puts it back. Handles on a selected wire take priority over the pin underneath, so grabbing an end never starts a new wire.

### 4.7 Copy, paste, duplicate, clipboard

- Copy takes the selected parts and annotations plus the implicit wires (4.3) and puts a Wokwi-format JSON fragment `{ "parts": [...], "connections": [...] }` on the system clipboard as text. Because paths are relative to the source pin, copying preserves routes with no transformation; paste only translates `top`/`left` by the paste offset (cursor position, or +1 grid for Ctrl+V in place), which is the only x/y transform ever applied.
- **Id renumbering on paste.** The clipboard fragment carries the ids from wherever it was copied (this file, another tab, a `diagram.json` in a text editor), and those ids usually already exist in the target. Paste therefore runs every incoming part through `IdRemapper` against the target document before anything is inserted:
  1. Auto-generated ids of the form `<prefix><n>` (`flop1`, `and2`, `mux7`, `text3`, `gnd4`, `pwr2`) are always renumbered: each becomes `<prefix><next free n>` in the target, allocated in the order the parts appear in the fragment so a pasted row stays in sequence (`and1..and8` → `and326..and333`). This happens even when the old number is free, so pasted parts are always the newest numbers and never interleave with existing ones.
  2. User-given names (anything not matching `<prefix><n>` for a known prefix, e.g. `state_reg_2`, `auto_clear`) are kept if free in the target; on a clash they get the lowest free numeric suffix (`state_reg_2` → `state_reg_2_1`, then `_2`, ...). An option in Preferences turns this into "always suffix pasted names" for people who want pasted copies visibly marked.
  3. Collisions inside the fragment itself (two parts with the same id, possible when pasting hand-edited JSON) are resolved the same way, and the fragment's own names never block each other from reusing an id that the first occurrence vacated.
  4. Every connection in the fragment is rewritten with the old → new id map, so wires stay attached to the right pins. Connections that reference an id not present in the fragment (copied from the middle of a net) are dropped and reported in the bottom dock.
  5. The map is applied once, atomically, inside a single `Paste` undo command. The Inspector shows "pasted 14 parts, 9 renamed" and the renamed list, so you can see what became what.
  Duplicate (`D`, Alt+drag) uses the same path, so a duplicated `state_reg_0` becomes `state_reg_0_1` and its `and17` becomes the next free `and<n>`.
- The pasted objects become the selection, attached to the cursor until you click, so paste flows straight into a click-drag move.
- Because the clipboard text is valid Wokwi JSON, you can also paste fragments copied from a `diagram.json` in a text editor straight into the canvas, and vice versa.
- `D` duplicates the selection with the same mechanism; Alt+drag (4.4) is duplicate-and-move.
- **Paste name format** (added 2026-10-04): a "Paste names" field on the toolbar (a saved preference) such as `r#_*`, where `#` is the number to step and `*` any text. Paste, Duplicate and Alt-drag rename the ids that match by stepping that number, all by the same step, the smallest that frees every one of them: copying register `r1`'s bank (`r1_b31`, `r1_b31_n1`, ...) pastes `r2_b31`, `r2_b31_n1`, ... Ids that do not match follow the rules above; a format without exactly one `#` is shown in red and ignored.
- **Find and Replace in Names** (Edit menu, Ctrl+Shift+F): substring replacement in the selected parts' ids, with a live preview (old -> new), refusing names that would collide with other parts or are not Verilog names; wires follow; one undo step.

### 4.8 Undo / redo

Multi-level, unlimited undo and redo per tab (done): every model change is a `QUndoCommand`; a drag is one step, a held arrow key's repeats merge into one step. Ctrl+Z / Ctrl+Y / Cmd+Shift+Z. View → Undo History lists every step of the active tab; clicking an entry jumps back or forward to that point.

### 4.8b Window layout

The Inspector's width is set by dragging its splitter (minimum 220 px); its contents scroll, so selecting never changes it (done). The window's size and position, the toolbar and every dock (including that width) (Inspector, Undo History; later Violations) are saved with their position, size and visibility, and restored on the next launch (done). Saved on quit and shortly after any change, so a crash does not lose it.

### 4.9 Keyboard map

Pure Wokwi keys (decided): everything in 2.3 as is, including `R`, `D`, `A`, `F`, `G` and the color keys. The only additions are the ones decided above: arrow keys pan, Ctrl/Cmd+arrows nudge, Ctrl/Cmd+click adds a wire vertex, F8 steps through violations, Ctrl+Tab switches tabs. No alternate preset.

### 4.10 Multiple open files (tabs)

Chiply is a multi-document editor so a block can be developed and tested in its own small file, then copied into the large design.

- **One tab per file.** Each tab is an `EditorSession`: its own `Document`, scene, view (zoom, scroll position), selection, undo stack, DRC results and simulation state. Switching tabs switches all of those; nothing is shared except the clipboard, the part library and the preferences. Ctrl+Tab / Ctrl+Shift+Tab cycle tabs; Ctrl+W closes one; tabs can be reordered by dragging and torn off into a second window for side-by-side work on a wide monitor.
- **Opening**: File → Open, Recent Files, drag-and-drop of `diagram.json` files onto the window, and `chiply a.json b.json` on the command line all open tabs. Opening a file that is already open just activates its tab. File → New creates a blank untitled tab; File → New from Template (Ctrl+Shift+N) creates an untitled tab that starts as a copy of `template.json` (Tiny Tapeout's Wokwi template plus the eight bidirectional I/O blocks), built into Chiply, so no template file is opened or changed (Save As names the new design).
- **Modified state**: the tab title shows a `•` when unsaved; closing a dirty tab or quitting asks per file; Save All is on the File menu. Autosave writes `name.diagram.json.autosave` beside the file every few minutes and offers recovery on the next open.
- **Cross-tab copy and paste** is the normal copy/paste of 4.7: copy in the block's tab, switch tabs, paste. Ids are renumbered into the target file by the rules in 4.7, routes are preserved, and the pasted group arrives attached to the cursor for placement. Drag-and-drop of a selection from one tab onto another tab's title does the same in one gesture.
- **Block tabs and the TT blocks**: a block developed in its own file typically has its own `board-tt-block-input`/`output` for standalone simulation. Pasting into the main design would bring those along, so the paste dialog offers "skip Tiny Tapeout I/O blocks and the wires on them" (default on when the target already has them) and reports the dangling wires it dropped. The same filter applies to `wokwi-vcc`/`wokwi-gnd` only on request, since those are usually wanted.
- **Simulation per tab**: Run/Pause/Step act on the active tab; a tab that is simulating keeps running in the background with its tab title showing a running indicator, and the sim panel re-binds when you switch back.
- **Reference/library tabs**: a tab can be opened read-only ("Open as library") so you can copy from, say, the reference design without risk of saving changes into it.
- Later, section 7's hierarchical blocks will let a block file be instantiated as a single symbol instead of pasted flat; the tab model is the groundwork for that.

---

## 5. Netlist, DRC and Verilog export

### 5.1 Nets

Union-find over pin references joined by wires. `wokwi-resistor` is a short; switch and button contacts merge nets according to their state in simulation and are treated as shorts for DRC. VCC/GND parts make constant nets. The EXT* pins and everything hanging off them (switches, buttons, clock generator, LEDs, 7-segment) are the testbench side and are excluded from the chip netlist. Every net knows its driver pins and its load pins from the direction metadata in 3.7.

### 5.2 Design rule checks (DRC)

Each check has an id, a default severity and an on/off switch. Switches live in Preferences → DRC (global defaults) and can be overridden per document (saved in the sidecar file, so a scratch file can run with fewer checks than the tapeout design). The CLI takes the same ids: `chiply-cli check design.json --disable unconnected-input`.

| Check id | Default | Severity | What it reports |
|---|---|---|---|
| `unconnected-input` | on | warning | an `in` pin with no wire, or whose net has no driver (message says which); an undriven net that runs through a junction is reported by `unconnected-junction` instead |
| `unconnected-junction` | on | warning | a junction that nothing drives but that feeds inputs, reported once at the junction with the inputs it feeds; its own group in the Violations pane so the real unconnected inputs stand apart (modules whose junctions are hookup points for copy/paste or, later, off-page connectors), and it can be turned off on its own |
| `multiple-drivers` | on | error | a net with two or more driver pins (`out`, `inout`, `power`): two outputs tied together, an output tied to VCC/GND, etc. Lists every driver |
| `short-circuit` | on | error | VCC and GND symbols on the same net (Wokwi's "Short circuit") |
| `clock-from-logic` | on | warning | a flip-flop clock driven by combinational logic instead of a clock source or flip-flop output (Wokwi's "Clock driven by combinatorial logic") |
| `unconnected-output` | off | info | an `out` pin that drives nothing (harmless; the ASIC tools optimize it away) |
| `dangling-wire` | on | error | a connection naming a part or pin that does not exist (possible after hand-editing JSON) |
| `invalid-id` | on | error | id is not a legal Verilog identifier, is a keyword, or is duplicated |
| `unknown-part` | on | error | part type with no library definition (blocks export) |
| `tt-bidir-bit` | on | error | bidirectional block with a missing or duplicated `verilogBit` |
| `combinational-loop` | on | error | a cycle through gates/muxes with no flip-flop in it (lists the parts on the loop). Found in the reference design: `mux1`..`mux9` each have `OUT` wired back to their own `A` (should be the `Q` of the flip-flop their `OUT` drives, as for `mux10`..`mux12`), making nine level-sensitive latches |
| `stacked-parts` | on | warning | two parts of the same type at the same position (an invisible duplicate, typically from copy/paste). Found in the reference design: `ttio5` and `ttio8` are both bit 5 at (2126.4, 3332.22); only `ttio8` is wired, and Wokwi's export happened to use it |

Running: DRC runs live by default, incrementally on the parts and nets touched by each edit (debounced ~100 ms), so violations appear and disappear as you wire; the pane shows how much the last check covered. A "Full DRC" button does a full pass, and live checking can be switched off for very large edits. Export always runs a full pass first and stops on errors unless you choose Export Anyway (warnings only ask); simulation warns about errors and shows the pane but still runs, as Wokwi does.

**Violations pane** (right side, below the Inspector, following the active tab):

- A table with severity icon, check name, message, and location (part id, pin, or net with its member pins), grouped by check or by severity, sortable, with a text filter and a counter per severity that also appears in the status bar ("2 errors, 14 warnings").
- Checkboxes in the pane header toggle each check on or off for this document without opening Preferences, so turning off `unconnected-input` while a block is half-built is one click.
- **Click on a violation** snaps the canvas to it: the view centers on the offending item and zooms so the involved items fill the view with a margin (never below 100 %, never above 400 %), the item is selected so the Inspector shows it, and it flashes with a pulsing highlight for about a second, then stays highlighted until the selection changes. For a `multiple-drivers` violation the whole net lights up (all its wires and pins) and the view fits the net's bounding box; a "next driver" arrow in the row steps through the drivers one by one. For `unconnected-input` the pin itself is ringed.
- F8 / Shift+F8 step to the next / previous violation with the same snap behavior, so you can walk a long list with one hand.
- Right-click: "Waive this violation" (stored in the sidecar with an optional reason; waived rows move to a collapsed group and the counters exclude them), "Disable this check", "Copy message", "Copy all as text".
- Rows update in place as you fix things; a fixed violation disappears, a new one appears, and the selection in the pane is kept if its row still exists.

### 5.3 Verilog writer

Mirrors Wokwi's exporter: the TT port list, `default_nettype none`, `wire netN`, constants, `assign uo_out[k]`, `uio_out`/`uio_oe` per `verilogBit`, one cell instance per part named by its id, empty connections for unused outputs. Net numbering is deterministic (by first appearance in part order) so diffs stay small. Option: use Chiply net labels as net names instead of `netN`.

### 5.4 Equivalence test

Automated (developer machines with Icarus Verilog or Verilator installed; skipped elsewhere): compile Chiply's export and Wokwi's golden export of the reference design, drive both with the same random `ui_in`/`uio_in`/`rst_n` sequence for 100k cycles, compare `uo_out`/`uio_out`/`uio_oe` every cycle. This proves the netlister against your own 1024-part design before you trust it for a tapeout.

### 5.5 Export Tiny Tapeout project

Writes `src/tt_um_<name>.v`, `src/cells.v`, any custom-block `.v` files, and patches `info.yaml` (`language: Verilog`, `top_module`, `source_files`), matching what `tt-support-tools` expects.

### 5.6 Command line

`chiply-cli export-verilog diagram.json -o out.v` and `chiply-cli check diagram.json [--enable/--disable <check-id>]` do the same headlessly and exit non-zero on errors, for use in a Makefile or CI.

---

## 6. Built-in simulator (decided 2026-10-03)

Chiply simulates designs itself, like Wokwi: press Run and the circuit runs, with no PDK, no standard-cell or gate-level netlist, and nothing else to install. The simulator lives in its own plain C++ library (`src/sim`, no Qt) so the GUI, the CLI and the tests all use the same engine.

### 6.1 Model

- **Logic modes** (decided 2026-10-03 after comparing with wokwi.com): the default is **Wokwi logic**: every gate input reads 0 or 1 (floating or unknown reads 0), flip-flops start at a random 0 or 1 like wokwi.com (`Math.random()`; `option zero-start`, `x-start` and `seed N` override it, so an LFSR never starts locked at all zeros), reset beats set on DFFSR, an SR flop toggles when S and R are both 1, and gates are evaluated one at a time with the latest values, so feedback loops start from definite values and symmetric latches settle, as on wokwi.com. Nets still show X/Z on the canvas for debugging. **Verilog mode** (`option verilog`, used to verify against the Verilog export) keeps four-state pessimism and parallel delta cycles. Tiny Tapeout input pads have a very weak pull-down (weaker than a resistor), so an open switch reads and shows 0. The contacts of DIP switches, slide switches and pushbuttons have the same pull-down (added 2026-10-05), so a switch wired straight to logic, with no Tiny Tapeout block, also reads and shows 0 when open; a driver or a pull-up resistor on the net wins. Nets that nothing at all is connected to still show Z.
- **Values**: four states per net: `0`, `1`, `X` (unknown: uninitialised flip-flop, conflict) and `Z` (undriven). Drive strength is either strong (gate outputs, VCC/GND, Tiny Tapeout block outputs) or weak (a resistor pulling a net to VCC/GND). Net value = the strong drivers if any (all equal, else `X`), else the weak ones, else `Z`.
- **Primitives** (from the part library's `verilog` mapping and Wokwi's own simulator behaviour): AND/OR/XOR/NAND/NOR/XNOR (2-input), NOT, buffer, MUX (`out = SEL ? B : A`), D flip-flop, DR (async reset), DSR (async set and reset, reset wins, as `cells.v`), SR flip-flop (Wokwi's `wokwi-flip-flop-sr`), constants for VCC/GND. Gates treat `X`/`Z` inputs pessimistically (`0 AND X = 0`, `1 AND X = X`, ...). Flip-flops sample on a rising `CLK` edge (`0→1`), outputs `X` until reset or first clock.
- **Time and events**: an event queue keyed by time (integer picoseconds) and delta cycle. Default mode is zero-delay with delta cycles (fast, Wokwi-like); a unit-delay mode (configurable per gate type) is available to see glitches and races. Combinational loops are allowed; an oscillation limit per time step reports "combinational loop did not settle" with the nets involved instead of hanging.
- **Switch-level parts**: pushbuttons, slide switches, DIP switches and resistors are passive. Nets joined through a closed switch form one electrical node; toggling a switch re-partitions just those nodes. Resistors join nets weakly (pull-ups / pull-downs), which is how the Tiny Tapeout template's RESET button works.
- **Netlist compile**: the document is compiled once per Run into flat arrays (nets, primitives, fanout lists) for speed; edits while stopped recompile in milliseconds. Unknown part types and the Pico are ignored with a warning (their pins float).

### 6.2 Board and stimulus

- **Tiny Tapeout blocks**: the input block drives its `IN0..7`, `CLK`, `RST_N` pins from whatever its `EXT*` pins see; the output block passes `OUT0..7` through to `EXTOUT0..7`; the bidirectional block drives `IN` from `UIO` and drives `UIO` from `OUT` when `OE` is 1. So the design side and the testbench side are wired exactly as in Wokwi, and the same diagram simulates the chip plus its board.
- **Clock generator**: square wave at its `frequency` attr (e.g. `10000`, `10k`).
- **Inputs**: click a pushbutton (or press its `key` attr, e.g. `s` = Step, `r` = RESET), click slide switches and DIP switches; `bounce` ignored.
- **Outputs**: LEDs light (anode high, cathode low), 7-segment segments light per `common` (anode/cathode) and `color`.
- **Pacing**: real time by default, paced to the clock generator; a speed control runs faster or slower; Pause, Step (one clock period), Reset (re-initialise state).

### 6.3 UI while running

- Run / Pause / Step / Reset on the toolbar; editing is locked while running (Wokwi behaviour), selection and navigation still work.
- Live values on the canvas: wires drawn bright when `1`, dim when `0`, red-dashed when `X` / conflict, gray-dotted when `Z`; values must not rely on colour alone, so `X`/`Z` also differ in line style. Hovering a pin or wire shows its value in the tooltip. Each flip-flop shows its stored bit in a small square on its Q lead, as on wokwi.com: filled yellow for `1`, empty for `0`, dashed red when unknown. The **LiveWire** toolbar checkbox (on by default, saved as a preference) turns this wire colouring on or off; tooltips show values either way.
- A status line shows simulated time, clock cycles and speed (e.g. "1.25x real time").

### 6.4 Traces and tests

- **Logic analyzer part**: records D0..D7 into a VCD named by its attrs, as Wokwi does; Simulation > Save Trace as VCD writes it (Wokwi downloads it on Stop), and "Open Trace in GTKWave" appears when GTKWave is installed (optional).
- **Probes**: right-click any pin or wire and choose Probe; probed nets appear in the Waveforms pane and in the VCD, and are remembered per file.
- **Truth tables**: `chiply-cli sim` runs a design headlessly with scripted stimulus and Tiny Tapeout's `truthtable.md` format, for regression tests and CI.
- **Verification of the simulator itself**: per-primitive unit tests; event-ordering tests (delta cycles, async set/reset, clock edges); and a co-simulation test that drives Chiply's simulator and a reference Verilog simulation of Wokwi's golden export of the reference design (`reference/tt_um_wokwi_*.v` + `cells.v`) with the same random `ui_in`/`uio_in`/`rst_n` sequence and compares `uo_out`/`uio_out`/`uio_oe` every cycle. That test uses Icarus Verilog or Verilator only when present on the developer machine (skipped otherwise); users never need them.

### 6.5 Boards with firmware

A `wokwi-pi-pico` is rendered, wired, saved and loaded like any part; the simulator treats its pins as undriven. A later, separate project adds a full RP2040 emulator as another `SimBackend` (each backend owns a set of pins, advances by a requested number of picoseconds and exchanges pin states at a shared time base), so the Pico runs real firmware against the built-in logic simulation, the way Wokwi uses rp2040js.

### 6.6 Optional Verilator backend (M9)

For very large designs, or designs with Verilog custom blocks, the same netlist can be exported (section 5) and simulated by Verilator behind the `ChipBackend` interface (`sim/ChipBackend.h`): Verilator computes the chip, the built-in simulator keeps the board and shows Verilator's net values live. Optional, detected at runtime, never required; see M9 for how it is built and cached.

---

## 7. Custom blocks

### 7.1 Wokwi mode and Extended mode (decided 2026-10-03)

Chiply has two modes, so a design never stops loading in Wokwi by accident:

- **Wokwi mode** (default): only Wokwi's own parts are offered. Every design you make loads, renders and simulates on wokwi.com and exports through Wokwi's `cells.v`.
- **Extended mode**: a checkable **Edit > Chiply Extensions** (a saved preference) adds Chiply's own parts to the palette: the extended cells below, and later custom blocks (7.2), RAM/ROM and sub-sheets. A design that uses any of them is no longer Wokwi-loadable (Wokwi shows such parts as unknown), so the status bar always shows the mode, saving such a design says so, and in Wokwi mode a DRC check (`extension-part`, warning) lists every extension part, so it is impossible to miss.
- Opening a design that contains extension parts always works (they render, simulate and export); in Wokwi mode Chiply suggests turning the extensions on to add more.
- Extension parts are saved in `diagram.json` like any part (`"type": "chiply-..."`), single-bit wires as always.

**Extended cells** (part type, pins, function, Verilog cell in Chiply's `chiply_cells.v`), aimed at denser designs, named after the usual standard-cell functions:

| Part | Pins | OUT = | Cell |
|---|---|---|---|
| `chiply-gate-and-3` / `-and-4` | A, B, C, (D) | A·B·C(·D) | `and3_cell`, `and4_cell` |
| `chiply-gate-nand-3` / `-nand-4` | A, B, C, (D) | !(A·B·C(·D)) | `nand3_cell`, `nand4_cell` |
| `chiply-gate-or-3` / `-or-4` | A, B, C, (D) | A+B+C(+D) | `or3_cell`, `or4_cell` |
| `chiply-gate-nor-3` / `-nor-4` | A, B, C, (D) | !(A+B+C(+D)) | `nor3_cell`, `nor4_cell` |
| `chiply-gate-xor-3` | A, B, C | A^B^C (full-adder sum) | `xor3_cell` |
| `chiply-maj-3` | A, B, C | majority (full-adder carry) | `maj3_cell` |
| `chiply-mux-4` | A, B, C, D, S0, S1 | S1 ? (S0 ? D : C) : (S0 ? B : A) | `mux4_cell` |
| `chiply-a21oi` | A1, A2, B1 | !((A1·A2)+B1) | `a21oi_cell` |
| `chiply-a21o` | A1, A2, B1 | (A1·A2)+B1 | `a21o_cell` |
| `chiply-o21ai` | A1, A2, B1 | !((A1+A2)·B1) | `o21ai_cell` |
| `chiply-o21a` | A1, A2, B1 | (A1+A2)·B1 | `o21a_cell` |
| `chiply-a22oi` | A1, A2, B1, B2 | !((A1·A2)+(B1·B2)) | `a22oi_cell` |
| `chiply-o22ai` | A1, A2, B1, B2 | !((A1+A2)·(B1+B2)) | `o22ai_cell` |

They simulate natively (built-in and Verilator), take part in DRC like any gate, and export as instances of the cells above; the Verilog and Tiny Tapeout exports add `chiply_cells.v` (written by Chiply, same style as `cells.v`) to the sources only when the design uses one. Symbols follow Wokwi's gate style: 3- and 4-input gates are taller versions of the 2-input shapes; AOI/OAI cells draw their AND/OR input stage feeding the NOR/NAND (or OR/AND) output stage inside one part.

### 7.2 Custom blocks

A custom block is a folder:

```
blocks/sram_16x8/
  block.json
  sram_16x8.v
```

```json
{
  "name": "sram_16x8",
  "module": "sram_16x8",
  "verilog": ["sram_16x8.v"],
  "ports": [
    { "name": "clk",  "dir": "in",  "width": 1, "clock": true },
    { "name": "we",   "dir": "in",  "width": 1 },
    { "name": "addr", "dir": "in",  "width": 4 },
    { "name": "din",  "dir": "in",  "width": 8 },
    { "name": "dout", "dir": "out", "width": 8 }
  ],
  "params": { "INIT_FILE": "" },
  "symbol": "auto"
}
```

- Block folders are found in a `blocks/` folder next to the design and in the per-user library (`~/Library/Application Support/Chiply/blocks` on macOS); the design's own folder wins when both have a block of the same name (decided 2026-10-03). Optional `label` and `prefix` fields set the palette name and the id prefix for new parts.
- It becomes part type `chiply-block-sram_16x8`, in the palette under Custom. The auto symbol is a rectangle with inputs on the left, outputs on the right, clock pins marked with the triangle; an SVG can be supplied instead.
- Multi-bit ports are expanded to one pin per bit (`addr0..addr3`, `dout0..dout7`) so wires stay single-bit, exactly like the TT blocks do with `IN0..IN7`. The netlister rebuilds the vector: `.addr({net44, net43, net42, net41})`. Single-bit wires only, decided: every wire Chiply writes is a plain Wokwi connection, so any design without custom blocks pastes into Wokwi and renders. Bus wires may come later as an extension.
- The Verilog module is included verbatim in exports; parameters come from the part's attrs.
- Simulation of custom blocks without external tools: (a) blocks whose implementation is another Chiply schematic (sub-sheets) simulate natively in the built-in simulator; (b) common blocks are built-in behavioural primitives with parameters, starting with RAM and ROM (width, depth, init file) since memories are the main need; (c) blocks backed only by arbitrary Verilog simulate through the optional Verilator backend (M9).
- Wokwi cannot load a file containing `chiply-block-*` parts. Chiply warns on save ("this design is no longer Wokwi-loadable") and the TT export path (section 5.5) is the way to tape it out.
- Later: hierarchical blocks whose implementation is another Chiply schematic (sub-sheets).
- Custom blocks, RAM/ROM and sub-sheets are extension parts: offered only in Extended mode (7.1).

### 7.3 RAM and ROM (decided 2026-10-03)

Built-in behavioural memories (Extended mode) that simulate natively, with no Verilator, and export as Verilog:

- **Types carry the size**, because the size decides the pins: `chiply-ram-<depth>x<width>` and `chiply-rom-<depth>x<width>`, depth a power of two from 2 to 256, width 1 to 16. Any such type is created on first use; Add Part offers RAM 16x8 and ROM 16x8, and the Inspector's Size row (words x bits) changes the type, undoably. Wires to pins that disappear stay and DRC reports them.
- **RAM**: pins `clk` (clock), `we`, `a0..`, `d0..` in, `q0..` out. Writes on the rising clock edge when `we` = 1 (applied in the flip-flop update phase); the read follows the address at once. Contents start like the flip-flops: random (seeded), zero, or unknown (Verilog mode); an unknown address or `we` writes unknowns. Exported as the parameterized `chiply_ram` (flop storage, asynchronous read) in `chiply_cells.v`.
- **ROM**: pins `a0..` in, `q0..` out; contents from the part's `data` attr or a `file` (relative to the design's folder), in `$readmemh` style (hex words, `//` and `/* */` comments, `@addr`, `_` separators); missing words are 0. Exported as a per-instance case-table module `<top>_<id>_rom` written after the top module, which synthesizes to plain logic. Contents that cannot be read, are too long or too wide are a DRC error (`memory-contents`) and stop the export.

---

## 8. Milestones

Estimates are working days for one developer using Claude Code; each milestone ends with something you can run.

| # | Milestone | Deliverable / acceptance | Est. |
|---|---|---|---|
| M0 | Project skeleton | **Done.** CMake + Qt 6 + Catch2 build on your Mac; empty window; CI on GitHub Actions (macOS, Linux) | 1 |
| M1 | Core model + Wokwi JSON | **Done.** Load/save all three reference files with JSON-equal round trip; wire path codec incl. `"*"`; id generator | 2–3 |
| M2 | Part library + calibration | **Done.** 30 part types in `resources/parts.json` with exact Wokwi geometry and pin directions (2.7); Chiply's own symbol artwork for all of them; wires drawn with Wokwi's completion rule; verified against the reference design | 3–4 |
| M3 | Viewer + selection | **Done.** Wokwi-exact rendering, zoom/pan/fit/grid, hover (part outline, pin names); click / Shift+click / Ctrl+click, marquee (enclosed, Alt = touched, Ctrl = add, edge auto-scroll), Esc, Select All, implicit and stretching wires, group box and count, selection kept across theme changes; GUI tests on the reference design | 2–3 |
| M4 | Part editing | **Done.** Click-drag move (part or selection, wires follow live, grid snap with Alt half / Ctrl-Cmd free, Esc cancel, one undo step); arrows move the selection 1 grid step, Shift 5; R rotate; Delete; D duplicate with renumbered ids; `A` / `+ Add Part` palette with cursor placement; Inspector (name with Verilog validation, attributes, pins with directions, wire color); F2 / double-click / mini toolbar to rename; Show Part Names; unlimited undo/redo with a history panel. Alt+drag duplicate moves to M6 with copy/paste | 3–4 |
| M5 | Wire editing | **Done.** Drawing wires from pins (L-bend preview, grid-snapped bends, click or drag-release on a pin, Esc / right-click cancel, Backspace, GND/VCC default colors); yellow segment handles, orange corner handles, cyan end handles (drag onto another pin to reconnect, elastic preview; a click without a drag deselects the wire and starts a new wire from that end's pin, for a second connection right after the first; while dragging an end or drawing a wire, a large tooltip names the pin under the cursor as confirmation it is targeted); Ctrl/Cmd+drag split; Wokwi color keys while drawing and on selected wires; double-click delete; all undoable | 4–5 |
| M6 | Copy/paste + tabs | **Done.** Cut/Copy/Paste (Cmd/Ctrl+X/C/V) as Wokwi JSON text on the system clipboard (also pastes a whole diagram.json or a fragment from a text editor); ids renumbered against the target (auto ids next-free in order, custom names kept or suffixed); pasted parts float with the cursor until a click drops them (one undo step, Esc or Undo cancels); pasting into another tab asks whether to skip Tiny Tapeout I/O blocks the target already has (default skip); Option/Alt+drag duplicates; status bar reports renamed/skipped/dropped counts. Tabs, per-tab undo, Save All already in place. Deferred: drag a selection onto another tab, tear-off tabs, read-only library tabs, autosave | 3–4 |
| M7 | Built-in simulator (section 6) | Staged; each stage ends with something runnable and tested: | 8–12 |
| M7a | Netlist compile | **Done.** `core/Netlist`: nets from wires, junctions and same-prefix internal pins (`1.l`/`1.r`, `COM.1`/`COM.2`); switches, buttons and resistors kept as devices between nets; drivers/loads from pin directions; unknown pins/parts reported, never dropped; matches the net partition of Wokwi's Verilog export of the reference design exactly (924 cells, all chip-side pins, constant nets); 10 ms for the reference design; `chiply-cli netlist` | 1–2 |
| M7b | Kernel + primitives | **Done.** `src/sim` (plain C++): 0/1/X/Z values with Verilog-style pessimism, event queue with delta cycles, flip-flop updates in a separate phase (like non-blocking assignments, so gated clocks do not race), zero-delay and unit-delay modes, loop detection; AND/OR/XOR/NAND/NOR/XNOR/NOT/BUF/MUX, D/DR/DSR/SR flip-flops, VCC/GND; flip-flops start at 0 (Wokwi) or X; `chiply-cli sim` scripts. Co-simulation against Icarus running Wokwi's Verilog export of the reference design: 3000 random cycles, 6000 samples, identical including X; about 125k cycles/s on the 1024-part design (optimized build) | 2–3 |
| M7c | Board and stimulus | **Done.** Strong/weak drive strengths (resistors pass a side's strong value weakly to the other: pull-ups/downs); pushbuttons, slide switches (value 1 = middle to pin 3) and DIP switches regroup nets when toggled; Tiny Tapeout input pads (floating reads 0, as Wokwi), output pads, bidirectional pads (OUT when OE = 1); clock generator from its `frequency` attr; LED and 7-segment (common anode/cathode) lit state; chip-only mode for verification; CLI `press`/`release`/`switch`/`segments`. Tested on the TT template (DIP to 7-segment through the chip, RESET pull-up, Step, 10 kHz clock = 10 edges per ms) and the reference design | 1–2 |
| M7d | Interactive UI | **Done.** Toolbar Play/Pause (right of Fit and the zoom buttons), Step (one clock period, while paused) and Stop, drawn icons; simulation mode: clicks and keys go to parts (pushbuttons held while pressed or by their `key`, slide switch levers and individual DIP switches toggle), never select or edit; editing menu, Add Part and undo disabled; real-time pacing at 60 Hz with a CPU budget per tick; lit LEDs (glow) and 7-segment segments, live wire values (high bright and wider, low dimmed, X red dashed, Z grey dotted), values in pin and wire tooltips; status shows simulated time and speed; the file is never changed by simulating. App now builds optimised by default | 2–3 |
| M7e | Traces | **Done.** The simulator logs changes of watched nets with their time; `sim/Trace` records every `wokwi-logic-analyzer` (channels D0..D7 per its `channels` attr, file name from its `filename` attr, as Wokwi) and probed nets, merges changes within one time step, and writes VCD (1 ps timescale, one scope per analyzer plus `probes`). GUI: right-click a pin or wire, Probe (a wire is named after its driver, e.g. `flop30:Q`); probes are remembered per file in the app settings; a Waveforms pane (bottom dock, opens with the first probe or when the design has an analyzer) shows the run live and keeps it after Stop: 1 = top line with a shaded band, 0 = bottom line, X = red hatched, Z = dashed middle, large labels with the value at the hover cursor; wheel zoom at the cursor, drag pans, F fits, End follows; the pane scrolls when there are many rows. Simulation menu: Play/Step/Stop, Save Trace as VCD (defaults to the analyzer file name next to the diagram), Open Trace in GTKWave (shown when GTKWave is installed). CLI: `chiply-cli truthtable` runs Tiny Tapeout's `truthtable.md` (tt-support-tools semantics: `t` toggles, `c` expands to setup + full clock pulse + row, `x`/`-` unchanged or don't care, 10 ns per step, chip only with CLK/RST_N floating unless `--set`) and yosys-style tables, `--vcd` records ui_in/uo_out; sim scripts gain `trace` and `vcd`. Not done: the logic analyzer's trigger modes | 1–2 |
| M8 | Netlist, DRC, Verilog | **Done.** `core/Drc`: the 11 checks of 5.2 on the chip side (board parts are the testbench; an input fed through a switch or resistor from a driver counts as driven). **Incremental**: each update compares the new document with the last one checked and re-checks only touched parts (added, removed, changed, re-wired) plus the parts sharing their nets before and after, looking through switches and resistors; violations remember which parts reproduce them; loops that lose or gain cells are re-checked as a whole; a route or colour change checks nothing. A randomized test applies 300 edits to the reference design and requires incremental == full after each (also run on 20 seeds x 600 edits). Reference design: exactly the known mux1..9 loops and ttio5/ttio8 bit 5. GUI: live DRC 100 ms after each edit, Full DRC button / Check menu (Ctrl+Shift+D), Live checkbox (preference), Violations pane below the Inspector (counts with shapes, what the last check covered, filter, groups per check, waived group), click / F8 / Shift+F8 snap with a pulsing orange highlight of pins, net or loop wires, per-design check switches and waivers with reasons in `<name>.chiply.json` saved with the design, DRC counts in the status bar. `core/Verilog`: export byte-identical to Wokwi's for the reference design (same net numbering); Icarus on Chiply's export of the fixed reference design matches Chiply's simulator for 2000 random cycles; the exported template passes all 256 inputs; Verilator lint clean on the fixed design. File > Export Verilog and Export Tiny Tapeout Project (writes `src/<top>.v` and `src/cells.v`, sets language/top_module/source_files in info.yaml keeping its layout, comments out wokwi_id, updates test/Makefile PROJECT_SOURCES). Exports run a full DRC first: errors offer Show Violations / Export Anyway, warnings ask. CLI: `check`, `check --list`, `export-verilog`, `export-tt` (non-zero exit on DRC errors unless `--force`). Differences from 5.2: simulation warns about DRC errors and shows the pane instead of refusing (Wokwi simulates such designs); check switches are per design (no global Preferences page yet) | 3–4 |
| M9 | Optional Verilator backend | **Done.** `sim/ChipBackend`: another engine computes the chip; the built-in simulator keeps the board (pads, switches, buttons, clock, displays) and one chip primitive hands the backend clk/rst_n/ui_in/uio_in and drives every chip net with the backend's values, so wires, flip-flop squares, tooltips, probes and the logic analyzer stay live. `vl/VerilatorChip`: exports the chip (core/Verilog, net map included), runs `verilator --cc --public-flat-rw --x-initial unique`, generates a C wrapper, compiles the generated C++ and the Verilator runtime in parallel with the system compiler for this machine's architecture (so the Intel Verilator in /usr/local works on Apple silicon), links a shared library and loads it with dlopen. Builds are cached by content hash in ~/Library/Caches/Chiply/verilator (runtime objects shared), built in a private directory and moved into place so concurrent builds are safe; about 2-4 s for the reference design, instant from the cache. Flip-flops start random (seeded) or zero, as in the built-in engine. GUI: Simulation > Engine (Built-in / Verilator, a preference, disabled with the reason when Verilator or a compiler is missing); Play builds with a cancellable progress dialog, failures show the compiler log and offer the built-in simulator; the status line names the engine. CLI: `option verilator` in sim scripts, `truthtable --verilator`. Tests (skipped without Verilator): every chip net of the fixed reference design equals the built-in simulator for 1500 random cycles (over a million comparisons); the template truth table; Tiny Snake runs on the board; cache hit; GUI run through the Verilator chip. On the 1024-part reference design the built-in engine is about 3x faster (Verilator copies every net back for the live view); Verilator is for much larger designs and, with M10, Verilog-only blocks | 2–3 |
| M10 | Extended mode and custom blocks (section 7) | Staged: | 4–6 |
| M10a | Extended mode and cells | **Done.** Edit > Chiply Extensions (saved preference, usable while simulating), WOKWI MODE / EXTENDED MODE in the status bar, Add Part offers the 17 cells of 7.1 under "Chiply cells" only in Extended mode, `extension-part` DRC warning in Wokwi mode (CLI: `check --extensions`), messages when opening or saving a design that uses extension parts. Cells: library entries on the 0.1 inch grid (3-input 10 mm high, 4-input and AOI/OAI 15.24 mm with inputs in pairs and OUT centred, MUX4 20.32 mm with S0/S1 from below), symbols in Wokwi's gate style (AOI/OAI draw their input stage feeding the output stage), built-in simulation with four-state semantics, DRC (combinational, loops, clock-from-logic), `resources/chiply_cells.v` (BSD) written beside Verilog exports, added to Tiny Tapeout projects (info.yaml and test/Makefile) and to Verilator builds when used. Tests: every cell's full truth table in the built-in simulator, through Chiply's Verilog export under Icarus (cells.v + chiply_cells.v) and through the Verilator engine; DRC in both modes; the project export; the mode switch in the GUI | 1–2 |
| M10b | Custom blocks | **Done.** `core/Blocks`: `block.json` (name, module, verilog files, ports with dir in/out/inout, width, clock; params; optional label and prefix) validated with clear messages; part type `chiply-block-<name>` in the "Custom" palette group (Extended mode). Found in the design's `blocks/` folder, then the per-user library (`~/Library/Application Support/Chiply/blocks`, `$CHIPLY_BLOCKS` overrides); the design's folder wins for the same name. Part definitions live in a deque so reloading never moves them. Auto symbol on the 0.1 inch grid: inputs left, outputs/inouts right, bit 0 at the top, width from the pin names, name at the top, clock wedge on clock pins. Parameters are part attrs (defaults from block.json), editable in the Inspector. Export: module instance with `#(.P (v))` (strings quoted), vectors joined MSB first, unconnected input bits 1'b0, unconnected output bits get their own wire, whole unconnected ports empty; block sources copied beside Verilog exports and into Tiny Tapeout projects (listed in info.yaml and test/Makefile). Simulation through the Verilator engine (block files and their contents in the build and cache key); with the built-in engine Play offers to switch, and the simulator warns that blocks are not simulated. DRC treats block pins as chip pins. Edit > Reload Custom Blocks, Edit > Open Block Library Folder. Tests: loading and pin layout, error reports, folder priority, export text, an adder (all 256 inputs, OFFSET parameter) and a counter under Icarus and through Verilator, DRC, project export, GUI. Limitation: block names are global per Chiply session, so two open designs with different blocks of the same name share the most recently loaded one | 1–2 |
| M10c | RAM and ROM | **Done** (section 7.3). `core/Memory`: size-carrying types made on first use (thread-safe library lookup), auto symbols, `$readmemh`-style contents from `data` or `file`; native simulation with writes in the update phase and four-state handling; `chiply_ram` in `chiply_cells.v`, ROMs as generated case-table modules; DRC `memory-contents`; the design folder passed to simulator, DRC, Verilator and exports; Inspector Size row. Tests: types, contents parser, a seven-segment ROM and RAM write/read sequences in the built-in simulator (zero, random, unknown starts), the same against Icarus on the export and through Verilator, DRC, project export, GUI size change with undo | 1 |
| M10d | Sub-sheets | Blocks whose implementation is another Chiply schematic; simulate natively, flattened on export | 1–2 |
| M11 | Polish and packaging | Preferences, key remap, recent files, autosave, crash-safe save, `.app` bundle and Linux AppImage, user docs | 2–3 |
| M12 | Web version (section 13) | Engine API; core + simulator + DRC in WebAssembly with the test suite under Node; TypeScript canvas front end; static hosting | 6–10 |

Total roughly 40–55 days. M0–M6 (done) give a Wokwi-compatible editor; M7 makes it simulate on its own like Wokwi; M8 adds checks and export for tapeout; M9–M10 go beyond Wokwi.

Suggested order of value: M0–M3 first (you can already open and inspect your design), then M5 before M4 if wire editing matters more to you than part editing.

---

## 9. Testing strategy

- **Unit**: path codec (every `h/v/*` case, normalization), geometry (rotation, snapping), id generation, rename rewriting, `IdRemapper` (auto ids renumbered in order, custom names kept or suffixed, in-fragment collisions, connections rewritten, dangling connections dropped), netlist on small hand-built diagrams, DRC cases per check (each with the check enabled and disabled, plus waivers), and the pin-direction table of every part definition validated against the schema at build time.
- **Round trip**: the three reference files load → save JSON-equal; also a fuzz test that edits and un-edits (undo) and checks equality. Cross-document test: copy a block from the TT template tab, paste into the reference design, assert no duplicate ids, every pasted wire still references pasted parts, and the netlist of the pasted block is isomorphic to the original.
- **Golden images**: render each part type at the four rotations to PNG and compare against checked-in images, so symbol regressions are caught.
- **Simulator**: per-primitive and event-ordering unit tests; co-simulation of the built-in simulator against a Verilog simulation of Wokwi's golden export of the reference design (6.4, only where Icarus/Verilator is installed).
- **Equivalence**: Chiply's Verilog export vs Wokwi's export of the reference design (section 5.4).
- **Interaction**: Qt Test with synthesized mouse events for selection (click, Shift+click, marquee enclosed/crossing/additive), click-drag moves (single part, group, snapping, Esc cancel, wires following, single undo step), the wire tool and its handles, and the Violations pane (click centers and zooms on the item, selects it, highlights the whole net for multiple drivers; F8 stepping).
- **Manual Wokwi check** once per milestone: save from Chiply, upload to Wokwi, confirm it renders and simulates. This is the only test that catches a wrong pin offset, which is why calibration comes early (M2).

---

## 10. Environment setup on this Mac

The machine is an Apple M4 Max (arm64). The original Homebrew in `/usr/local` is an Intel install whose tools (cmake, verilator, GNU coreutils, which shadow `uname`) run under Rosetta; Intel Homebrew on macOS 26 has no prebuilt bottles. Chiply is built natively with a second, arm64 Homebrew in `/opt/homebrew`, installed 2026-10-02:

```bash
/opt/homebrew/bin/brew install qtbase qtsvg cmake ninja   # prebuilt arm64 bottles, Qt 6.11
/opt/homebrew/bin/brew install icarus-verilog             # optional: only for the simulator co-simulation test
export PATH=/opt/homebrew/bin:$PATH
cmake -S chiply -B chiply/build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build chiply/build
```

Only `qtbase` and `qtsvg` are installed, not the full `qt`, which would also pull Qt WebEngine. Simulation needs nothing extra. The optional Verilator backend (M9) works with the Intel Verilator in `/usr/local`: Chiply only uses Verilator's generated C++ and runtime sources and compiles them itself with `/usr/bin/c++ -arch arm64`, so the library matches the app.

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Pin offsets or rotation pivot differ from Wokwi by a grid unit, so saved files look broken in Wokwi | Resolved in M2: geometry taken from Wokwi's own definitions (2.7) and guarded by the reference-wiring unit test; still do a manual Wokwi check each milestone |
| Wokwi changes its format or adds parts | Unknown keys/parts preserved verbatim (3.6); part library is data, not code |
| Wire editing UX takes longer than estimated | It is the biggest milestone (M5) and is isolated in `WireTool` + handles; ship draw-only first, handles second |
| Built-in simulator too slow for big designs | Compiled flat netlist, zero-delay event kernel; the reference design (1024 parts) is the benchmark; optional Verilator backend (M9) for anything larger |
| Built-in simulator behaves differently from silicon / Wokwi | Primitive semantics follow `cells.v`; co-simulation test against Wokwi's golden Verilog export; truth-table regression tests |
| Qt licensing concerns | LGPL dynamic linking; no commercial Qt needed for an open-source Chiply |

---

## 12. Decisions (made 2026-10-02)

| # | Topic | Decision | Where it landed |
|---|---|---|---|
| 1 | Left-drag on empty canvas | Pan, as in Wokwi (changed 2026-10-03 so Wokwi users need not relearn); Shift+drag is the marquee. Pan is also middle-drag, Space+drag, Shift+wheel, and the arrow keys, which move the diagram in the arrow's direction like Wokwi. With parts selected, arrows move them (Shift: 5 steps); otherwise arrows pan; Ctrl/Cmd+arrows always pan | 4.2, 4.3, 4.4 |
| 2 | Double-click on a wire | Deletes it, as in Wokwi. Ctrl/Cmd on a segment adds a vertex, realised as Ctrl/Cmd+drag splitting the segment into a step | 4.6 |
| 3 | Chiply-only metadata | Sidecar `name.chiply.json`; `diagram.json` stays pure Wokwi | 3.3, 3.6 |
| 4 | Buses | Single-bit wires only for now, so any design without custom blocks pastes into Wokwi and renders. Bus wires may be added later | 7 |
| 5 | Extra parts | Add `wokwi-logic-analyzer` (with VCD capture in simulation) and `wokwi-pi-pico` (place and wire only; firmware emulation is a separate project) | 2.4, 6, M2 |
| 6 | Qt 6 install | Native arm64 Homebrew: `brew install qtbase qtsvg` (the modules of `qt` that Chiply uses) | 10 |
| 7 | License | BSD 3-Clause | 3.1, M0 |
| 8 | Keys | Pure Wokwi keys, plus the additions in decisions 1 and 2 | 4.9 |
| 9 | Simulation (2026-10-03) | Built-in event-driven simulator, no PDK, gate-level netlists or external tools; Verilator only as an optional backend | 6, M7, M9 |
| 10 | CI (2026-10-03) | Every change verified locally with the full test suite; GitHub CI runs only on request | 9 |
| 11 | Modes (2026-10-03) | Wokwi mode by default; Chiply's own parts (extended cells, custom blocks, RAM/ROM, sub-sheets) only in Extended mode, switched by a saved preference | 7.1, M10 |
| 12 | Web version (2026-10-03) | Option A: the C++ engine compiled to WebAssembly in a Web Worker, with a TypeScript front end; no server on the interactive path; the Qt app stays and shares the engine | 13, M12 |

Repository: <https://github.com/kdp1965/Chiply> (public, BSD 3-Clause, `main`). The Pico is agreed to be a future full-emulation backend and a project of its own; this plan only keeps the simulation interface ready for it (6.5). Nothing remains open.

## 13. Web version (option A, decided 2026-10-03)

Goal: the Chiply experience in a browser, with nothing to install, keeping the C++ engine's speed.

- **What runs where.** The engine (`core`, `sim`, DRC, Verilog export, truth tables) is plain C++ with no Qt; Emscripten compiles it to WebAssembly, which runs in a Web Worker at roughly 1.2-2x native time. On the reference design the native simulator runs 3 simulated seconds in 35 ms (about 85x real time), so the browser keeps a wide margin. The page itself is a TypeScript front end drawing on a canvas (or WebGL), which also avoids the SVG/DOM cost that slows Wokwi on large designs; part art can follow Wokwi's MIT-licensed `wokwi-elements`, whose geometry Chiply already matches.
- **Engine API.** One message protocol (JSON) for everything the UI needs: load/save documents, edit commands with undo/redo, selection queries, netlist and DRC results, simulation control (play, pause, step, press, switch) and compact value updates (changed net bits, about 60 per second; roughly 125 bytes per frame for the reference design). The Qt app moves onto the same API in-process, so desktop and web share one engine and one test suite.
- **What changes for the web build.** No `popen`/`dlopen` (the Verilator backend and GTKWave stay desktop features), files through browser upload/download and the File System Access API, threads only where the site sends COOP/COEP headers (the engine works single-threaded).
- **Hosting.** Static files (e.g. GitHub Pages), working offline once loaded. No server on the interactive path; a server is only an optional extra for heavy batch jobs (Verilator runs, long regressions), sandboxed, behind the same API.
- **First steps.** Compile core + sim + DRC to WebAssembly and run the existing Catch2 tests under Node; measure the reference design against native; then build the front end. A Qt-for-WebAssembly build of the whole app is a cheap way to preview, not the product (20-30 MB download, browser shortcut clashes such as Ctrl+W/Ctrl+N).
- **Licensing.** The engine is BSD and Chiply's own code; option A does not ship Qt to the browser.

---

## Appendix A. Pin calibration (no longer needed)

The browser-console calibration planned here was not needed: the exact geometry was found in Wokwi's public editor code and the `wokwi-boards` repository (see 2.7). If Wokwi adds a part, the same sources are the place to look, and the reference-wiring unit test checks any new numbers against a real design.

## Appendix B. Verilog export shape to replicate

```verilog
/* Automatically generated from https://wokwi.com/projects/414123795172381697 */
`default_nettype none
// verilator lint_off UNUSEDSIGNAL
// verilator lint_off PINCONNECTEMPTY
module tt_um_wokwi_414123795172381697(
  input  wire [7:0] ui_in,
  output wire [7:0] uo_out,
  input  wire [7:0] uio_in,
  output wire [7:0] uio_out,
  output wire [7:0] uio_oe,
  input ena, input clk, input rst_n
);
  wire net1 = clk;
  wire net2 = rst_n;
  wire net3 = ui_in[0];
  ...
  wire net20 = 1'b0;
  wire net24 = 1'b1;
  ...
  assign uo_out[0] = net11;
  ...
  dffsr_cell state_reg_2 ( .clk (net1), .d (...), .s (...), .r (...), .q (...), .notq () );
  mux_cell mux62 ( .a (net1004), .b (net1003), .sel (net997), .out (net982) );
endmodule
```

The full golden file is `reference/tt_um_wokwi_414123795172381697.v`.
