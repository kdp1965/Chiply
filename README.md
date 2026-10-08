# Chiply

A desktop schematic editor for digital logic that reads and writes
[Wokwi](https://wokwi.com) `diagram.json` files unchanged, with the Wokwi look
and feel, plus naming of gates and flops, Verilog export, Verilator
simulation and custom blocks. Built for [Tiny Tapeout](https://tinytapeout.com)
designs.

Status: early development. The design and milestones are in
[PLAN.md](PLAN.md).

| Milestone | State |
|---|---|
| M0 Project skeleton (CMake, Qt window with tabs, CI) | done |
| M1 Core model, byte-exact Wokwi JSON round trip, wire path codec, ids | done |
| M2 Part library: exact Wokwi geometry, pin directions, symbols, wires | done |
| M3 Viewer and selection: hover, click/marquee selection, implicit wires | done |
| M4 Part editing: move, nudge, rotate, delete, duplicate, add parts, Inspector, rename, undo | done |
| M5 Wire editing: draw from pins, segment/corner/end handles, split, colors, delete | done |
| M6 Copy/paste across tabs with id renumbering, Alt+drag duplicate | done |
| M7 Built-in event-driven simulator, board parts, live UI, waveforms, VCD, truth tables | done |
| M8 Incremental DRC with Violations pane, Verilog and Tiny Tapeout project export | done |
| M9 Optional Verilator engine for the chip (Simulation > Engine), built and cached at run time | done |
| M10a Wokwi / Extended mode; extended cells (3/4-input gates, XOR3, MAJ3, MUX4, AOI/OAI) | done |
| M10b Custom blocks: Verilog modules behind auto symbols (`blocks/` folder and user library) | done |
| M10c RAM and ROM (any size up to 256 words x 16 bits), simulated natively | done |
| M10d Sub-sheets: schematics as blocks, with Sheet input / Sheet output ports | done |
| M11 Polish and packaging | next |

## Building

Requirements: CMake 3.21+, Ninja, a C++20 compiler, Qt 6.5+ (Widgets).
nlohmann/json and Catch2 are fetched by CMake.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build build
ctest --test-dir build
```

On macOS, `brew install qtbase qtsvg cmake ninja` is enough; the full `qt`
formula also pulls in Qt WebEngine, which Chiply does not use. On Apple
silicon, use the native Homebrew in `/opt/homebrew`: an Intel Homebrew in
`/usr/local` produces x86_64 binaries and has no prebuilt Qt on macOS 26.

View → Theme switches between System, Light and Dark; `--theme dark` sets
it for one run.

`chiply --screenshot out.png file.json` renders the window to a PNG and
exits; with `QT_QPA_PLATFORM=offscreen` it runs headless.

The core library, CLI and tests build without Qt:

```bash
cmake -S . -B build -G Ninja -DCHIPLY_BUILD_GUI=OFF
```

## Tests

```bash
ctest --test-dir build   # core, round-trip, Wokwi wire fixture, and offscreen GUI tests
```

Optional: with Verilator installed (`brew install verilator`), Simulation >
Engine > Verilator simulates the chip in Verilator. Nothing is needed at build
time; Chiply compiles Verilator's output itself and caches it.

WebAssembly (an experiment towards the web version, see PLAN.md section 13):
with Emscripten installed (`brew install emscripten`), `wasm/build.sh` builds the
engine for Node and the browser; `node build-wasm/src/cli/chiply-cli.js bench
design.json` times it, and `wasm/dist/index.html` does the same on a dropped
file (serve the folder, e.g. `python3 -m http.server -d wasm/dist 8000`).

The whole editor also runs in a browser with Qt for WebAssembly: `wasm/build-qt.sh`
(its header says what to install: Qt 6.11.2 `wasm_singlethread`, Emscripten
4.0.7, a matching host Qt) builds it into `wasm/qt/`; serve that folder
(`python3 -m http.server -d wasm/qt 8000`) and open `chiply.html`, or
`chiply.html?file=design.json` to open a design served next to it. Files are
opened with the browser's file picker and saved as downloads; the Verilator
engine, GTKWave and the Tiny Tapeout project export are desktop-only. The
download is 16.5 MB (6 MB compressed) and the first start takes about 10 s.

GitHub Actions CI (Linux and macOS) runs only on request: Actions tab, CI,
"Run workflow".

## Command line

```bash
chiply-cli info diagram.json             # parts and wires summary
chiply-cli format diagram.json out.json  # load and save
chiply-cli check-roundtrip diagram.json  # verify byte-exact round trip
chiply-cli netlist diagram.json          # connectivity summary
chiply-cli sim diagram.json script.sim   # scripted stimulus and checks (see --help)
chiply-cli check diagram.json            # design rule checks (exit 1 on errors)
chiply-cli export-verilog diagram.json -o tt_um_name.v
chiply-cli export-tt diagram.json path/to/tt-project   # src/*.v, cells.v, info.yaml
chiply-cli bench diagram.json [--seconds S] [--cycles N] [--switch part idx 0|1]
                                         # time parse, netlist, DRC and simulation
chiply-cli truthtable diagram.json truthtable.md [--vcd out.vcd]
                                         # Tiny Tapeout truth table on the chip
```

## License

BSD 3-Clause, see [LICENSE](LICENSE). `resources/cells.v`, built into Chiply
for the Tiny Tapeout export, is Tiny Tapeout's Wokwi cell library
(`ttsky-wokwi-template`, Apache-2.0), unchanged.
`template.json`, the starting point for File > New from Template (built into
Chiply), is Tiny Tapeout's Wokwi template
(<https://wokwi.com/projects/354858054593504257>) with the eight bidirectional
I/O blocks added. Reference files under `reference/` keep
their own origins and licenses, listed in [reference/README.md](reference/README.md).
