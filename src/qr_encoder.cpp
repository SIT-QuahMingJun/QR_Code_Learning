/*
@filename:  qr_encoder.cpp
@brief:     QR Code encoder — Stage 1–2: Version + Error Correction level.
            Source: https://sumitbopche01.medium.com/understanding-qr-codes-a-detailed-exploration-ca62fa767a65
            2D Barcode (ISO / IEC 18004)

Pipeline:
1.  Input data (E.g., order-1234)
2.  Choose version + EC Level
3.  Encoded data bits
4.  Reed-Solomon, ecc
5.  Build module matrix
6.  Place data bits in zig zag
7.  Apply mask
8.  Format info
9.  Version info
10. Render Final QR Matrix
*/

#include "qr_encoder.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <stdexcept>

namespace {

// ---------------------------------------------------------------------------
// ISO/IEC 18004 capacity table (abridged — versions 1..10).
// Each entry: { version, EC level, EC codewords per block,
//              group1 {blocks, data per block}, group2 {blocks, data per block} }
//
// group2 is {0, 0} when there is no second group (versions 1-4, and most
// single-group cases). For versions >= 5, some EC levels split data into
// two groups with different block sizes.
// ---------------------------------------------------------------------------

// Retrieves tablerow given version and error level
const VersionECInfo* findRow(int version, ErrorCorrectionLevel ec) {
    for (const VersionECInfo& row : kVersionTable) {
        if (row.version == version && row.ec == ec) return &row;
    }
    return nullptr;
}

// Reduce wasted bits
// Character count indicator bit width, per mode and version range.
// This is a separate ISO rule from the capacity table:
//   versions 1-9, 10-26, 27-40 each use different widths.
int countBitsForMode(Mode mode, int version) {
    if (version <= 9) {
        switch (mode) {
            case Mode::NUMERIC:      return 10;
            case Mode::ALPHANUMERIC: return 9;
            case Mode::BYTE:         return 8;
            case Mode::KANJI:        return 8;
        }
    } else if (version <= 26) {
        switch (mode) {
            case Mode::NUMERIC:      return 12;
            case Mode::ALPHANUMERIC: return 11;
            case Mode::BYTE:         return 16;
            case Mode::KANJI:        return 10;
        }
    } else {
        switch (mode) {
            case Mode::NUMERIC:      return 14;
            case Mode::ALPHANUMERIC: return 13;
            case Mode::BYTE:         return 16;
            case Mode::KANJI:        return 12;
        }
    }
    return 0;
}

// Number of data bits needed to encode `data` in `mode` (excluding the
// 4-bit mode indicator and the character-count field).
int dataBitsForLength(const std::string& data, Mode mode) {
    switch (mode) {
        case Mode::NUMERIC: {
            int n = static_cast<int>(data.size());
            return (n / 3) * 10 + (n % 3 == 2 ? 7 : (n % 3 == 1 ? 4 : 0));
        }
        case Mode::ALPHANUMERIC: {
            int n = static_cast<int>(data.size());
            return (n / 2) * 11 + (n % 2) * 6;
        }
        case Mode::BYTE:
            return static_cast<int>(data.size()) * 8;
        case Mode::KANJI:
            return static_cast<int>(data.size()) * 13;
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Stage 1–2 implementation
// ---------------------------------------------------------------------------

// The QR grid is always 17 + 4 * version modules per side.
// Version 1 = 21x21, version 2 = 25x25, ..., version 40 = 177x177.
int moduleSize(int version) {
    // Gridsize = 21 + 4 x (Version - 1) -> Evaluates directly to: 17 + 4 * version

    // Fixed
    // 3 Position markers: (7 x 7 each) squares located at Top-Left, Top-Right, Bottom-Left corners.
    // 1 Alignment pattern: 5x5 squares (introduced in Version 2+). Version 2 has 1; higher versions have more.
    // 3 Separators: 1-module wide white borders that fully insulate the 3 Position Markers.
    // Timing Pattern: Row 6 and Column 6 (0-indexed). Long tracks of strictly alternating Black/White modules.    //  Format information: 
    
    // Format & System Metadata modules
    // Format information: 30 total modules (a 15-bit configuration string duplicated exactly twice). Encodes information about the QR code’s error correction level (2 bits), ecc bits (10 bits), and masking pattern (3 bits).
    // Version information: 36 Version information: 36 total modules (an 18-bit configuration string duplicated twice). explicit matrix size validation for large QR codes.

    // Data and ECC
    // The remaining unreserved modules on the grid grid: App payload + ReedSolomon ECC. Scrambled dynamically using 1/8 standard math mask pattern

    return 17 + 4 * version;
}

// Total data codewords = (group1 blocks * group1 data per block) + (group2 blocks * group2 data per block).
// E.g. version 5-Q: (2 * 15) + (2 * 16) = 62.
int dataCodewords(int version, ErrorCorrectionLevel ec) {
    const VersionECInfo* row = findRow(version, ec);
    if (row == nullptr) {
        throw std::invalid_argument("Unknown version/EC combination");
    }

    return row->group1.numBlocks * row->group1.dataCodewordsPerBlock + row->group2.numBlocks * row->group2.dataCodewordsPerBlock;
}

// REALLY IMPORTANT FUNCTION FOR PART 1
// Choose the smallest version that can hold the data at the given EC level.
// We estimate the number of bits needed, then scan the table from version 1
// upward until the data codewords (each = 8 bits) are enough.
int chooseVersion(const std::string& data, ErrorCorrectionLevel ec) {
    const Mode mode = detectMode(data);

    // Estimate total bits: mode indicator (4) + count bits + data bits.
    // (We'll refine this in Stage 3; for now a rough estimate is enough to
    // pick the version.)
    int dataBits = 0;
    switch (mode) {
        case Mode::NUMERIC:
            // Groups of 3 digits → 10 bits; 2 digits → 7 bits; 1 digit → 4 bits
            // Example: 104 | 00
            dataBits = (static_cast<int>(data.size()) / 3) * 10 + (data.size() % 3 == 2 ? 7 : (data.size() % 3 == 1 ? 4 : 0));
            break;
        case Mode::ALPHANUMERIC:
            // Pairs → 11 bits; single leftover → 6 bits
            dataBits = (static_cast<int>(data.size()) / 2) * 11 + (data.size() % 2) * 6;
            break;
        case Mode::BYTE:
            dataBits = static_cast<int>(data.size()) * 8;
            break;
        // I mean it is created by Denso Wave so
        case Mode::KANJI:
            dataBits = static_cast<int>(data.size()) * 13;
            break;
    }

    // A terminator is added later only when capacity remains. Do not reserve a
    // mandatory four bits here, or a payload that exactly fills a version will
    // be unnecessarily promoted to a larger QR version.
    // Count bits depend on version range; compute per version so we don't
    // overestimate and pick a larger version than necessary.
    for (const auto& row : kVersionTable) {
        if (row.ec != ec) continue;
        int countBits = countBitsForMode(mode, row.version);
        int totalBits = 4 + countBits + dataBits;
        int capacityBits = dataCodewords(row.version, ec) * 8;
        if (totalBits <= capacityBits) {
            return row.version;
        }
    }

    throw std::invalid_argument("Data too long for QR code (max version 10 in this table)");
}

// Stage 3: Data encoding

// Alphanumeric character table (ISO/IEC 18004). Index = value.
// 0-9, A-Z, space, $%*+-./:
const std::string kAlnumChars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// Returns the alphanumeric value of a character, or -1 if invalid.
int alnumValue(char c) {
    auto pos = kAlnumChars.find(c);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

// Encodes `data` into a bit stream: mode indicator + count + data bits.
// Returns the packed bytes (not yet padded to full codewords).
std::vector<uint8_t> encodeData(const std::string& data, Mode mode, int version) {
    /*
    Example :   "HELLO WORLD" -> Big endian (version 1-Q, alphanumeric)
    [Mode: 0010] [Count: 000001011] [Data: 61 bits] [Terminator: 0000] [Pad-to-byte: 00] [Pad bytes: 0xEC 0x11 0xEC]

    NOTE: mode/count/data are bit-packed ACROSS byte boundaries (not one byte each).
    The 8-bit codewords are:
                [00100000]  -   mode(0010) + first 4 bits of count
                [01011 011]  -   last 5 bits of count + start of data
                [00001011]  -   data (continues)
                [01111000]  -   data
                [11010001]  -   data
                [01110010]  -   data
                [11011100]  -   data
                [01001101]  -   data
                [01000011]  -   data
                [01 0000 00]  -   data ends here (61 bits) + terminator(0000) + pad-to-byte(00)
                [11101100]  -   pad byte 1 (0xEC)
                [00010001]  -   pad byte 2 (0x11)
                [11101100]  -   pad byte 3 (0xEC)
    */

    BitBuffer bits;

    // 1. Mode indicator (4 bits)
    switch (mode) {
        case Mode::NUMERIC:      bits.append(0b0001, 4); break;
        case Mode::ALPHANUMERIC: bits.append(0b0010, 4); break;
        case Mode::BYTE:         bits.append(0b0100, 4); break;
        case Mode::KANJI:        bits.append(0b1000, 4); break;
    }

    // 2. Character count indicator
    bits.append(static_cast<int>(data.size()), countBitsForMode(mode, version));

    // 3. Data bits
    switch (mode) {
        case Mode::NUMERIC: {
            // Groups of 3 digits → 10 bits; 2 → 7 bits; 1 → 4 bits
            int i = 0;
            while (i < static_cast<int>(data.size())) {
                int remaining = static_cast<int>(data.size()) - i;
                if (remaining >= 3) {
                    int val = (data[i] - '0') * 100 + (data[i+1] - '0') * 10 + (data[i+2] - '0');
                    bits.append(val, 10);
                    i += 3;
                } else if (remaining == 2) {
                    int val = (data[i] - '0') * 10 + (data[i+1] - '0');
                    bits.append(val, 7);
                    i += 2;
                } else {
                    int val = data[i] - '0';
                    bits.append(val, 4);
                    i += 1;
                }
            }
            break;
        }
        case Mode::ALPHANUMERIC: {
            // Pairs → 11 bits; single leftover → 6 bits
            int i = 0;
            while (i < static_cast<int>(data.size())) {
                int remaining = static_cast<int>(data.size()) - i;
                if (remaining >= 2) {
                    int v1 = alnumValue(data[i]);
                    int v2 = alnumValue(data[i+1]);
                    if (v1 < 0 || v2 < 0) throw std::invalid_argument("Invalid alphanumeric char");
                    bits.append(v1 * 45 + v2, 11);
                    i += 2;
                } else {
                    int v1 = alnumValue(data[i]);
                    if (v1 < 0) throw std::invalid_argument("Invalid alphanumeric char");
                    bits.append(v1, 6);
                    i += 1;
                }
            }
            break;
        }
        case Mode::BYTE: {
            // Each byte → 8 bits (raw UTF-8)
            for (unsigned char c : data) {
                bits.append(c, 8);
            }
            break;
        }
        case Mode::KANJI: {
            throw std::invalid_argument("Kanji mode not implemented yet");
        }
    }

    return bits.bytes;
}

// ---------------------------------------------------------------------------
// Stage 3: Terminator + padding (finalize the data codewords)
// ---------------------------------------------------------------------------

// Adds terminator (up to 4 zero bits), pads to byte boundary, then fills
// remaining capacity with alternating 0xEC/0x11 pad bytes.
std::vector<uint8_t> finalizeData(const std::vector<uint8_t>& dataBits, int bitCount,
                                  int version, ErrorCorrectionLevel ec) {
    BitBuffer bits;
    // Copy the existing data bits into a fresh BitBuffer so we can append.
    // Only copy the valid bits (bitCount), not the byte-padded zeros.
    for (int i = 0; i < bitCount; ++i) {
        int byteIdx = i / 8;
        int bitIdx = 7 - (i % 8);
        int bit = (dataBits[byteIdx] >> bitIdx) & 1;
        bits.append(bit, 1);
    }

    int usedBits = bits.bitCount;
    int capacityBits = dataCodewords(version, ec) * 8;
    if (usedBits > capacityBits) {
        throw std::invalid_argument("Encoded data exceeds QR capacity");
    }

    // 1. Terminator: up to 4 zero bits (only what fits in remaining capacity).
    int terminatorBits = std::min(4, capacityBits - usedBits);
    if (terminatorBits > 0) {
        bits.append(0, terminatorBits);
        usedBits += terminatorBits;
    }

    // 2. Pad to byte boundary with zeros.
    while (usedBits % 8 != 0 && usedBits < capacityBits) {
        bits.append(0, 1);
        usedBits++;
    }

    // 3. Pad bytes: alternate 0xEC, 0x11 until capacity is full.
    int padByteIndex = 0;
    while (usedBits < capacityBits) {
        uint8_t padByte = (padByteIndex % 2 == 0) ? 0xEC : 0x11;
        bits.append(padByte, 8);
        usedBits += 8;
        padByteIndex++;
    }

    return bits.bytes;
}

// ---------------------------------------------------------------------------
// Stage 4: Reed-Solomon error correction (Ngl idrg this part)
// ---------------------------------------------------------------------------

namespace {

// GF(256) log and antilog tables.
// Every number 0-255 can be written as 2^n. log[x] = n, antilog[n] = 2^n.
// Built using the primitive polynomial 0x11D (x^8 + x^4 + x^3 + x^2 + 1).
int gfLog[256];
int gfAntilog[256];
bool gfTablesBuilt = false;

// Build the GF(256) log/antilog tables once.
void buildGfTables() {
    if (gfTablesBuilt) return;
    int x = 1;
    for (int i = 0; i < 255; ++i) {
        gfAntilog[i] = x;
        gfLog[x] = i;
        x <<= 1;                    // multiply by 2
        if (x & 0x100) x ^= 0x11D;  // reduce mod primitive polynomial
    }
    gfTablesBuilt = true;
}

// Multiply two numbers in GF(256).
int gfMul(int a, int b) {
    if (a == 0 || b == 0) return 0;
    buildGfTables();
    return gfAntilog[(gfLog[a] + gfLog[b]) % 255];
}

// Raise a number to a power in GF(256).
int gfPow(int a, int n) {
    buildGfTables();
    return gfAntilog[(gfLog[a] * n) % 255];
}

// Build the generator polynomial of the given degree.
// g(x) = (x - a^0)(x - a^1)...(x - a^(degree-1))
// Returns coefficients, highest degree first.
std::vector<int> generatorPoly(int degree) {
    buildGfTables();
    std::vector<int> result(1, 1);  // start with g(x) = 1 (constant term)

    for (int i = 0; i < degree; ++i) {
        // Multiply current polynomial by (x - a^i)
        std::vector<int> next(result.size() + 1, 0);
        for (size_t j = 0; j < result.size(); ++j) {
            // term * x
            next[j] ^= result[j];
            // term * (-a^i) = term * a^i (since -x == x in GF(256))
            next[j + 1] ^= gfMul(result[j], gfPow(2, i));
        }
        result = next;
    }
    return result;
}

}  // namespace

// Computes `ecCodewords` error correction codewords for `data`. Uses polynomial division: remainder of (data * x^n) / generator.
// WHY REED-SOLOMON : A QR code can get scratched/damaged. RS adds redundancy so a scanner can detect AND fix errors. The EC codewords are the "spare parts."
std::vector<uint8_t> reedSolomon(const std::vector<uint8_t>& data, int ecCodewords) {
    /*
    Example :   "HELLO WORLD" -> data codewords -> Reed-Solomon error correction

    "HELLO WORLD"
        ↓
    encodeData()
        ↓
    1. Data codewords: 20 5B 0B 78 D1 72 DC 4D 43 00 EC 11 EC

    2. These 13 bytes become the coefficients of the message polynomial M(x):

        M(x) = 20x^12 + 5Bx^11 + 0Bx^10 + ... + EC

    3. The generator polynomial G(x) has degree 13 because Version 1-Q requires 13 error-correction codewords:

        G(x) = (x - a^0)(x - a^1)(x - a^2)...(x - a^12)

    4. Shift the message polynomial left by 13 positions:

        M(x) * x^13
        
        This reserves 13 codewords for the error-correction remainder.

    5. Then perform polynomial long division in GF(256):

        M(x) * x^13
        ----------------
             G(x)

        The remainder R(x) has degree < 13 and contains the 13 error-correction codewords:

        R(x) = [13 EC codewords]

    6. Final QR codeword stream:

        [13 data codewords] [13 EC codewords]

    NOTE: The 13-position shift is a polynomial operation: M(x) * x^13, not a bit/byte shift of the original data.
    */

    buildGfTables();

    // G(x): the fixed divisor, degree = number of EC codewords we want.
    // e.g. 10 EC codewords → G(x) has degree 10 (11 coefficients).
    std::vector<int> gen = generatorPoly(ecCodewords);

    // M(x): the message polynomial = data bytes as coefficients,
    // shifted left by ecCodewords (multiply by x^n) to reserve space
    // for the EC remainder at the end.
    std::vector<int> msg(data.size() + ecCodewords, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        msg[i] = data[i];
    }

    // Polynomial long division in GF(256).
    // For each data coefficient, XOR the (scaled) generator into the message.
    // This is the same as normal long division, but:
    //   - "subtract" = XOR (since -x == x in GF(256))
    //   - "scale" = gfMul (GF multiplication)
    // After processing all data terms, the leftover low-degree terms are the
    // remainder = EC codewords.
    for (size_t i = 0; i < data.size(); ++i) {
        int coef = msg[i];          // current leading coefficient
        if (coef == 0) continue;    // nothing to cancel, skip
        for (size_t j = 0; j < gen.size(); ++j) {
            // XOR the generator (scaled by coef) into the message at position i.
            msg[i + j] ^= gfMul(gen[j], coef);
        }
    }

    // The remainder: the last ecCodewords terms of msg.
    // These are the error correction codewords appended after the data.
    std::vector<uint8_t> ecc(ecCodewords);
    for (int i = 0; i < ecCodewords; ++i) {
        ecc[i] = static_cast<uint8_t>(msg[data.size() + i]);
    }
    return ecc;
}

// Splits the finalized data codewords into blocks, computes EC per block,
// then interleaves data and EC codewords per ISO/IEC 18004.
std::vector<uint8_t> interleaveAndEcc(const std::vector<uint8_t>& data,
                                      int version, ErrorCorrectionLevel ec) {
    const VersionECInfo* row = findRow(version, ec);
    if (row == nullptr) {
        throw std::invalid_argument("Unknown version/EC combination");
    }

    const int g1Blocks = row->group1.numBlocks;
    const int g1Data = row->group1.dataCodewordsPerBlock;
    const int g2Blocks = row->group2.numBlocks;
    const int g2Data = row->group2.dataCodewordsPerBlock;
    const int ecPerBlock = row->ecCodewordsPerBlock;
    const int totalBlocks = g1Blocks + g2Blocks;
    if (static_cast<int>(data.size()) != dataCodewords(version, ec)) {
        throw std::invalid_argument("Unexpected data-codeword count for QR version/EC level");
    }

    // Split the data stream into per-block slices.
    // group1 blocks come first (each g1Data bytes), then group2 (g2Data bytes).
    std::vector<std::vector<uint8_t>> blocks(totalBlocks);
    int offset = 0;
    for (int b = 0; b < g1Blocks; ++b) {
        blocks[b].assign(data.begin() + offset, data.begin() + offset + g1Data);
        offset += g1Data;
    }
    for (int b = 0; b < g2Blocks; ++b) {
        blocks[g1Blocks + b].assign(data.begin() + offset, data.begin() + offset + g2Data);
        offset += g2Data;
    }

    // Compute EC codewords for each block independently.
    std::vector<std::vector<uint8_t>> ecc(totalBlocks);
    for (int b = 0; b < totalBlocks; ++b) {
        ecc[b] = reedSolomon(blocks[b], ecPerBlock);
    }

    // Interleave the data codewords: take byte 0 of every block, then byte 1,
    // etc. group2 blocks are shorter, so they run out first.
    std::vector<uint8_t> result;
    int maxData = std::max(g1Data, g2Data);
    for (int i = 0; i < maxData; ++i) {
        for (int b = 0; b < totalBlocks; ++b) {
            if (i < static_cast<int>(blocks[b].size())) {
                result.push_back(blocks[b][i]);
            }
        }
    }

    // Interleave the EC codewords: byte 0 of every block's EC, then byte 1, etc.
    for (int i = 0; i < ecPerBlock; ++i) {
        for (int b = 0; b < totalBlocks; ++b) {
            result.push_back(ecc[b][i]);
        }
    }

    return result;
}

// Stage 5: Build the module matrix (function patterns)

// Creates an empty QrCode grid of the correct size for `version`.
// All modules start light (0). Function patterns are drawn next.
QrCode createMatrix(int version) {
    QrCode qr;
    qr.version = version;
    qr.size = moduleSize(version);
    qr.modules.assign(qr.size * qr.size, 0);   // all light
    qr.reserved.assign(qr.size * qr.size, 0);  // nothing reserved yet
    return qr;
}

// Returns true if the module at (row, col) is dark.
bool isDark(const QrCode& qr, int row, int col) {
    return qr.modules[row * qr.size + col] != 0;
}

// Sets the module at (row, col) to dark (true) or light (false).
void setModule(QrCode& qr, int row, int col, bool dark) {
    qr.modules[row * qr.size + col] = dark ? 1 : 0;
}

// Marks a module as reserved (part of a function pattern).
void reserveModule(QrCode& qr, int row, int col) {
    qr.reserved[row * qr.size + col] = 1;
}

// Returns true if the module is reserved (part of a function pattern).
bool isReserved(const QrCode& qr, int row, int col) {
    return qr.reserved[row * qr.size + col] != 0;
}

// Draws a 7x7 finder pattern (the corner squares) at (row, col), plus its
// one-module white separator. The separator is a function area: it must be
// reserved, not left as an arbitrary light data cell.
// The pattern is a dark border, a light ring, and a dark center:
//   1111111
//   1000001
//   1011101
//   1011101
//   1011101
//   1000001
//   1111111
void drawFinder(QrCode& qr, int row, int col) {
    for (int r = -1; r <= 7; ++r) {
        for (int c = -1; c <= 7; ++c) {
            const int matrixRow = row + r;
            const int matrixCol = col + c;
            if (matrixRow < 0 || matrixRow >= qr.size || matrixCol < 0 || matrixCol >= qr.size) {
                continue;
            }

            const bool insideFinder = r >= 0 && r < 7 && c >= 0 && c < 7;
            const bool dark = insideFinder &&
                ((r == 0 || r == 6 || c == 0 || c == 6) || (r >= 2 && r <= 4 && c >= 2 && c <= 4));
            setModule(qr, matrixRow, matrixCol, dark);
            reserveModule(qr, matrixRow, matrixCol);
        }
    }
}

// Draws the timing patterns: alternating dark/light along row 6 and col 6.
// These run between the finder patterns and help the scanner measure the grid.
void drawTiming(QrCode& qr) {
    for (int i = 8; i < qr.size - 8; ++i) {
        bool dark = (i % 2 == 0);
        setModule(qr, 6, i, dark);  // horizontal timing (row 6)
        setModule(qr, i, 6, dark);  // vertical timing (col 6)
        reserveModule(qr, 6, i);    // timing is a function pattern
        reserveModule(qr, i, 6);
    }
}

// Draws the alignment patterns (5x5 squares) at their version-dependent positions.
// Alignment patterns help correct distortion when scanning at an angle.
//
// Every version >= 2 has alignment-pattern CENTER POSITIONS defined by the
// standard. Some candidate patterns are skipped because they overlap the
// finder patterns (e.g. V2's (6,6) overlaps the top-left finder, so V2 ends
// up with only ONE visible alignment pattern at (18,18)).
//
// The center positions come from a fixed lookup table (ISO/IEC 18004), NOT
// from a simple arithmetic formula.
void drawAlignment(QrCode& qr) {
    if (qr.version < 2) return;

    // Alignment pattern center coordinates per version (ISO/IEC 18004).
    // Index = version. Each row is the list of center coordinates.
    static const int kAlignmentTable[][7] = {
        /* v1 */ { 0 },
        /* v2 */ { 6, 18 },
        /* v3 */ { 6, 22 },
        /* v4 */ { 6, 26 },
        /* v5 */ { 6, 30 },
        /* v6 */ { 6, 34 },
        /* v7 */ { 6, 22, 38 },
        /* v8 */ { 6, 24, 42 },
        /* v9 */ { 6, 26, 46 },
        /* v10 */ { 6, 28, 50 },
    };

    // Number of coordinates for each version (0 for v1).
    static const int kAlignmentCount[] = {
        0, 2, 2, 2, 2, 2, 3, 3, 3, 3
    };

    int version = qr.version;
    if (version > 10) return;  // table only covers versions 1-10

    const int* coords = kAlignmentTable[version - 1];
    int count = kAlignmentCount[version - 1];
    int size = qr.size;

    // Draw a 5x5 alignment pattern at each (row, col) center,
    // skipping any that overlap the finder patterns.
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < count; ++j) {
            int row = coords[i];
            int col = coords[j];

            // Skip if this pattern would overlap a finder pattern.
            // Finders occupy the top-left, top-right, bottom-left 8x8 areas.
            bool overlapsFinder = (row <= 8 && col <= 8) ||   // top-left
                                  (row <= 8 && col >= size - 9) ||  // top-right
                                  (row >= size - 9 && col <= 8);    // bottom-left
            if (overlapsFinder) continue;

            // 5x5 pattern: dark border, light ring, dark center.
            for (int r = -2; r <= 2; ++r) {
                for (int c = -2; c <= 2; ++c) {
                    bool dark = (r == -2 || r == 2 || c == -2 || c == 2) ||
                                (r == 0 && c == 0);
                    setModule(qr, row + r, col + c, dark);
                    reserveModule(qr, row + r, col + c);  // alignment is a function pattern
                }
            }
        }
    }
}

// Sets the always-dark module. In row/column coordinates this is
// (8, size - 8), which is (8, 4 * version + 9).
void drawDarkModule(QrCode& qr) {
    const int row = 8;
    const int col = qr.size - 8;
    setModule(qr, row, col, true);
    reserveModule(qr, row, col);  // dark module is a function pattern
}

void reserveFormatInfoAreas(QrCode& qr) {
    const int size = qr.size;

    for (int i = 0; i < 6; ++i) {
        reserveModule(qr, 8, i);
        reserveModule(qr, i, 8);
    }
    reserveModule(qr, 8, 7);
    reserveModule(qr, 8, 8);
    reserveModule(qr, 7, 8);

    for (int i = 0; i < 8; ++i) {
        reserveModule(qr, size - 1 - i, 8);
    }
    for (int i = 8; i < 15; ++i) {
        reserveModule(qr, 8, size - 15 + i);
    }
}

void reserveVersionInfoAreas(QrCode& qr) {
    if (qr.version < 7) return;

    const int size = qr.size;
    for (int i = 0; i < 18; ++i) {
        reserveModule(qr, i / 3, size - 11 + (i % 3));
        reserveModule(qr, size - 11 + (i % 3), i / 3);
    }
}

// Stage 6: Place data bits (zigzag)
// Places the data + EC codewords into the grid in a zigzag pattern.
//
// HOW THE ZIGZAG WORKS:
//   - Start at the bottom-right corner.
//   - Move in 2-column-wide vertical strips, going up then down alternately.
//   - Within each strip, read the right column first, then the left.
//   - Skip any module that is part of a function pattern (already set).
//
// The bits are placed MSB-first: bit 7 of each byte goes in first.
void placeData(QrCode& qr, const std::vector<uint8_t>& data) {
    int size = qr.size;
    int bitIndex = 0;  // which bit of `data` we're placing

    // Start at the bottom-right, moving upward in the rightmost column pair.
    int col = size - 1;
    bool upward = true;

    while (col >= 0) {
        // Skip the vertical timing pattern column (col 6).
        if (col == 6) {
            col--;
            continue;
        }

        // Process the 2-column strip: col and col-1.
        for (int i = 0; i < size; ++i) {
            int row = upward ? (size - 1 - i) : i;

            // Right column first, then left column.
            for (int c = 0; c < 2; ++c) {
                int curCol = col - c;
                if (curCol < 0) continue;

                // Skip if this module is part of a function pattern.
                // We use isReserved() (not isDark()) because function patterns
                // can be light too (e.g. finder inner ring). isDark() would
                // wrongly write data into light function modules.
                if (isReserved(qr, row, curCol)) continue;

                // Place the next bit.
                if (bitIndex < static_cast<int>(data.size()) * 8) {
                    int byteIdx = bitIndex / 8;
                    int bit = (data[byteIdx] >> (7 - (bitIndex % 8))) & 1;
                    setModule(qr, row, curCol, bit != 0);
                    bitIndex++;
                }
            }
        }

        // Move to the next column pair, reversing direction.
        col -= 2;
        upward = !upward;
    }

    // Sanity check: every bit must have been placed.
    // If this throws, the reserved-module map is wrong (some data bits
    // couldn't find a home, or the grid is too small).
    if (bitIndex != static_cast<int>(data.size()) * 8) {
        throw std::runtime_error("Not all data bits were placed");
    }
}

// Stage 7: Data masking
// Returns true if mask pattern `maskId` should flip the module at (row, col).
// The 8 mask formulas from ISO/IEC 18004. If true, the data bit is inverted.
bool maskCondition(int maskId, int row, int col) {
    switch (maskId) {
        case 0: return (row + col) % 2 == 0;
        case 1: return row % 2 == 0;
        case 2: return col % 3 == 0;
        case 3: return (row + col) % 3 == 0;
        case 4: return (row / 2 + col / 3) % 2 == 0;
        case 5: return (row * col) % 2 + (row * col) % 3 == 0;
        case 6: return ((row * col) % 2 + (row * col) % 3) % 2 == 0;
        case 7: return ((row + col) % 2 + (row * col) % 3) % 2 == 0;
        default: return false;
    }
}

// Applies mask pattern `maskId` (0-7) to the data region of the grid.
// Function patterns (reserved modules) are NOT masked.
void applyMask(QrCode& qr, int maskId) {
    for (int row = 0; row < qr.size; ++row) {
        for (int col = 0; col < qr.size; ++col) {
            // Skip function patterns.
            if (isReserved(qr, row, col)) continue;
            // Flip the data bit if the mask condition is true.
            if (maskCondition(maskId, row, col)) {
                bool dark = isDark(qr, row, col);
                setModule(qr, row, col, !dark);
            }
        }
    }
}

// Penalty rule 1: runs of 5+ same-color modules in a row/column.
// Each run of length n >= 5 adds (3 + n - 5) penalty.
int penaltyRule1(const QrCode& qr) {
    int penalty = 0;
    // Check rows.
    for (int row = 0; row < qr.size; ++row) {
        int run = 1;
        for (int col = 1; col < qr.size; ++col) {
            if (isDark(qr, row, col) == isDark(qr, row, col - 1)) {
                run++;
            } else {
                if (run >= 5) penalty += 3 + (run - 5);
                run = 1;
            }
        }
        if (run >= 5) penalty += 3 + (run - 5);
    }
    // Check columns.
    for (int col = 0; col < qr.size; ++col) {
        int run = 1;
        for (int row = 1; row < qr.size; ++row) {
            if (isDark(qr, row, col) == isDark(qr, row - 1, col)) {
                run++;
            } else {
                if (run >= 5) penalty += 3 + (run - 5);
                run = 1;
            }
        }
        if (run >= 5) penalty += 3 + (run - 5);
    }
    return penalty;
}

// Penalty rule 2: 2x2 blocks of same-color modules. Each adds +3.
int penaltyRule2(const QrCode& qr) {
    int penalty = 0;
    for (int row = 0; row < qr.size - 1; ++row) {
        for (int col = 0; col < qr.size - 1; ++col) {
            bool c = isDark(qr, row, col);
            if (isDark(qr, row, col + 1) == c &&
                isDark(qr, row + 1, col) == c &&
                isDark(qr, row + 1, col + 1) == c) {
                penalty += 3;
            }
        }
    }
    return penalty;
}

// Penalty rule 3: finder-like patterns (1:1:3:1:1) in rows/columns.
// Each adds +40.
//
// The standard looks for the pattern 1011101 (dark-light-dark-dark-dark-
// light-dark) with FOUR light modules on ONE side (before OR after):
//   00001011101  OR  10111010000
// It does NOT require 4 lights on both sides simultaneously.
int penaltyRule3(const QrCode& qr) {
    int penalty = 0;
    int size = qr.size;

    // Check rows.
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size - 10; ++col) {
            // Core pattern 1011101 at positions col..col+6.
            bool core = isDark(qr, row, col) && !isDark(qr, row, col + 1) &&
                        isDark(qr, row, col + 2) && isDark(qr, row, col + 3) &&
                        isDark(qr, row, col + 4) && !isDark(qr, row, col + 5) &&
                        isDark(qr, row, col + 6);
            if (!core) continue;

            // 4 light modules BEFORE (col-4..col-1) OR AFTER (col+7..col+10).
            bool fourBefore = (col >= 4) &&
                !isDark(qr, row, col - 4) && !isDark(qr, row, col - 3) &&
                !isDark(qr, row, col - 2) && !isDark(qr, row, col - 1);
            bool fourAfter = (col + 10 < size) &&
                !isDark(qr, row, col + 7) && !isDark(qr, row, col + 8) &&
                !isDark(qr, row, col + 9) && !isDark(qr, row, col + 10);

            if (fourBefore || fourAfter) penalty += 40;
        }
    }

