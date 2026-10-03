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
| M2 Part library and pin calibration | next |

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

## Command line

```bash
chiply-cli info diagram.json             # parts and wires summary
chiply-cli format diagram.json out.json  # load and save
chiply-cli check-roundtrip diagram.json  # verify byte-exact round trip
```

## License

BSD 3-Clause, see [LICENSE](LICENSE). Reference files under `reference/` keep
their own origins and licenses, listed in [reference/README.md](reference/README.md).
