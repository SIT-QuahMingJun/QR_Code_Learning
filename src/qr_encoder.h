/*
@filename:  qr_encoder.h
@brief:     QR Code encoder — pure C++ logic (no GL, no browser).
            Source: https://sumitbopche01.medium.com/understanding-qr-codes-a-detailed-exploration-ca62fa767a65
            2D Barcode (ISO / IEC 18004)

            This header is shared by:
              - Native test build (GoogleTest) — compiled with g++/clang
              - WebGL/WASM build (Emscripten)  — compiled with emcc
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

// ---------------------------------------------------------------------------
// Stage 1–2: Version + Error Correction level
// ---------------------------------------------------------------------------

// Error correction levels (ISO/IEC 18004). Higher = more damage tolerance, lower = less data capacity.
enum class ErrorCorrectionLevel {
    L,  // ~7%  recovery    Most data fits, least protection
    M,  // ~15% recovery    Balanced
    Q,  // ~25% recovery    More protection, less data
    H   // ~30% recovery    Most protection, least data
};

// Data encoding modes
enum class Mode {
    NUMERIC,      // 0001 — digits 0-9
    ALPHANUMERIC, // 0010 — 0-9 A-Z space $%*+-./:
    BYTE,         // 0100 — raw UTF-8 bytes
    KANJI         // 1000 — Shift-JIS
};

struct BlockGroup {
    int numBlocks;
    int dataCodewordsPerBlock;
};

struct VersionECInfo {
    int version;
    ErrorCorrectionLevel ec;
    int ecCodewordsPerBlock;
    BlockGroup group1;
    BlockGroup group2;  // may be empty
};

// For typical mobile apps, M for payment ig? Version is automatically chosen.
/*
+-----+----+----------+-----------+-----------------+-----------+-----------------+------------+

| Ver | EC | EC/Block | G1 Blocks | G1 Data / Block | G2 Blocks | G2 Data / Block | Total Data |
+-----+----+----------+-----------+-----------------+-----------+-----------------+------------+

|  1  | L  |    7     |     1     |       19        |    --     |       --        |     19     |
|  1  | M  |   10     |     1     |       16        |    --     |       --        |     16     |
|  1  | Q  |   13     |     1     |       13        |    --     |       --        |     13     |
|  1  | H  |   17     |     1     |        9        |    --     |       --        |      9     |
|  2  | L  |   10     |     1     |       34        |    --     |       --        |     34     |
|  2  | M  |   16     |     1     |       28        |    --     |       --        |     28     |
|  2  | Q  |   22     |     1     |       22        |    --     |       --        |     22     |
|  2  | H  |   28     |     1     |       16        |    --     |       --        |     16     |
|  3  | L  |   15     |     1     |       55        |    --     |       --        |     55     |
|  3  | M  |   26     |     1     |       44        |    --     |       --        |     44     |
|  3  | Q  |   18     |     2     |       17        |    --     |       --        |     34     |
|  3  | H  |   22     |     2     |       13        |    --     |       --        |     26     |
|  4  | L  |   20     |     1     |       80        |    --     |       --        |     80     |
|  4  | M  |   18     |     2     |       32        |    --     |       --        |     64     |
|  4  | Q  |   26     |     2     |       24        |    --     |       --        |     48     |
|  4  | H  |   16     |     4     |        9        |    --     |       --        |     36     |
|  5  | L  |   26     |     1     |      108        |    --     |       --        |    108     |
|  5  | M  |   24     |     2     |       43        |    --     |       --        |     86     |
|  5  | Q  |   18     |     2     |       15        |     2     |       16        |     62     |
|  5  | H  |   22     |     2     |       11        |     2     |       12        |     46     |
|  6  | L  |   18     |     2     |       68        |    --     |       --        |    136     |
|  6  | M  |   16     |     4     |       27        |    --     |       --        |    108     |
|  6  | Q  |   24     |     4     |       19        |    --     |       --        |     76     |
|  6  | H  |   28     |     4     |       15        |    --     |       --        |     60     |
|  7  | L  |   20     |     2     |       78        |    --     |       --        |    156     |
|  7  | M  |   18     |     4     |       31        |    --     |       --        |    124     |
|  7  | Q  |   18     |     2     |       14        |     4     |       15        |     88     |
|  7  | H  |   26     |     4     |       13        |     1     |       14        |     66     |
|  8  | L  |   24     |     2     |       97        |    --     |       --        |    194     |
|  8  | M  |   22     |     2     |       38        |     2     |       39        |    154     |
|  8  | Q  |   22     |     4     |       18        |     2     |       19        |    110     |
|  8  | H  |   26     |     4     |       14        |     2     |       15        |     86     |
|  9  | L  |   30     |     2     |      116        |    --     |       --        |    232     |
|  9  | M  |   22     |     3     |       36        |     2     |       37        |    182     |
|  9  | Q  |   20     |     4     |       16        |     4     |       17        |    132     |
|  9  | H  |   24     |     4     |       12        |     4     |       13        |    100     |
| 10  | L  |   18     |     2     |       68        |     2     |       69        |    274     |
| 10  | M  |   26     |     4     |       43        |     1     |       44        |    216     |
| 10  | Q  |   24     |     6     |       19        |     2     |       20        |    154     |
| 10  | H  |   28     |     6     |       15        |     2     |       16        |    122     |
+-----+----+----------+-----------+-----------------+-----------+-----------------+------------+
*/
const VersionECInfo kVersionTable[] = {
    // Version 1
    { 1, ErrorCorrectionLevel::L,  7, { 1, 19 }, { 0, 0 } },
    { 1, ErrorCorrectionLevel::M, 10, { 1, 16 }, { 0, 0 } },
    { 1, ErrorCorrectionLevel::Q, 13, { 1, 13 }, { 0, 0 } },
    { 1, ErrorCorrectionLevel::H, 17, { 1,  9 }, { 0, 0 } },

    // Version 2
    { 2, ErrorCorrectionLevel::L, 10, { 1, 34 }, { 0, 0 } },
    { 2, ErrorCorrectionLevel::M, 16, { 1, 28 }, { 0, 0 } },
    { 2, ErrorCorrectionLevel::Q, 22, { 1, 22 }, { 0, 0 } },
    { 2, ErrorCorrectionLevel::H, 28, { 1, 16 }, { 0, 0 } },

    // Version 3
    { 3, ErrorCorrectionLevel::L, 15, { 1, 55 }, { 0, 0 } },
    { 3, ErrorCorrectionLevel::M, 26, { 1, 44 }, { 0, 0 } },
    { 3, ErrorCorrectionLevel::Q, 18, { 2, 17 }, { 0, 0 } },
    { 3, ErrorCorrectionLevel::H, 22, { 2, 13 }, { 0, 0 } },

    // Version 4
    { 4, ErrorCorrectionLevel::L, 20, { 1, 80 }, { 0, 0 } },
    { 4, ErrorCorrectionLevel::M, 18, { 2, 32 }, { 0, 0 } },
    { 4, ErrorCorrectionLevel::Q, 26, { 2, 24 }, { 0, 0 } },
    { 4, ErrorCorrectionLevel::H, 16, { 4,  9 }, { 0, 0 } },

    // Version 5  ← group2 appears here
    { 5, ErrorCorrectionLevel::L, 26, { 1, 108 }, { 0, 0 } },
    { 5, ErrorCorrectionLevel::M, 24, { 2,  43 }, { 0, 0 } },
    { 5, ErrorCorrectionLevel::Q, 18, { 2,  15 }, { 2, 16 } },
    { 5, ErrorCorrectionLevel::H, 22, { 2,  11 }, { 2, 12 } },

    // Version 6
    { 6, ErrorCorrectionLevel::L, 18, { 2,  68 }, { 0, 0 } },
    { 6, ErrorCorrectionLevel::M, 16, { 4,  27 }, { 0, 0 } },
    { 6, ErrorCorrectionLevel::Q, 24, { 4,  19 }, { 0, 0 } },
    { 6, ErrorCorrectionLevel::H, 28, { 4,  15 }, { 0, 0 } },

    // Version 7
    { 7, ErrorCorrectionLevel::L, 20, { 2,  78 }, { 0, 0 } },
    { 7, ErrorCorrectionLevel::M, 18, { 4,  31 }, { 0, 0 } },
    { 7, ErrorCorrectionLevel::Q, 18, { 2,  14 }, { 4, 15 } },
    { 7, ErrorCorrectionLevel::H, 26, { 4,  13 }, { 1, 14 } },

    // Version 8
    { 8, ErrorCorrectionLevel::L, 24, { 2,  97 }, { 0, 0 } },
    { 8, ErrorCorrectionLevel::M, 22, { 2,  38 }, { 2, 39 } },
    { 8, ErrorCorrectionLevel::Q, 22, { 4,  18 }, { 2, 19 } },
    { 8, ErrorCorrectionLevel::H, 26, { 4,  14 }, { 2, 15 } },

    // Version 9
    { 9, ErrorCorrectionLevel::L, 30, { 2, 116 }, { 0, 0 } },
    { 9, ErrorCorrectionLevel::M, 22, { 3,  36 }, { 2, 37 } },
    { 9, ErrorCorrectionLevel::Q, 20, { 4,  16 }, { 4, 17 } },
    { 9, ErrorCorrectionLevel::H, 24, { 4,  12 }, { 4, 13 } },

    // Version 10
    { 10, ErrorCorrectionLevel::L, 18, { 2,  68 }, { 2, 69 } },
    { 10, ErrorCorrectionLevel::M, 26, { 4,  43 }, { 1, 44 } },
    { 10, ErrorCorrectionLevel::Q, 24, { 6,  19 }, { 2, 20 } },
    { 10, ErrorCorrectionLevel::H, 28, { 6,  15 }, { 2, 16 } },
};