    // Check columns.
    for (int col = 0; col < size; ++col) {
        for (int row = 0; row < size - 10; ++row) {
            bool core = isDark(qr, row, col) && !isDark(qr, row + 1, col) &&
                        isDark(qr, row + 2, col) && isDark(qr, row + 3, col) &&
                        isDark(qr, row + 4, col) && !isDark(qr, row + 5, col) &&
                        isDark(qr, row + 6, col);
            if (!core) continue;

            bool fourBefore = (row >= 4) &&
                !isDark(qr, row - 4, col) && !isDark(qr, row - 3, col) &&
                !isDark(qr, row - 2, col) && !isDark(qr, row - 1, col);
            bool fourAfter = (row + 10 < size) &&
                !isDark(qr, row + 7, col) && !isDark(qr, row + 8, col) &&
                !isDark(qr, row + 9, col) && !isDark(qr, row + 10, col);

            if (fourBefore || fourAfter) penalty += 40;
        }
    }
    return penalty;
}

// Penalty rule 4: proportion of dark modules.
// Measures distance from 50% dark, in 5% steps, +10 per step.
//   50% → 0, 55% → 10, 60% → 20, 45% → 10, 40% → 20
int penaltyRule4(const QrCode& qr) {
    int total = qr.size * qr.size;
    int dark = 0;
    for (uint8_t m : qr.modules) if (m) dark++;

    // Compute the deviation from 50% WITHOUT truncating `percent` first.
    //   percent = dark * 100 / total
    //   steps = |percent - 50| / 5
    // To avoid integer truncation in `percent`, work with the numerator:
    //   |percent - 50| / 5
    //   = |(dark*100/total) - 50| / 5
    //   = |dark*100 - 50*total| / (5*total)
    //   = |20*dark - 10*total| / total        (divide num & denom by 5)
    // penalty = steps * 10
    int numerator = std::abs(20 * dark - 10 * total);
    int steps = numerator / total;
    return steps * 10;
}

