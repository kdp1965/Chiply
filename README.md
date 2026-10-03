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
chiply-cli truthtable diagram.json truthtable.md [--vcd out.vcd]
                                         # Tiny Tapeout truth table on the chip
```

## License

BSD 3-Clause, see [LICENSE](LICENSE). `resources/cells.v`, built into Chiply
for the Tiny Tapeout export, is Tiny Tapeout's Wokwi cell library
(`ttsky-wokwi-template`, Apache-2.0), unchanged.
`resources/tt_template.diagram.json`, the starting point for File > New from
Template, is Tiny Tapeout's Wokwi template
(<https://wokwi.com/projects/354858054593504257>), unchanged. Reference files under `reference/` keep
their own origins and licenses, listed in [reference/README.md](reference/README.md).