// Stage 1–2 function declarations

// Returns the module grid size for a version: size = 17 + 4 * version.
// E.g. version 1 → 21, version 2 → 25, version 10 → 57.
int moduleSize(int version);

// Returns the total number of data codewords for a version + EC level.
// Sums group1 and group2 data codewords from kVersionTable.
int dataCodewords(int version, ErrorCorrectionLevel ec);

// Returns the smallest version (1..40) that can hold `data` at `ec` level.
// Throws std::invalid_argument if data is too long for version 10.
int chooseVersion(const std::string& data, ErrorCorrectionLevel ec);

// ---------------------------------------------------------------------------
// Stage 3: Data encoding
// ---------------------------------------------------------------------------

// Encodes `data` into a bit stream (mode indicator + count + data bits).
// Returns the packed bytes. Padding to full codewords is done later.
std::vector<uint8_t> encodeData(const std::string& data, Mode mode, int version);

// Adds terminator (up to 4 zero bits), pads to byte boundary, then fills
// remaining capacity with alternating 0xEC/0x11 pad bytes.
// `dataBits` is the raw bit stream from encodeData; `bitCount` is the exact
// number of valid bits in it (the last byte may be partially filled).
// Returns the complete data codewords for the given version + EC level.
std::vector<uint8_t> finalizeData(const std::vector<uint8_t>& dataBits, int bitCount,
                                  int version, ErrorCorrectionLevel ec);