// Computes the total penalty score for the current grid (lower is better).
int penaltyScore(const QrCode& qr) {
    return penaltyRule1(qr) + penaltyRule2(qr) + penaltyRule3(qr) + penaltyRule4(qr);
}

// Tries all 8 masks, scores each, and applies the best one.
// Returns the chosen mask ID (0-7).
int chooseAndApplyMask(QrCode& qr) {
    int bestMask = 0;
    int bestScore = INT_MAX;

    for (int maskId = 0; maskId < 8; ++maskId) {
        // Copy the grid, apply the mask, score it.
        QrCode copy = qr;
        applyMask(copy, maskId);
        int score = penaltyScore(copy);
        if (score < bestScore) {
            bestScore = score;
            bestMask = maskId;
        }
    }

    // Apply the best mask to the real grid.
    applyMask(qr, bestMask);
    return bestMask;
}

// Stage 8: Format information

// Computes the 15-bit format information string.
//
// The format info is: [2 bits EC level][3 bits mask ID][10 bits BCH error]
// The 10 BCH bits are computed with generator polynomial 0x537, then the
// whole 15 bits are XORed with 0x5412 to avoid all-zero patterns.
int formatInfo(ErrorCorrectionLevel ec, int maskId) {
    // EC level → 2-bit format code (ISO/IEC 18004).
    int ecBits = 0;
    switch (ec) {
        case ErrorCorrectionLevel::L: ecBits = 0b01; break;
        case ErrorCorrectionLevel::M: ecBits = 0b00; break;
        case ErrorCorrectionLevel::Q: ecBits = 0b11; break;
        case ErrorCorrectionLevel::H: ecBits = 0b10; break;
    }

    // 5-bit data: [2 EC bits][3 mask bits].
    int data = (ecBits << 3) | (maskId & 0x7);

    // Compute 10 BCH error correction bits.
    // Generator polynomial for format info: 0x537 (x^10 + x^8 + x^5 + x^4 + x^2 + x + 1).
    int rem = data << 10;
    for (int i = 14; i >= 10; --i) {
        if ((rem >> i) & 1) {
            rem ^= 0x537 << (i - 10);
        }
    }
    int bch = rem & 0x3FF;  // 10 bits

    // Combine: [5 data bits][10 BCH bits] = 15 bits, then XOR with 0x5412.
    int format = ((data << 10) | bch) ^ 0x5412;
    return format & 0x7FFF;  // 15 bits
}

