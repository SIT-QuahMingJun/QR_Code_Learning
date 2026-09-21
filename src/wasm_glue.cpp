/*
@filename:  wasm_glue.cpp
@brief:     WASM bridge — exposes the pure C++ QR encoder to JavaScript.

            This is the ONLY file that touches Emscripten. It wraps the
            browser-agnostic qr_encoder.{h,cpp} behind a small set of
            `extern "C"` functions that JavaScript can call directly.

            The QrCode struct cannot cross the WASM boundary, so we store
            the latest result in a static buffer and let JS pull it out
            one module at a time:

                qr_encode(data, ecLevel)  -> version (or -1 on error)
                qr_get_size()             -> grid dimension (e.g. 21)
                qr_get_module(row, col)   -> 0 (light) or 1 (dark)
                qr_free()                 -> release the stored grid

            Compiled with Emscripten (emcc) into a modularized WASM module.
*/

#include "qr_encoder.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Static storage for the most recently encoded QR code.
// A single global is fine here: the browser only ever renders one QR at a
// time, and this keeps the JS interface dead simple.
// ---------------------------------------------------------------------------
static QrCode s_qr;
static bool s_hasQr = false;

// ---------------------------------------------------------------------------
// extern "C" interface (callable from JavaScript)
// ---------------------------------------------------------------------------

// Encodes `data` at the given EC level and stores the result.
//   ecLevel: 0=L, 1=M, 2=Q, 3=H
// Returns the QR version (1..10) on success, or -1 on error.
extern "C" int qr_encode(const char* data, int ecLevel) {
    if (data == nullptr) return -1;

    ErrorCorrectionLevel ec;
    switch (ecLevel) {
        case 0: ec = ErrorCorrectionLevel::L; break;
        case 1: ec = ErrorCorrectionLevel::M; break;
        case 2: ec = ErrorCorrectionLevel::Q; break;
        case 3: ec = ErrorCorrectionLevel::H; break;
        default: return -1;
    }

    try {
        s_qr = makeQRCode(std::string(data), ec);
        s_hasQr = true;
        return s_qr.version;
    } catch (...) {
        s_hasQr = false;
        return -1;
    }
}

// Returns the grid dimension (size x size) of the stored QR, or 0 if none.
extern "C" int qr_get_size() {
    return s_hasQr ? s_qr.size : 0;
}

// Returns 1 if the module at (row, col) is dark, 0 if light.
// Returns 0 (light) if out of range or no QR is stored.
extern "C" int qr_get_module(int row, int col) {
    if (!s_hasQr) return 0;
    if (row < 0 || col < 0 || row >= s_qr.size || col >= s_qr.size) return 0;
    return s_qr.modules[row * s_qr.size + col] ? 1 : 0;
}

// Releases the stored QR grid.
extern "C" void qr_free() {
    s_hasQr = false;
    s_qr = QrCode{};
}