# Chiply — Build Plan

Status: draft for review (2026-10-02)
Author: Claude, for Ken Pettit

Chiply is a native C++ / Qt desktop schematic editor that reads and writes Wokwi
`diagram.json` files unchanged, reproduces the Wokwi logic-design look and feel
(the Tiny Tapeout flavor of Wokwi), and adds what Wokwi lacks: naming of gates
and flops, a real netlist and Verilog export, Verilator simulation, and
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
5. Simulation with Verilator, interactive like Wokwi (buttons, switches, clock, LEDs, 7-segment), plus VCD output.
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
| Simulation | Verilator ≥ 5 (5.050 installed), optional built-in gate-level sim later | |
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
    SimBackend.h            # co-simulation interface: owned pins, advance(dt), exchange pin states; Verilator is the first backend, an RP2040 emulator a future one
    VerilatorBackend.cpp    # generate harness, run verilator, dlopen the model
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
- Snapping: parts snap by their origin; wire vertices snap to the grid; Shift disables snapping; Alt/Ctrl uses the 4.8 px fine grid. Pins are not snapped (they are where the symbol puts them), which is exactly why Wokwi writes a fractional first segment for mm-based parts.

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
- **Themes**: View → Theme offers System (follow macOS), Light and Dark, remembered in preferences; `--theme` overrides it for one run. Light is the Wokwi look (white canvas, magenta symbols, black leads). Dark matches Wokwi's dark mode (see below). The window chrome follows the same choice. All canvas colors come from one `CanvasColors` table, so every item type added later (symbols, wires, handles, violation highlights) reads its colors there instead of hard-coding them.
- Wire colors are drawn exactly as named in the file in both themes, as Wokwi does (`green` stays #008000 and `black` stays black on the dark canvas). The dark canvas uses Wokwi's dark palette: background #333333, leads #aaaaaa, symbol outlines #d478e2, text #cccccc.
- While dragging, only the moving items and their attached wires repaint; everything else is static. Target: 60 fps panning at the reference size on your machine.

---

## 4. UI specification (Wokwi parity, plus the extras)

### 4.1 Window layout

Menu bar; a top toolbar with `+` Add part, zoom controls, Fit, Grid, Run/Pause/Step (sim); a tab bar with one tab per open file (4.10); the schematic canvas of the active tab in the center; on the right, two stacked docks: the Inspector (id, type, attrs, pin list with directions) that updates with the selection, and the Violations pane (5.2) whose entries navigate the canvas when clicked; a bottom dock for the simulation console and Verilator build log. White canvas, light-gray 0.1 inch dot grid, Wokwi-like.

### 4.2 Navigation

| Gesture | Action |
|---|---|
| Mouse wheel / trackpad scroll | Zoom, centered on the cursor |
| Shift + wheel | Pan horizontally; Ctrl/Cmd + wheel also pans vertically |
| Left-drag on empty canvas | Marquee select (decided; 4.3) |
| Middle-drag, Shift + left-drag, or Space + left-drag | Pan |
| Arrow keys | With parts selected: move them one grid step (Shift: five steps), as in Wokwi. With nothing selected: move the diagram in the arrow's direction by a tenth of the viewport (Shift: a full viewport). Ctrl/Cmd+arrows always pan |
| `+` / `-`, `F` | Zoom in/out, fit to contents |
| `G`, Shift, Alt/Ctrl | Grid toggle, snap off, fine snap |

### 4.3 Selecting objects

Selectable objects are parts (gates, flops, TT blocks, switches, displays, power symbols), text annotations, and wires. One selection model serves all of them.

- **Click** on an object selects it alone, except in a pin's hit region (4.5 px around the pin, where the blue pin marker shows): a click there never selects the part, it starts a new wire from that pin (4.6, milestone M5). Click on empty canvas clears the selection. `Esc` clears it too. Ctrl/Cmd+A selects everything.
- **Shift+click** toggles an object in or out of the current selection (Wokwi's multi-select). Shift+press followed by a drag of more than 4 px pans instead, so the two never conflict.
- **Click on a wire** selects the wire and shows its editing handles (4.6). Wires are hit-tested with a tolerance of a few screen pixels so thin wires are easy to grab at any zoom.
- **Marquee**: press on empty canvas and drag; a translucent blue rubber-band rectangle follows the cursor. On release, every part and text annotation whose bounding box is fully inside the rectangle is selected, and every wire whose entire route is inside is selected. Alt while releasing switches to "crossing" mode (anything the rectangle touches), which is handy for grabbing long wires. Ctrl/Cmd+marquee adds the enclosed items to the selection (Shift+drag is panning, as in Wokwi). Dragging past the viewport edge auto-scrolls so a marquee can cover more than one screen.
- **Implicit wires**: a wire whose two ends are both on selected parts is treated as part of the selection for move, copy, delete and duplicate even if it was not explicitly selected. It is drawn highlighted so you can see what will travel with the parts. A wire with only one end on a selected part is drawn dashed to warn that it will stretch.
- **Feedback**: selected parts get a blue outline; a single selected part also shows Wokwi's mini toolbar (rotate / edit / delete) above it; a multi-selection shows a dotted group bounding box with the item count. The Inspector shows the one part's id and attrs, or a summary ("12 parts, 9 wires") with the operations that apply to all of them (color, delete, rotate).
- Selection survives zoom and pan, and is cleared when the document is reloaded.

### 4.4 Moving objects by click-drag

- **Start**: press the left button on any part or annotation and move more than 4 screen pixels. If the object was not already selected it becomes the sole selection (or joins it with Shift), so a single click-drag on an unselected gate just moves that gate, and a click-drag on any member of a marquee selection moves the whole selection. No "move tool" is needed; this matches Wokwi.
- **Rigid group move**: all selected parts, annotations and implicit wires move as one body. The grabbed part's origin snaps to the 9.6 px grid and the same delta is applied to every other selected object, so relative positions and all internal wire routes are preserved exactly. Shift disables snapping; Alt/Ctrl uses the 4.8 px fine grid; fine snapping of the grabbed part still moves the whole group by one delta.
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
- **Naming**: the id is shown as a small label next to the part (toggle "Show names" globally; Wokwi hides ids except in the edit dialog). Rename by F2, double-click on the label, or the Inspector. Validation: unique, matches `[A-Za-z_][A-Za-z0-9_]*`, rejects Verilog keywords. Renaming rewrites every wire that references the id.
- **Attributes**: the Inspector shows the attrs defined by the part's schema (`frequency`, `label`, `key`, `verilogBit`, `text`, ...) with proper editors, and a raw JSON view for anything unknown.

### 4.6 Wires

- **Drawing**: click a pin (anywhere in its hit region, which takes priority over selecting the part), each further click adds a bend, click a pin to finish; Esc or right-click cancels. The pending segment previews as an L-bend that follows the cursor; the preview's elbow orientation flips when the cursor crosses the diagonal, which is how Wokwi's editor feels. A net with three ends is normally two wires from the same pin. Wokwi also has a beta `wokwi-junction` part (a dot with one pin `J`); Chiply supports it as a part, and a later option can drop a junction where a wire is started from the middle of another wire.
- **Editing a selected wire**: yellow segment handles with a dark outline (done; Wokwi uses small purple dots, Chiply makes them larger and yellow for visibility) and round handles on every vertex (drag to move the corner; neighbors stay orthogonal), bar handles at the middle of each segment (drag to slide that segment perpendicular), a trash icon, and Delete. Double-click on a wire deletes it, exactly as in Wokwi (decided). Ctrl+click on a segment inserts a vertex there; on macOS this is Cmd+click, because Qt maps Cmd to its Ctrl modifier and the OS turns a physical Ctrl+click into a right-click. With several wires selected, color keys and Delete apply to all of them.
- **Colors**: the key map in 2.3, active while drawing or with wires selected; default color by source pin function. Also a color swatch popup on the wire toolbar.
- **Re-anchoring**: dragging a wire's endpoint off a pin and onto another pin reconnects it.

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

### 4.8 Undo / redo

Multi-level, unlimited undo and redo per tab (done): every model change is a `QUndoCommand`; a drag is one step, a held arrow key's repeats merge into one step. Ctrl+Z / Ctrl+Y / Cmd+Shift+Z. View → Undo History lists every step of the active tab; clicking an entry jumps back or forward to that point.

### 4.9 Keyboard map

Pure Wokwi keys (decided): everything in 2.3 as is, including `R`, `D`, `A`, `F`, `G` and the color keys. The only additions are the ones decided above: arrow keys pan, Ctrl/Cmd+arrows nudge, Ctrl/Cmd+click adds a wire vertex, F8 steps through violations, Ctrl+Tab switches tabs. No alternate preset.

### 4.10 Multiple open files (tabs)

Chiply is a multi-document editor so a block can be developed and tested in its own small file, then copied into the large design.

- **One tab per file.** Each tab is an `EditorSession`: its own `Document`, scene, view (zoom, scroll position), selection, undo stack, DRC results and simulation state. Switching tabs switches all of those; nothing is shared except the clipboard, the part library and the preferences. Ctrl+Tab / Ctrl+Shift+Tab cycle tabs; Ctrl+W closes one; tabs can be reordered by dragging and torn off into a second window for side-by-side work on a wide monitor.
- **Opening**: File → Open, Recent Files, drag-and-drop of `diagram.json` files onto the window, and `chiply a.json b.json` on the command line all open tabs. Opening a file that is already open just activates its tab. File → New creates an untitled tab from the Tiny Tapeout template or a blank sheet.
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
| `unconnected-input` | on | warning | an `in` pin with no wire, or whose net has no driver (message says which) |
| `multiple-drivers` | on | error | a net with two or more driver pins (`out`, `inout`, `power`): two outputs tied together, an output tied to VCC/GND, etc. Lists every driver |
| `short-circuit` | on | error | VCC and GND symbols on the same net (Wokwi's "Short circuit") |
| `clock-from-logic` | on | warning | a flip-flop clock driven by combinational logic instead of a clock source or flip-flop output (Wokwi's "Clock driven by combinatorial logic") |
| `unconnected-output` | off | info | an `out` pin that drives nothing (harmless; the ASIC tools optimize it away) |
| `dangling-wire` | on | error | a connection naming a part or pin that does not exist (possible after hand-editing JSON) |
| `invalid-id` | on | error | id is not a legal Verilog identifier, is a keyword, or is duplicated |
| `unknown-part` | on | error | part type with no library definition (blocks export) |
| `tt-bidir-bit` | on | error | bidirectional block with a missing or duplicated `verilogBit` |

Running: DRC runs live by default, incrementally on the nets touched by each edit (debounced ~100 ms), so violations appear and disappear as you wire. A "Run DRC" button does a full pass, and live checking can be switched off for very large edits. Export and simulation always run a full pass first and refuse to proceed on errors (warnings only ask).

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

Automated: compile Chiply's export and Wokwi's golden export of the reference design with Verilator, drive both with the same random `ui_in`/`uio_in`/`rst_n` sequence for 100k cycles, compare `uo_out`/`uio_out`/`uio_oe` every cycle. This proves the netlister against your own 1024-part design before you trust it for a tapeout.

### 5.5 Export Tiny Tapeout project

Writes `src/tt_um_<name>.v`, `src/cells.v`, any custom-block `.v` files, and patches `info.yaml` (`language: Verilog`, `top_module`, `source_files`), matching what `tt-support-tools` expects.

### 5.6 Command line

`chiply-cli export-verilog diagram.json -o out.v` and `chiply-cli check diagram.json [--enable/--disable <check-id>]` do the same headlessly and exit non-zero on errors, for use in a Makefile or CI.

---

## 6. Simulation with Verilator

Pipeline, all driven from the Run button and a worker thread:

1. Netlist → `build/<design>/top.v` (section 5) plus `cells.v` and custom block sources.
2. Chiply also generates `harness.cpp`: a tiny C ABI (`sim_create`, `sim_set_input(i, v)`, `sim_eval`, `sim_clock(n)`, `sim_get(i)`, `sim_names()`, `sim_trace(path)`). Because Chiply wrote the Verilog, it knows every `netN` and emits direct accessors for them (Verilator `--public-flat-rw`), so every wire in the schematic is observable.
3. Run `verilator --cc --build -O2 --trace -CFLAGS -fPIC -LDFLAGS -shared ...` to produce `libchiply_sim.dylib`; cache by hash of the netlist so unchanged designs do not rebuild. Verilator output appears in the bottom dock.
4. Load with `QLibrary`, run on a worker thread paced to the `wokwi-clock-generator` frequency (your design uses 10 kHz; Wokwi recommends ≤ 100 kHz), with Run / Pause / Step-one-clock / speed slider.
5. **Stimulus parts**: pushbutton (mouse or its `key` attr, e.g. `s` for Step, `r` for RESET), slide switch, DIP switch, clock generator; VCC/GND constants; the `ena` port is tied high. The net connected to `EXTINk` drives `ui_in[k]`, `EXTCLK` → `clk`, `EXTRST_N` → `rst_n`.
6. **Display parts**: LED, 7-segment (common anode/cathode per attr), and optionally live net coloring on the schematic (high = bright, low = dim) and a value tooltip on hover. This is something Wokwi does not do and is very useful for debugging flop chains.
7. **Traces**: VCD to a chosen file; "Open in GTKWave" (installed at `/opt/local/bin/gtkwave`). A `wokwi-logic-analyzer` part does the same thing Wokwi's does: it records the nets on D0..D7 into a VCD named by its attrs, with channel names and trigger settings from its attrs, so a design that already has an analyzer wired in keeps working.
8. **Truth-table tests**: run Tiny Tapeout's `truthtable.md` format headlessly (`chiply-cli run-truthtable`) so a design can be regression-tested from the command line.
9. **Boards**: a `wokwi-pi-pico` is rendered, wired, saved and loaded like any part, and in this project the simulator treats its pins as undriven. The intent, agreed, is that a later project adds a full RP2040 emulator as a second `SimBackend` so the Pico runs real firmware (UF2/ELF) against the Verilator model, the way Wokwi does with its rp2040js. To keep that door open, `SimBackend` is specified now as a co-simulation interface: each backend owns a set of pins, advances by a requested number of picoseconds, and exchanges pin states with the others at a shared time base; the Verilator backend is written against that interface even though it is the only one in this project.

Optional later: a built-in event-driven gate-level simulator for instant feedback without a compile step. Not needed for correctness since Verilator is the reference.

---

## 7. Custom blocks

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

- It becomes part type `chiply-block-sram_16x8`, in the palette under Custom. The auto symbol is a rectangle with inputs on the left, outputs on the right, clock pins marked with the triangle; an SVG can be supplied instead.
- Multi-bit ports are expanded to one pin per bit (`addr0..addr3`, `dout0..dout7`) so wires stay single-bit, exactly like the TT blocks do with `IN0..IN7`. The netlister rebuilds the vector: `.addr({net44, net43, net42, net41})`. Single-bit wires only, decided: every wire Chiply writes is a plain Wokwi connection, so any design without custom blocks pastes into Wokwi and renders. Bus wires may come later as an extension.
- The Verilog module is included verbatim in exports and in the Verilator build; parameters come from the part's attrs.
- Wokwi cannot load a file containing `chiply-block-*` parts. Chiply warns on save ("this design is no longer Wokwi-loadable") and the TT export path (section 5.5) is the way to tape it out.
- Later: hierarchical blocks whose implementation is another Chiply schematic (sub-sheets).

---

## 8. Milestones

Estimates are working days for one developer using Claude Code; each milestone ends with something you can run.

| # | Milestone | Deliverable / acceptance | Est. |
|---|---|---|---|
| M0 | Project skeleton | CMake + Qt 6 + Catch2 build on your Mac; empty window; CI on GitHub Actions (macOS, Linux) | 1 |
| M1 | Core model + Wokwi JSON | Load/save all three reference files with JSON-equal round trip; wire path codec incl. `"*"`; id generator | 2–3 |
| M2 | Part library + calibration | **Done.** 30 part types in `resources/parts.json` with exact Wokwi geometry and pin directions (2.7); Chiply's own symbol artwork for all of them; wires drawn with Wokwi's completion rule; verified against the reference design | 3–4 |
| M3 | Viewer + selection | **Done.** Wokwi-exact rendering, zoom/pan/fit/grid, hover (part outline, pin names); click / Shift+click / Ctrl+click, marquee (enclosed, Alt = touched, Ctrl = add, edge auto-scroll), Esc, Select All, implicit and stretching wires, group box and count, selection kept across theme changes; GUI tests on the reference design | 2–3 |
| M4 | Part editing | **Done.** Click-drag move (part or selection, wires follow live, grid snap with Alt half / Ctrl-Cmd free, Esc cancel, one undo step); arrows move the selection 1 grid step, Shift 5; R rotate; Delete; D duplicate with renumbered ids; `A` / `+ Add Part` palette with cursor placement; Inspector (name with Verilog validation, attributes, pins with directions, wire color); F2 / double-click / mini toolbar to rename; Show Part Names; unlimited undo/redo with a history panel. Alt+drag duplicate moves to M6 with copy/paste | 3–4 |
| M5 | Wire editing | **Segment handles done** (yellow, constant screen size, on selected wires; drag slides a segment on the grid, Alt half grid, Ctrl/Cmd free; end segments get a jog so pins stay connected; written back as a Wokwi path that reproduces the route; undo/redo). Still to do: drawing new wires (started by a click in a pin's hit region, which must then no longer select the part), vertex handles, Ctrl/Cmd+click to add a vertex, double-click delete, color keys, re-anchoring | 4–5 |
| M6 | Copy/paste + tabs | Copy/paste/duplicate/Alt-drag with routes preserved; `IdRemapper` renumbering rules (4.7) with tests; multiple files in tabs with per-tab undo/view/selection, cross-tab paste, Save All, autosave (4.10) | 3–4 |
| M7 | Netlist, DRC, Verilog | Netlist with directions; switchable DRC checks running live; Violations pane with click-to-snap, F8 stepping and waivers (5.2); Verilog export; equivalence test vs Wokwi's golden export passes on the reference design; TT project export; CLI | 3–4 |
| M8 | Verilator simulation | Build pipeline with cache; run/pause/step; buttons, switches, clock, LED, 7-segment; live nets; VCD; truth-table runner | 4–6 |
| M9 | Custom blocks | `block.json` loader, auto symbols, bit-expanded pins, netlist and sim integration, TT export with extra sources | 3–4 |
| M10 | Polish and packaging | Preferences, key remap, recent files, autosave, crash-safe save, `.app` bundle and Linux AppImage, user docs | 2–3 |

Total roughly 30–41 days. M1–M7 give you a Wokwi-compatible editor with export; M8–M9 are the parts Wokwi cannot do.

Suggested order of value: M0–M3 first (you can already open and inspect your design), then M5 before M4 if wire editing matters more to you than part editing.

---

## 9. Testing strategy

- **Unit**: path codec (every `h/v/*` case, normalization), geometry (rotation, snapping), id generation, rename rewriting, `IdRemapper` (auto ids renumbered in order, custom names kept or suffixed, in-fragment collisions, connections rewritten, dangling connections dropped), netlist on small hand-built diagrams, DRC cases per check (each with the check enabled and disabled, plus waivers), and the pin-direction table of every part definition validated against the schema at build time.
- **Round trip**: the three reference files load → save JSON-equal; also a fuzz test that edits and un-edits (undo) and checks equality. Cross-document test: copy a block from the TT template tab, paste into the reference design, assert no duplicate ids, every pasted wire still references pasted parts, and the netlist of the pasted block is isomorphic to the original.
- **Golden images**: render each part type at the four rotations to PNG and compare against checked-in images, so symbol regressions are caught.
- **Equivalence**: Verilator co-simulation of Chiply's export vs Wokwi's export of the reference design (section 5.4).
- **Interaction**: Qt Test with synthesized mouse events for selection (click, Shift+click, marquee enclosed/crossing/additive), click-drag moves (single part, group, snapping, Esc cancel, wires following, single undo step), the wire tool and its handles, and the Violations pane (click centers and zooms on the item, selects it, highlights the whole net for multiple drivers; F8 stepping).
- **Manual Wokwi check** once per milestone: save from Chiply, upload to Wokwi, confirm it renders and simulates. This is the only test that catches a wrong pin offset, which is why calibration comes early (M2).

---

## 10. Environment setup on this Mac

The machine is an Apple M4 Max (arm64). The original Homebrew in `/usr/local` is an Intel install whose tools (cmake, verilator, GNU coreutils, which shadow `uname`) run under Rosetta; Intel Homebrew on macOS 26 has no prebuilt bottles. Chiply is built natively with a second, arm64 Homebrew in `/opt/homebrew`, installed 2026-10-02:

```bash
/opt/homebrew/bin/brew install qtbase qtsvg cmake ninja   # prebuilt arm64 bottles, Qt 6.11
/opt/homebrew/bin/brew install verilator                  # needed natively from M8 on
export PATH=/opt/homebrew/bin:$PATH
cmake -S chiply -B chiply/build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build chiply/build
```

Only `qtbase` and `qtsvg` are installed, not the full `qt`, which would also pull Qt WebEngine. The simulator library built by Verilator in M8 must match the app's architecture, so it must come from the arm64 Verilator, not the Intel one in `/usr/local`.

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Pin offsets or rotation pivot differ from Wokwi by a grid unit, so saved files look broken in Wokwi | Resolved in M2: geometry taken from Wokwi's own definitions (2.7) and guarded by the reference-wiring unit test; still do a manual Wokwi check each milestone |
| Wokwi changes its format or adds parts | Unknown keys/parts preserved verbatim (3.6); part library is data, not code |
| Wire editing UX takes longer than estimated | It is the biggest milestone (M5) and is isolated in `WireTool` + handles; ship draw-only first, handles second |
| Verilator build time annoys interactive use | Cache by netlist hash; `-O1` for interactive builds; optional built-in simulator later |
| Qt licensing concerns | LGPL dynamic linking; no commercial Qt needed for an open-source Chiply |

---

## 12. Decisions (made 2026-10-02)

| # | Topic | Decision | Where it landed |
|---|---|---|---|
| 1 | Left-drag on empty canvas | Marquee select. Pan is middle-drag, Shift+drag, Space+drag, Shift+wheel, and the arrow keys, which move the diagram in the arrow's direction like Wokwi. With parts selected, arrows move them (Shift: 5 steps); otherwise arrows pan; Ctrl/Cmd+arrows always pan | 4.2, 4.3, 4.4 |
| 2 | Double-click on a wire | Deletes it, as in Wokwi. Ctrl+click (Cmd+click on macOS) on a segment adds a vertex | 4.6 |
| 3 | Chiply-only metadata | Sidecar `name.chiply.json`; `diagram.json` stays pure Wokwi | 3.3, 3.6 |
| 4 | Buses | Single-bit wires only for now, so any design without custom blocks pastes into Wokwi and renders. Bus wires may be added later | 7 |
| 5 | Extra parts | Add `wokwi-logic-analyzer` (with VCD capture in simulation) and `wokwi-pi-pico` (place and wire only; firmware emulation is a separate project) | 2.4, 6, M2 |
| 6 | Qt 6 install | Native arm64 Homebrew: `brew install qtbase qtsvg` (the modules of `qt` that Chiply uses) | 10 |
| 7 | License | BSD 3-Clause | 3.1, M0 |
| 8 | Keys | Pure Wokwi keys, plus the additions in decisions 1 and 2 | 4.9 |

Repository: <https://github.com/kdp1965/Chiply> (public, BSD 3-Clause, `main`). The Pico is agreed to be a future full-emulation backend and a project of its own; this plan only keeps the simulation interface ready for it (6.9). Nothing remains open.

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