// Writes the format information into the grid at both locations.
// The 15 bits are placed in two copies for redundancy.
void writeFormatInfo(QrCode& qr, ErrorCorrectionLevel ec, int maskId) {
    int format = formatInfo(ec, maskId);
    int size = qr.size;

    // Copy 1: around the top-left finder.
    // Bits 0-5 go in row 8 (cols 0-5), bit 6 at (8,7), bit 7 at
    // (8,8), bit 8 at (7,8), and bits 9-14 in column 8 (rows 5-0).
    // (Standard placement.)
    for (int i = 0; i < 6; ++i) {
        setModule(qr, 8, i, (format >> i) & 1);          // row 8, cols 0-5
        reserveModule(qr, 8, i);
    }
    setModule(qr, 8, 7, (format >> 6) & 1);              // bit 6
    reserveModule(qr, 8, 7);
    setModule(qr, 8, 8, (format >> 7) & 1);              // bit 7
    reserveModule(qr, 8, 8);
    setModule(qr, 7, 8, (format >> 8) & 1);              // bit 8
    reserveModule(qr, 7, 8);
    for (int i = 0; i < 6; ++i) {
        setModule(qr, i, 8, (format >> (14 - i)) & 1);   // col 8, rows 0-5
        reserveModule(qr, i, 8);
    }

    // Copy 2: split near top-right and bottom-left finders.
    // Bits 0-7 go in column 8 (rows size-1 down to size-8).
    // Bits 8-14 go in row 8 (cols size-7 to size-1).
    for (int i = 0; i < 8; ++i) {
        setModule(qr, size - 1 - i, 8, (format >> i) & 1);
        reserveModule(qr, size - 1 - i, 8);
    }
    for (int i = 8; i < 15; ++i) {
        setModule(qr, 8, size - 15 + i, (format >> i) & 1);
        reserveModule(qr, 8, size - 15 + i);
    }

    // The fixed dark module sits beside the second format-information copy.
    drawDarkModule(qr);
}


