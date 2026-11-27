# QR Code Learning

A personal C++ project I built for fun while learning how QR codes work. It turns text into a QR code and includes a browser demo using WebAssembly.

[Try the live demo](https://sit-quahmingjun.github.io/QR_Code_Learning/)

## What it does

- Detects numeric, alphanumeric and byte encoding modes.
- Selects a QR version from 1–10 based on the input and error-correction level.
- Generates Reed–Solomon error-correction data.
- Places the data into a QR matrix and compares eight mask patterns.
- Supports L, M, Q and H error-correction levels.
- Displays the result in a browser with a four-module white border.

The browser interface accepts up to 200 characters. Actual capacity depends on the encoding mode and error-correction level.

## How it is organised

The encoder is separate from the browser interface. The same C++ logic is used by the native tests and the WebAssembly build.

- `src/qr_encoder.cpp` — QR encoding implementation.
- `src/qr_encoder.h` — shared types and encoder interface.
- `src/wasm_glue.cpp` — bridge between JavaScript and C++.
- `web/index.html` — input controls and canvas rendering.
- `tests/` — smoke tests and an optional GoogleTest suite.

## Run the native tests

Requires CMake 3.16 or newer and a C++17 compiler.

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --config Debug
ctest --test-dir build/native -C Debug --output-on-failure
```

The default smoke tests do not require GoogleTest. To enable the additional GoogleTest suite, configure with `-DQR_FETCH_GTEST=ON`; its dependency must be available.

## Run the browser demo locally

Requires an activated Emscripten SDK, CMake and Python 3.

```sh
emcmake cmake -S . -B build/wasm -DCMAKE_BUILD_TYPE=Debug
cmake --build build/wasm --config Debug
python -m http.server 8080 --directory build/wasm
```

Open http://localhost:8080/index.html.

## Testing

The smoke tests check matrix structure, encoding across error-correction levels, and reference matrix hashes for selected inputs. They also include deterministic byte-mode inputs covering versions 1–10.

## A deployment issue

The commit history records a GitHub Pages build issue involving an ignored HTML file. The deployment was updated to use the tracked `web/index.html`.

## Scope

This is a learning project. Its capacity table covers QR versions 1–10, and automatic mode selection uses numeric, alphanumeric or byte encoding.

## License

CC0-1.0 — see `LICENSE`.