// Stage 4: Reed-Solomon error correction

// Computes `ecCodewords` error correction codewords for `data` using
// Reed-Solomon over GF(256). Returns the EC codewords.
std::vector<uint8_t> reedSolomon(const std::vector<uint8_t>& data, int ecCodewords);

// Splits the finalized data codewords into blocks (per kVersionTable),
// computes Reed-Solomon EC codewords for EACH block separately, then
// interleaves the data codewords and the EC codewords as required by
// ISO/IEC 18004. Returns the final codeword stream ready for placement.
// For single-block versions this reduces to [data][EC] concatenation.
std::vector<uint8_t> interleaveAndEcc(const std::vector<uint8_t>& data,
                                      int version, ErrorCorrectionLevel ec);

// Growing stream helper
struct BitBuffer {
    std::vector<uint8_t> bytes;
    int bitCount = 0;

    // Note: QR Code stores information in big-endian order
    void append(int value, int numBits) {
        for (int i = numBits - 1; i >= 0; --i) {
            if (bitCount % 8 == 0) bytes.push_back(0);
            if ((value >> i) & 1) bytes[bitCount / 8] |= (1 << (7 - bitCount % 8));
            bitCount++;
        }
    }
};







// The final QR matrix (module grid).
struct QrCode {
    int version;                    // which size
    int size;                       // 21, 25, 29... (17 + 4*version)
    std::vector<uint8_t> modules;   // the actual grid of dark/light squares
    std::vector<uint8_t> reserved;  // 1 = function pattern (don't write data here)
};