// Stage 9: Version information (only for version >= 7)

// Computes the 18-bit version information string.
//
// The version info is: [6 version bits][12 BCH error bits].
// The 12 BCH bits are computed with generator polynomial 0x1F25.
// Only versions 7-40 have version info; smaller versions don't need it.
int versionInfo(int version) {
    // 6-bit version data.
    int data = version & 0x3F;

    // Compute 12 BCH error correction bits.
    // Generator polynomial for version info: 0x1F25.
    int rem = data << 12;
    for (int i = 17; i >= 12; --i) {
        if ((rem >> i) & 1) {
            rem ^= 0x1F25 << (i - 12);
        }
    }
    int bch = rem & 0xFFF;  // 12 bits

    // Combine: [6 version bits][12 BCH bits] = 18 bits.
    return (data << 12) | bch;
}

// Writes the version information into the grid at both locations.
// Only for version >= 7. No-op otherwise.
void writeVersionInfo(QrCode& qr, int version) {
    if (version < 7) return;

    int info = versionInfo(version);
    int size = qr.size;

    // The 18 bits are placed into two identical 3x6 blocks.
    //
    // Top-right:
    //   rows 0..5
    //   cols size-11 .. size-9
    //
    // Bottom-left:
    //   rows size-11 .. size-9
    //   cols 0..5
    //
    // Bits are placed LSB-first. For the top-right block:
    //   row = i / 3, col = size-11 + (i % 3)
    // The bottom-left block is the transpose:
    //   row = size-11 + (i % 3), col = i / 3
    for (int i = 0; i < 18; ++i) {
        int bit = (info >> i) & 1;

        // Top-right block.
        int trRow = i / 3;
        int trCol = size - 11 + (i % 3);
        setModule(qr, trRow, trCol, bit);
        reserveModule(qr, trRow, trCol);

        // Bottom-left block (transpose of top-right).
        int blRow = size - 11 + (i % 3);
        int blCol = i / 3;
        setModule(qr, blRow, blCol, bit);
        reserveModule(qr, blRow, blCol);
    }
}

