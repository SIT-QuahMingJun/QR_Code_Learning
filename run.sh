#!/usr/bin/env bash
# Build and test the native QR encoder, then build and serve the WASM demo.
# Usage: ./run.sh [all|native|wasm]

set -euo pipefail

MODE="${1:-all}"
case "$MODE" in
  all|native|wasm) ;;
  *) echo "Usage: $0 [all|native|wasm]" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_BUILD="$ROOT/build/native"
WASM_BUILD="$ROOT/build/wasm"

if [[ "$MODE" == "all" || "$MODE" == "native" ]]; then
  echo "==> Configuring native tests"
  cmake -S "$ROOT" -B "$NATIVE_BUILD" -DCMAKE_BUILD_TYPE=Debug
  echo "==> Building native tests"
  cmake --build "$NATIVE_BUILD" --config Debug
  echo "==> Running native tests"
  ctest --test-dir "$NATIVE_BUILD" --build-config Debug --output-on-failure
fi

if [[ "$MODE" == "native" ]]; then
  exit 0
fi

if ! command -v emcmake >/dev/null 2>&1 || ! command -v emcc >/dev/null 2>&1; then
  echo "Emscripten is not active. Run your emsdk environment script, then rerun:" >&2
  echo "  source <emsdk>/emsdk_env.sh" >&2
  exit 1
fi

echo "==> Configuring WASM demo"
emcmake cmake -S "$ROOT" -B "$WASM_BUILD" -DCMAKE_BUILD_TYPE=Debug
echo "==> Building WASM demo"
cmake --build "$WASM_BUILD" --config Debug

echo "==> Serving http://localhost:8080/index.html"
echo "Press Ctrl+C to stop the local server."
if command -v python3 >/dev/null 2>&1; then
  python3 -m http.server 8080 --directory "$WASM_BUILD"
elif command -v python >/dev/null 2>&1; then
  python -m http.server 8080 --directory "$WASM_BUILD"
elif command -v emrun >/dev/null 2>&1; then
  (cd "$WASM_BUILD" && emrun --no_browser --port 8080 index.html)
else
  echo "No local web server found. Install Python 3 or use emrun from Emscripten." >&2
  exit 1
fi