// ---------------------------------------------------------------------------
// Stage 5: Build the module matrix (function patterns)
// ---------------------------------------------------------------------------

// Creates an empty QrCode grid of the correct size for `version`.
QrCode createMatrix(int version);

// Draws a 7x7 finder pattern and its required one-module white separator.
// The whole pattern/separator region is reserved from data placement.
void drawFinder(QrCode& qr, int row, int col);

// Draws the timing patterns (alternating dark/light along row 6 and col 6).
void drawTiming(QrCode& qr);

// Draws the alignment patterns (5x5 squares) at their version-dependent positions.
void drawAlignment(QrCode& qr);

// Sets the always-dark module at (row size - 8, col 8).
void drawDarkModule(QrCode& qr);

// Reserves the locations for the two copies of the 15-bit format information.
// Reserve these before placing payload data; writeFormatInfo() fills them later.
void reserveFormatInfoAreas(QrCode& qr);

// Reserves the locations for the two copies of the 18-bit version information.
// This is needed only for versions 7 and above.
void reserveVersionInfoAreas(QrCode& qr);

// Returns true if the module at (row, col) is dark.
bool isDark(const QrCode& qr, int row, int col);

// Sets the module at (row, col) to dark (true) or light (false).
void setModule(QrCode& qr, int row, int col, bool dark);

// Marks a module as reserved (part of a function pattern).
// Reserved modules are skipped when placing data.
void reserveModule(QrCode& qr, int row, int col);

// Returns true if the module is reserved (part of a function pattern).
bool isReserved(const QrCode& qr, int row, int col);

// ---------------------------------------------------------------------------
// Stage 6: Place data bits (zigzag)
// ---------------------------------------------------------------------------

// Places the data + EC codewords into the grid in a zigzag pattern,
// skipping over function patterns. `data` is the full codeword stream
// (data codewords followed by EC codewords).
void placeData(QrCode& qr, const std::vector<uint8_t>& data);

// ---------------------------------------------------------------------------
// Stage 7: Data masking
// ---------------------------------------------------------------------------

// Applies mask pattern `maskId` (0-7) to the data region of the grid.
// Function patterns are NOT masked.
void applyMask(QrCode& qr, int maskId);

// Computes the penalty score for the current grid (lower is better).
// Used to pick the best mask pattern.
int penaltyScore(const QrCode& qr);

// Individual penalty rules (exposed for testing).
// Rule 3: finder-like patterns (1:1:3:1:1) with 4 lights on one side.
int penaltyRule3(const QrCode& qr);
// Rule 4: distance of dark-module % from 50%, in 5% steps, +10 each.
int penaltyRule4(const QrCode& qr);

// Tries all 8 masks, writes each candidate's format bits before scoring,
// and applies the best one. The final format bits are written separately.
// Returns the chosen mask ID (0-7).
int chooseAndApplyMask(QrCode& qr, ErrorCorrectionLevel ec);

// ---------------------------------------------------------------------------
// Stage 8: Format information
// ---------------------------------------------------------------------------

// Computes the 15-bit format information string for the given EC level and
// mask ID. Includes BCH error correction and the 0x5412 mask.
int formatInfo(ErrorCorrectionLevel ec, int maskId);

// Writes the format information into the grid at both locations
// (around the top-left finder, and split near top-right/bottom-left).
void writeFormatInfo(QrCode& qr, ErrorCorrectionLevel ec, int maskId);

// ---------------------------------------------------------------------------
// Stage 9: Version information (only for version >= 7)
// ---------------------------------------------------------------------------

// Computes the 18-bit version information string for `version`.
// Includes 12 BCH error correction bits (generator 0x1F25).
int versionInfo(int version);

// Writes the version information into the grid at both locations
// (near the top-right and bottom-left finders). No-op for version < 7.
void writeVersionInfo(QrCode& qr, int version);

// ---------------------------------------------------------------------------
// Stage 10: Render (orchestrator)
// ---------------------------------------------------------------------------

// Determines the best encoding mode for `data` (numeric, alphanumeric, byte).
Mode detectMode(const std::string& data);

// Builds a complete QR code from `data` at the given EC level.
// Runs all 9 stages in sequence and returns the finished grid.
QrCode makeQRCode(const std::string& data, ErrorCorrectionLevel ec);
