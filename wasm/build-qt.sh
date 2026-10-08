#!/bin/sh
# Builds the whole Chiply editor for the browser with Qt for WebAssembly
# (PLAN.md 13, option B). Needs:
#   - Qt 6.11.2 for WebAssembly: python3 -m pip install aqtinstall;
#     python3 -m aqt install-qt all_os wasm 6.11.2 wasm_singlethread -O ~/Qt
#   - the Emscripten version that Qt was built with (4.0.7):
#     git clone https://github.com/emscripten-core/emsdk ~/emsdk;
#     ~/emsdk/emsdk install 4.0.7; ~/emsdk/emsdk activate 4.0.7
#   - a host Qt of the same version for the build tools (Homebrew qtbase 6.11.2)
set -e
cd "$(dirname "$0")/.."
QT_WASM=${QT_WASM:-$HOME/Qt/6.11.2/wasm_singlethread}
QT_HOST=${QT_HOST:-/opt/homebrew/opt/qtbase}
. "$HOME/emsdk/emsdk_env.sh" > /dev/null 2>&1
chmod +x "$QT_WASM"/bin/* "$QT_WASM"/libexec/* 2>/dev/null || true
"$QT_WASM/bin/qt-cmake" -S . -B build-qtwasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_HOST_PATH="$QT_HOST" -DCHIPLY_BUILD_TESTS=OFF
cmake --build build-qtwasm
mkdir -p wasm/qt
cp build-qtwasm/src/app/chiply.html build-qtwasm/src/app/chiply.js build-qtwasm/src/app/chiply.wasm \
   build-qtwasm/src/app/qtloader.js build-qtwasm/src/app/qtlogo.svg wasm/qt/
echo "python3 -m http.server -d wasm/qt 8000   # then open http://localhost:8000/chiply.html"
echo "   (chiply.html?file=design.json opens a design served from the same folder)"
