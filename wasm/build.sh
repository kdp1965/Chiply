#!/bin/sh
# Builds the engine for WebAssembly (PLAN.md 13): the CLI and the core tests
# as Node programs, and the benchmark module for wasm/index.html.
# Needs Emscripten (brew install emscripten).
set -e
cd "$(dirname "$0")/.."
export PATH=/opt/homebrew/bin:$PATH
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHIPLY_BUILD_GUI=OFF
cmake --build build-wasm
mkdir -p wasm/dist
cp build-wasm/src/wasm/chiply_bench.js build-wasm/src/wasm/chiply_bench.wasm wasm/dist/
cp wasm/index.html wasm/worker.js wasm/dist/
echo "node build-wasm/src/cli/chiply-cli.js bench <diagram.json>   # the CLI under Node"
echo "node build-wasm/tests/chiply_tests.js                         # the core tests under Node"
echo "python3 -m http.server -d wasm/dist 8000                      # then open http://localhost:8000/"