// ---------------------------------------------------------------------------
// Stage 10: Render (orchestrator)
// ---------------------------------------------------------------------------

// Determines the best encoding mode for `data`.
//   - All digits → NUMERIC
//   - All alphanumeric chars → ALPHANUMERIC
//   - Otherwise → BYTE
Mode detectMode(const std::string& data) {
    bool allDigits = true;
    bool allAlnum = true;
    const std::string alnumChars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

    for (char c : data) {
        if (c < '0' || c > '9') allDigits = false;
        if (alnumChars.find(c) == std::string::npos) allAlnum = false;
    }

    if (allDigits) return Mode::NUMERIC;
    if (allAlnum) return Mode::ALPHANUMERIC;
    return Mode::BYTE;
}


// Function for piplining everything.
QrCode makeQRCode(const std::string& data, ErrorCorrectionLevel ec) {
    // Stage 1-2: choose version + EC level.
    int version = chooseVersion(data, ec);
    Mode mode = detectMode(data);

    // Stage 3: encode data bits, then finalize (terminator + padding).
    auto dataBits = encodeData(data, mode, version);
    int bitCount = 4 + countBitsForMode(mode, version) + dataBitsForLength(data, mode);
    auto codewords = finalizeData(dataBits, bitCount, version, ec);

    // Stage 4: Reed-Solomon error correction.
    // Splits into blocks, computes EC per block, and interleaves.
    auto full = interleaveAndEcc(codewords, version, ec);

    // Stage 5: build the module matrix (function patterns).
    QrCode qr = createMatrix(version);
    drawFinder(qr, 0, 0);
    drawFinder(qr, 0, qr.size - 7);
    drawFinder(qr, qr.size - 7, 0);
    drawTiming(qr);
    drawAlignment(qr);
    drawDarkModule(qr);
    reserveFormatInfoAreas(qr);
    reserveVersionInfoAreas(qr);

    // Stage 6: place data bits (zigzag).
    placeData(qr, full);

    // Stage 7: apply the best mask.
    int mask = chooseAndApplyMask(qr);

    // Stage 8: write format information.
    writeFormatInfo(qr, ec, mask);

    // Stage 9: write version information (only for version >= 7).
    writeVersionInfo(qr, version);

    return qr;
}

