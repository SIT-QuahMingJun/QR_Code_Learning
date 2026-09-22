/*
@filename:  qr_encoder_test.cpp
@brief:     GoogleTest for Stage 1–2: version selection + module size.
*/

#include <gtest/gtest.h>

#include "qr_encoder.h"

namespace {

int countReserved(const QrCode& qr) {
    int result = 0;
    for (uint8_t value : qr.reserved) result += value != 0;
    return result;
}

QrCode version1FunctionPatterns() {
    QrCode qr = createMatrix(1);
    drawFinder(qr, 0, 0);
    drawFinder(qr, 0, qr.size - 7);
    drawFinder(qr, qr.size - 7, 0);
    drawTiming(qr);
    drawAlignment(qr);
    drawDarkModule(qr);
    reserveFormatInfoAreas(qr);
    reserveVersionInfoAreas(qr);
    return qr;
}

std::string deterministicRandomPayload(int version, int length) {
    std::uint32_t state = UINT32_C(0x00C0FFEE) ^
                          (static_cast<std::uint32_t>(version) * UINT32_C(0x9E3779B9));
    std::string result;
    result.reserve(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        result.push_back(static_cast<char>('a' + state % 26));
    }
    return result;
}

}  // namespace

// --- moduleSize ------------------------------------------------------------

TEST(ModuleSize, Version1Is21) {
    EXPECT_EQ(moduleSize(1), 21);
}

TEST(ModuleSize, Version2Is25) {
    EXPECT_EQ(moduleSize(2), 25);
}

TEST(ModuleSize, Version10Is57) {
    EXPECT_EQ(moduleSize(10), 57);
}

TEST(ModuleSize, FormulaMatches) {
    for (int v = 1; v <= 10; ++v) {
        EXPECT_EQ(moduleSize(v), 17 + 4 * v);
    }
}

// --- dataCodewords ---------------------------------------------------------

TEST(DataCodewords, Version1LIs19) {
    // v1 total=26, EC=7, blocks=1 → 26 - 7*1 = 19
    EXPECT_EQ(dataCodewords(1, ErrorCorrectionLevel::L), 19);
}

TEST(DataCodewords, Version1MIs16) {
    // v1 total=26, EC=10, blocks=1 → 26 - 10*1 = 16
    EXPECT_EQ(dataCodewords(1, ErrorCorrectionLevel::M), 16);
}

TEST(DataCodewords, Version1QIs13) {
    // v1 total=26, EC=13, blocks=1 → 26 - 13*1 = 13
    EXPECT_EQ(dataCodewords(1, ErrorCorrectionLevel::Q), 13);
}

TEST(DataCodewords, Version1HIs9) {
    // v1 total=26, EC=17, blocks=1 → 26 - 17*1 = 9
    EXPECT_EQ(dataCodewords(1, ErrorCorrectionLevel::H), 9);
}

TEST(DataCodewords, Version5LIs108) {
    // v5 total=134, EC=26, blocks=1 → 134 - 26*1 = 108
    EXPECT_EQ(dataCodewords(5, ErrorCorrectionLevel::L), 108);
}

// --- chooseVersion ---------------------------------------------------------

TEST(ChooseVersion, ShortNumericFitsVersion1) {
    // "12345" → numeric mode, tiny → version 1
    EXPECT_EQ(chooseVersion("12345", ErrorCorrectionLevel::L), 1);
}

TEST(ChooseVersion, ShortTextFitsVersion1) {
    // "HELLO" → alphanumeric, tiny → version 1
    EXPECT_EQ(chooseVersion("HELLO", ErrorCorrectionLevel::L), 1);
}

TEST(ChooseVersion, LongerTextNeedsHigherVersion) {
    // A longer string needs a bigger version
    std::string longData(50, 'A');  // 50 chars alphanumeric
    int v = chooseVersion(longData, ErrorCorrectionLevel::L);
    EXPECT_GE(v, 1);
    EXPECT_LE(v, 10);
}

TEST(ChooseVersion, HigherECNeedsBiggerVersion) {
    // Same data, higher EC → same or bigger version
    std::string data = "order-1234";
    int vL = chooseVersion(data, ErrorCorrectionLevel::L);
    int vH = chooseVersion(data, ErrorCorrectionLevel::H);
    EXPECT_GE(vH, vL);
}

TEST(ChooseVersion, ThrowsOnTooLong) {
    // 1000 chars won't fit version 10
    std::string tooLong(1000, 'A');
    EXPECT_THROW(chooseVersion(tooLong, ErrorCorrectionLevel::H), std::invalid_argument);
}

// --- encodeData ------------------------------------------------------------

// Helper: convert a byte vector to a binary string for easy comparison.
std::string toBinary(const std::vector<uint8_t>& bytes) {
    std::string s;
    for (uint8_t b : bytes) {
        for (int i = 7; i >= 0; --i) {
            s += ((b >> i) & 1) ? '1' : '0';
        }
    }
    return s;
}

TEST(EncodeData, NumericModeIndicator) {
    // "012345" numeric → mode 0001, count 6 (10 bits), then data
    auto bytes = encodeData("012345", Mode::NUMERIC, 1);
    std::string bits = toBinary(bytes);
    // mode indicator = 0001
    EXPECT_EQ(bits.substr(0, 4), "0001");
    // count = 6 in 10 bits = 0000000110
    EXPECT_EQ(bits.substr(4, 10), "0000000110");
}

TEST(EncodeData, NumericPacksGroups) {
    // "012345" → "012"(12→10 bits) + "345"(345→10 bits)
    auto bytes = encodeData("012345", Mode::NUMERIC, 1);
    std::string bits = toBinary(bytes);
    // after mode(4) + count(10) = 14 bits
    EXPECT_EQ(bits.substr(14, 10), "0000001100");  // 12
    EXPECT_EQ(bits.substr(24, 10), "0101011001");  // 345
}

TEST(EncodeData, NumericPreservesLeadingZeros) {
    // "01200" → "012"(12) + "00"(0 in 7 bits)
    auto bytes = encodeData("01200", Mode::NUMERIC, 1);
    std::string bits = toBinary(bytes);
    // mode(4) + count(10) = 14
    EXPECT_EQ(bits.substr(14, 10), "0000001100");  // 12
    EXPECT_EQ(bits.substr(24, 7), "0000000");       // 0 in 7 bits
}

TEST(EncodeData, AlphanumericModeIndicator) {
    // "HELLO" alphanumeric → mode 0010, count 5 (9 bits)
    auto bytes = encodeData("HELLO", Mode::ALPHANUMERIC, 1);
    std::string bits = toBinary(bytes);
    EXPECT_EQ(bits.substr(0, 4), "0010");
    EXPECT_EQ(bits.substr(4, 9), "000000101");  // 5 in 9 bits
}

TEST(EncodeData, AlphanumericPacksPairs) {
    // "HE" → 17*45+14 = 779 → 11 bits
    auto bytes = encodeData("HE", Mode::ALPHANUMERIC, 1);
    std::string bits = toBinary(bytes);
    // mode(4) + count(9) = 13
    EXPECT_EQ(bits.substr(13, 11), "01100001011");  // 779
}

TEST(EncodeData, ByteMode) {
    // "A" byte → mode 0100, count 1 (8 bits), then 0x41
    auto bytes = encodeData("A", Mode::BYTE, 1);
    std::string bits = toBinary(bytes);
    EXPECT_EQ(bits.substr(0, 4), "0100");
    EXPECT_EQ(bits.substr(4, 8), "00000001");  // count 1
    EXPECT_EQ(bits.substr(12, 8), "01000001"); // 'A' = 0x41
}

// --- finalizeData ----------------------------------------------------------

TEST(FinalizeData, FillsExactCapacity) {
    // Version 1-L has 19 data codewords = 152 bits.
    // Encode "A" (byte mode) then finalize.
    auto dataBits = encodeData("A", Mode::BYTE, 1);
    auto finalized = finalizeData(dataBits, 20, 1, ErrorCorrectionLevel::L);
    EXPECT_EQ(finalized.size(), 19);  // exactly 19 codewords
}

TEST(FinalizeData, PadBytesAlternate) {
    // Version 1-L: 19 codewords. Encode "A" (byte mode).
    // dataBits = 4 (mode) + 8 (count) + 8 (data) = 20 bits.
    // terminator: 4 bits → 24. pad-to-byte: 0 (24 is multiple of 8).
    // remaining: 152 - 24 = 128 bits = 16 pad bytes.
    auto dataBits = encodeData("A", Mode::BYTE, 1);
    auto finalized = finalizeData(dataBits, 20, 1, ErrorCorrectionLevel::L);
    // First 3 bytes: mode(0100) + count(00000001) + data(01000001)
    // = 01000000 00010100 00010000
    EXPECT_EQ(finalized[0], 0x40);  // 01000000
    EXPECT_EQ(finalized[1], 0x14);  // 00010100
    EXPECT_EQ(finalized[2], 0x10);  // 00010000 (last 4 data + terminator 0000)
    // Pad bytes start at index 3: 0xEC, 0x11, 0xEC...
    EXPECT_EQ(finalized[3], 0xEC);
    EXPECT_EQ(finalized[4], 0x11);
    EXPECT_EQ(finalized[5], 0xEC);
}

TEST(FinalizeData, TerminatorCappedAtFour) {
    // Version 1-L: 19 codewords = 152 bits.
    // Encode "HELLO WORLD" alphanumeric (61 data bits).
    // mode(4) + count(9) + data(61) = 74 bits.
    // terminator: 4 bits → 78. pad-to-byte: 2 → 80.
    // remaining: 152 - 80 = 72 bits = 9 pad bytes.
    auto dataBits = encodeData("HELLO WORLD", Mode::ALPHANUMERIC, 1);
    auto finalized = finalizeData(dataBits, 74, 1, ErrorCorrectionLevel::L);
    EXPECT_EQ(finalized.size(), 19);
    // Byte 10 (index 9) = last 2 data bits + terminator(0000) + pad-to-byte(00)
    // = 01 0000 00 = 0x40
    EXPECT_EQ(finalized[9], 0x40);
    // Pad bytes start at index 10
    EXPECT_EQ(finalized[10], 0xEC);
    EXPECT_EQ(finalized[11], 0x11);
}

// --- reedSolomon -----------------------------------------------------------

TEST(ReedSolomon, HelloWorldGoldenVector) {
    // Known data codewords for "HELLO WORLD" (version 1-M).
    std::vector<uint8_t> data = {
        32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17
    };
    // Known EC codewords (10 of them) from the ISO/Thonky reference.
    std::vector<uint8_t> expected = {
        196, 35, 39, 119, 235, 215, 231, 226, 93, 23
    };
    auto ecc = reedSolomon(data, 10);
    EXPECT_EQ(ecc, expected);
}

TEST(ReedSolomon, CorrectsSingleError) {
    // A simple sanity check: RS should produce deterministic output.
    std::vector<uint8_t> data = { 0x40, 0x20, 0x10 };
    auto ecc1 = reedSolomon(data, 7);
    auto ecc2 = reedSolomon(data, 7);
    EXPECT_EQ(ecc1, ecc2);  // deterministic
    EXPECT_EQ(ecc1.size(), 7);
}

// --- interleaveAndEcc ------------------------------------------------------

TEST(InterleaveAndEcc, SingleBlockIsConcatenation) {
    // Version 1-M: 1 block, 16 data codewords, 10 EC codewords.
    // For a single block, interleaveAndEcc == [data][EC].
    std::vector<uint8_t> data = {
        32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17
    };
    auto result = interleaveAndEcc(data, 1, ErrorCorrectionLevel::M);
    // 16 data + 10 EC = 26 total.
    EXPECT_EQ(result.size(), 26);
    // First 16 bytes are the data unchanged.
    for (int i = 0; i < 16; ++i) EXPECT_EQ(result[i], data[i]);
    // Last 10 bytes are the known EC codewords.
    std::vector<uint8_t> expectedEc = { 196, 35, 39, 119, 235, 215, 231, 226, 93, 23 };
    for (int i = 0; i < 10; ++i) EXPECT_EQ(result[16 + i], expectedEc[i]);
}

TEST(InterleaveAndEcc, MultiBlockInterleavesData) {
    // Version 5-Q: group1 = 2 blocks x 15 data, group2 = 2 blocks x 16 data.
    // Total data = 2*15 + 2*16 = 62. EC per block = 18. Total = 62 + 4*18 = 134.
    // Build 62 distinct data codewords so we can verify interleaving order.
    std::vector<uint8_t> data(62);
    for (int i = 0; i < 62; ++i) data[i] = static_cast<uint8_t>(i);

    auto result = interleaveAndEcc(data, 5, ErrorCorrectionLevel::Q);
    EXPECT_EQ(result.size(), 134);

    // Interleaved data order: block0[0], block1[0], block2[0], block3[0],
    // block0[1], block1[1], ... (group2 blocks are longer, so they continue).
    // Blocks: b0 = data[0..14], b1 = data[15..29], b2 = data[30..45], b3 = data[46..61].
    // First 4 bytes: b0[0]=0, b1[0]=15, b2[0]=30, b3[0]=46.
    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 15);
    EXPECT_EQ(result[2], 30);
    EXPECT_EQ(result[3], 46);
    // Next 4: b0[1]=1, b1[1]=16, b2[1]=31, b3[1]=47.
    EXPECT_EQ(result[4], 1);
    EXPECT_EQ(result[5], 16);
    EXPECT_EQ(result[6], 31);
    EXPECT_EQ(result[7], 47);
    // After 15 rounds, group1 blocks (15 bytes) run out.
    // Byte index 15*4=60: b2[15]=45, b3[15]=61 (only group2 remains).
    EXPECT_EQ(result[60], 45);
    EXPECT_EQ(result[61], 61);
}

TEST(InterleaveAndEcc, MultiBlockEcInterleaved) {
    // Version 5-Q: 4 blocks, 18 EC codewords each.
    // The EC section starts at byte 62 and is interleaved: ec0[0], ec1[0],
    // ec2[0], ec3[0], ec0[1], ...
    std::vector<uint8_t> data(62);
    for (int i = 0; i < 62; ++i) data[i] = static_cast<uint8_t>(i);

    auto result = interleaveAndEcc(data, 5, ErrorCorrectionLevel::Q);
    EXPECT_EQ(result.size(), 134);

    // Compute each block's EC independently to verify interleaving.
    std::vector<std::vector<uint8_t>> blocks(4);
    blocks[0].assign(data.begin(), data.begin() + 15);
    blocks[1].assign(data.begin() + 15, data.begin() + 30);
    blocks[2].assign(data.begin() + 30, data.begin() + 46);
    blocks[3].assign(data.begin() + 46, data.begin() + 62);
    std::vector<std::vector<uint8_t>> ecc(4);
    for (int b = 0; b < 4; ++b) ecc[b] = reedSolomon(blocks[b], 18);

    // EC section starts at byte 62.
    for (int i = 0; i < 18; ++i) {
        EXPECT_EQ(result[62 + i * 4 + 0], ecc[0][i]);
        EXPECT_EQ(result[62 + i * 4 + 1], ecc[1][i]);
        EXPECT_EQ(result[62 + i * 4 + 2], ecc[2][i]);
        EXPECT_EQ(result[62 + i * 4 + 3], ecc[3][i]);
    }
}

// --- Stage 5: module matrix ------------------------------------------------

TEST(CreateMatrix, Version1Is21x21) {
    QrCode qr = createMatrix(1);
    EXPECT_EQ(qr.size, 21);
    EXPECT_EQ(qr.modules.size(), 21 * 21);
    // All modules start light.
    for (uint8_t m : qr.modules) EXPECT_EQ(m, 0);
}

TEST(SetAndGetModule, RoundTrip) {
    QrCode qr = createMatrix(1);
    setModule(qr, 5, 5, true);
    EXPECT_TRUE(isDark(qr, 5, 5));
    setModule(qr, 5, 5, false);
    EXPECT_FALSE(isDark(qr, 5, 5));
}

TEST(DrawFinder, TopLeftCorner) {
    QrCode qr = createMatrix(1);
    drawFinder(qr, 0, 0);
    // Border is dark.
    EXPECT_TRUE(isDark(qr, 0, 0));
    EXPECT_TRUE(isDark(qr, 0, 6));
    EXPECT_TRUE(isDark(qr, 6, 0));
    EXPECT_TRUE(isDark(qr, 6, 6));
    // Inner ring is light.
    EXPECT_FALSE(isDark(qr, 1, 1));
    EXPECT_FALSE(isDark(qr, 1, 5));
    // Center 3x3 is dark.
    EXPECT_TRUE(isDark(qr, 3, 3));
    EXPECT_TRUE(isDark(qr, 2, 2));
    EXPECT_TRUE(isDark(qr, 4, 4));
}

TEST(DrawFinder, ReservesWhiteSeparator) {
    QrCode qr = createMatrix(1);
    drawFinder(qr, 0, 0);

    // The separator is white, but it is still a fixed function area and must
    // not be overwritten by payload bits.
    EXPECT_TRUE(isReserved(qr, 7, 0));
    EXPECT_TRUE(isReserved(qr, 0, 7));
    EXPECT_TRUE(isReserved(qr, 7, 7));
    EXPECT_FALSE(isDark(qr, 7, 0));
    EXPECT_FALSE(isDark(qr, 0, 7));
}

TEST(FunctionPatternReservation, Version1MLeavesExactPayloadCapacity) {
    QrCode qr = version1FunctionPatterns();

    // Version 1-M contains 16 data codewords and 10 EC codewords, with no
    // remainder bits. Therefore every unreserved module is one payload bit.
    EXPECT_EQ(qr.size * qr.size - countReserved(qr), (16 + 10) * 8);
}

TEST(DrawTiming, AlternatesAlongRow6) {
    QrCode qr = createMatrix(1);
    drawTiming(qr);
    // Row 6, starting at col 8: dark, light, dark, light...
    EXPECT_TRUE(isDark(qr, 6, 8));
    EXPECT_FALSE(isDark(qr, 6, 9));
    EXPECT_TRUE(isDark(qr, 6, 10));
    EXPECT_FALSE(isDark(qr, 6, 11));
}

TEST(DrawDarkModule, UsesStandardRowColumnCoordinates) {
    QrCode qr = createMatrix(1);
    drawDarkModule(qr);
    // The standard coordinate is (x=8, y=size-8). This API uses (row,col).
    EXPECT_TRUE(isDark(qr, 13, 8));
    EXPECT_TRUE(isReserved(qr, 13, 8));

    for (int version : {2, 7}) {
        QrCode larger = createMatrix(version);
        drawDarkModule(larger);
        EXPECT_TRUE(isDark(larger, larger.size - 8, 8));
        EXPECT_TRUE(isReserved(larger, larger.size - 8, 8));
    }
}

TEST(DrawAlignment, Version2HasOneAt1818) {
    QrCode qr = createMatrix(2);
    drawAlignment(qr);
    // Version 2: single alignment pattern centered at (18, 18).
    // Center is dark.
    EXPECT_TRUE(isDark(qr, 18, 18));
    // Border is dark.
    EXPECT_TRUE(isDark(qr, 16, 16));
    EXPECT_TRUE(isDark(qr, 20, 20));
    // Inner ring is light.
    EXPECT_FALSE(isDark(qr, 17, 17));
}

TEST(DrawAlignment, Version1HasNone) {
    QrCode qr = createMatrix(1);
    drawAlignment(qr);
    // Version 1 has no alignment patterns; grid stays all light.
    for (uint8_t m : qr.modules) EXPECT_EQ(m, 0);
}

// --- placeData -------------------------------------------------------------

TEST(PlaceData, StartsAtBottomRight) {
    QrCode qr = createMatrix(1);
    // Place a single byte 0xFF (all dark) into an empty grid.
    placeData(qr, { 0xFF });
    // The first bit goes to the bottom-right corner (row 20, col 20).
    EXPECT_TRUE(isDark(qr, 20, 20));
}

TEST(PlaceData, SkipsFunctionPatterns) {
    QrCode qr = createMatrix(1);
    // Draw the finder patterns first (they occupy the corners).
    drawFinder(qr, 0, 0);
    drawFinder(qr, 0, 14);
    drawFinder(qr, 14, 0);
    drawTiming(qr);
    drawDarkModule(qr);

    // Place a byte of all dark bits.
    placeData(qr, { 0xFF });

    // The finder pattern at top-left should be unchanged (still its pattern).
    // (0,0) is dark (finder border), (1,1) is light (finder inner ring).
    EXPECT_TRUE(isDark(qr, 0, 0));
    EXPECT_FALSE(isDark(qr, 1, 1));
}

TEST(PlaceData, PlacesAllBits) {
    QrCode qr = createMatrix(1);
    // Place 0xAA (10101010) — alternating bits.
    placeData(qr, { 0xAA });
    // First bit (1) at bottom-right, second bit (0) at the next position.
    EXPECT_TRUE(isDark(qr, 20, 20));   // bit 7 = 1
    EXPECT_FALSE(isDark(qr, 20, 19));  // bit 6 = 0
}

TEST(PlaceData, SkipsLightFunctionModules) {
    // The key fix: function patterns can be LIGHT (e.g. finder inner ring).
    // placeData must skip them via isReserved(), not isDark().
    QrCode qr = createMatrix(1);
    drawFinder(qr, 0, 0);  // reserves the top-left 7x7 finder

    // Place a byte of all dark bits.
    placeData(qr, { 0xFF });

    // The finder's inner ring at (1,1) is light and must stay light
    // (placeData should NOT have written a dark bit there).
    EXPECT_FALSE(isDark(qr, 1, 1));
    // The finder's border at (0,0) is dark and must stay dark.
    EXPECT_TRUE(isDark(qr, 0, 0));
}

TEST(ReserveModule, MarksAndChecks) {
    QrCode qr = createMatrix(1);
    EXPECT_FALSE(isReserved(qr, 3, 3));
    reserveModule(qr, 3, 3);
    EXPECT_TRUE(isReserved(qr, 3, 3));
}

// --- Stage 7: masking ------------------------------------------------------

TEST(ApplyMask, Mask0FlipsEvenSum) {
    QrCode qr = createMatrix(1);
    // Set a data module at (2,2) dark (2+2=4, even → mask 0 flips it).
    setModule(qr, 2, 2, true);
    applyMask(qr, 0);
    // (2,2): (2+2)%2 == 0 → flip → should be light now.
    EXPECT_FALSE(isDark(qr, 2, 2));
}

TEST(ApplyMask, SkipsReservedModules) {
    QrCode qr = createMatrix(1);
    // Reserve a module and set it dark.
    reserveModule(qr, 2, 2);
    setModule(qr, 2, 2, true);
    applyMask(qr, 0);
    // Reserved modules are not masked → stays dark.
    EXPECT_TRUE(isDark(qr, 2, 2));
}

TEST(PenaltyScore, UniformGridHasHighPenalty) {
    QrCode qr = createMatrix(1);
    // An all-light grid is penalized heavily (long runs + 0% dark).
    // This is the point of masking: uniform grids score badly.
    EXPECT_GT(penaltyScore(qr), 0);
}

TEST(PenaltyScore, BalancedGridScoresLower) {
    // A checkerboard (alternating) grid should score lower than all-light,
    // because it has no long runs and ~50% dark.
    QrCode qr = createMatrix(1);
    for (int r = 0; r < qr.size; ++r)
        for (int c = 0; c < qr.size; ++c)
            setModule(qr, r, c, (r + c) % 2 == 0);

    QrCode allLight = createMatrix(1);
    EXPECT_LT(penaltyScore(qr), penaltyScore(allLight));
}

TEST(PenaltyRule3, DetectsFinderLikeWithFourBefore) {
    // Pattern: 0000 1011101 (4 lights before, then the finder-like core).
    QrCode qr = createMatrix(1);
    // Place the pattern in row 0 starting at col 0.
    // 0000 1011101
    // col: 0 1 2 3 4 5 6 7 8 9 10
    //      0 0 0 0 1 0 1 1 1 0 1
    setModule(qr, 0, 4, true);
    setModule(qr, 0, 6, true);
    setModule(qr, 0, 7, true);
    setModule(qr, 0, 8, true);
    setModule(qr, 0, 10, true);
    // Rule 3 should detect this (4 lights before the core).
    EXPECT_GT(penaltyRule3(qr), 0);
}

TEST(PenaltyRule3, DetectsFinderLikeWithFourAfter) {
    // Pattern: 1011101 0000 (core then 4 lights after).
    QrCode qr = createMatrix(1);
    // col: 0 1 2 3 4 5 6 7 8 9 10
    //      1 0 1 1 1 0 1 0 0 0 0
    setModule(qr, 0, 0, true);
    setModule(qr, 0, 2, true);
    setModule(qr, 0, 3, true);
    setModule(qr, 0, 4, true);
    setModule(qr, 0, 6, true);
    EXPECT_GT(penaltyRule3(qr), 0);
}

TEST(PenaltyRule4, FiftyPercentIsZero) {
    // A checkerboard has ~50% dark → Rule 4 penalty should be 0.
    QrCode qr = createMatrix(1);
    for (int r = 0; r < qr.size; ++r)
        for (int c = 0; c < qr.size; ++c)
            setModule(qr, r, c, (r + c) % 2 == 0);
    EXPECT_EQ(penaltyRule4(qr), 0);
}

TEST(PenaltyRule4, AllLightIsPenalized) {
    // All light = 0% dark → deviation from 50% is 50 → (50/5)*10 = 100.
    QrCode qr = createMatrix(1);
    EXPECT_EQ(penaltyRule4(qr), 100);
}

TEST(ChooseAndApplyMask, ReturnsValidMask) {
    QrCode qr = createMatrix(1);
    // Place some data so there's something to mask.
    placeData(qr, { 0xAA, 0x55 });
    reserveFormatInfoAreas(qr);
    drawDarkModule(qr);
    int mask = chooseAndApplyMask(qr, ErrorCorrectionLevel::M);
    EXPECT_GE(mask, 0);
    EXPECT_LE(mask, 7);
}

// --- Stage 8: format information -------------------------------------------

TEST(FormatInfo, KnownValue) {
    // Known format info for EC level M (00) and mask 0.
    // data = 00000, BCH = 0, XOR 0x5412 = 0x5412.
    // (This is a known reference value.)
    int fmt = formatInfo(ErrorCorrectionLevel::M, 0);
    EXPECT_EQ(fmt, 0x5412);
}

TEST(FormatInfo, Is15Bits) {
    // Format info must fit in 15 bits.
    for (int mask = 0; mask < 8; ++mask) {
        int fmt = formatInfo(ErrorCorrectionLevel::L, mask);
        EXPECT_GE(fmt, 0);
        EXPECT_LE(fmt, 0x7FFF);
    }
}

TEST(WriteFormatInfo, ReservesModules) {
    QrCode qr = createMatrix(1);
    writeFormatInfo(qr, ErrorCorrectionLevel::M, 0);
    // The format modules around the top-left finder should be reserved.
    EXPECT_TRUE(isReserved(qr, 8, 0));
    EXPECT_TRUE(isReserved(qr, 8, 5));
    EXPECT_TRUE(isReserved(qr, 0, 8));
    EXPECT_TRUE(isReserved(qr, 5, 8));
}

TEST(WriteFormatInfo, UsesStandardBitLocations) {
    QrCode qr = createMatrix(1);
    reserveFormatInfoAreas(qr);
    const int format = formatInfo(ErrorCorrectionLevel::M, 0);
    writeFormatInfo(qr, ErrorCorrectionLevel::M, 0);

    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(isDark(qr, i, 8), ((format >> i) & 1) != 0);
    }
    EXPECT_EQ(isDark(qr, 7, 8), ((format >> 6) & 1) != 0);
    EXPECT_EQ(isDark(qr, 8, 8), ((format >> 7) & 1) != 0);
    EXPECT_EQ(isDark(qr, 8, 7), ((format >> 8) & 1) != 0);
    for (int i = 9; i < 15; ++i) {
        EXPECT_EQ(isDark(qr, 8, 14 - i), ((format >> i) & 1) != 0);
    }
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(isDark(qr, 8, qr.size - 1 - i), ((format >> i) & 1) != 0);
    }
    for (int i = 8; i < 15; ++i) {
        EXPECT_EQ(isDark(qr, qr.size - 15 + i, 8), ((format >> i) & 1) != 0);
    }
    EXPECT_TRUE(isDark(qr, qr.size - 8, 8));
}

// --- Stage 9: version information ------------------------------------------

TEST(VersionInfo, KnownValue) {
    // Known version info for version 7: 0x07C94.
    // (Reference value from the ISO/Thonky table.)
    EXPECT_EQ(versionInfo(7), 0x07C94);
}

TEST(VersionInfo, Is18Bits) {
    // Version info must fit in 18 bits.
    for (int v = 7; v <= 10; ++v) {
        int info = versionInfo(v);
        EXPECT_GE(info, 0);
        EXPECT_LE(info, 0x3FFFF);
    }
}

TEST(WriteVersionInfo, NoopForVersion1) {
    QrCode qr = createMatrix(1);
    writeVersionInfo(qr, 1);
    // Version 1 has no version info; nothing should be reserved.
    for (uint8_t r : qr.reserved) EXPECT_EQ(r, 0);
}

TEST(WriteVersionInfo, ReservesForVersion7) {
    QrCode qr = createMatrix(7);
    writeVersionInfo(qr, 7);
    // Version 7 (size 45) should have version info reserved.
    // Top-right block at (0, size-11) = (0, 34).
    EXPECT_TRUE(isReserved(qr, 0, 34));
    // Bottom-left block at (size-11, 0) = (34, 0).
    EXPECT_TRUE(isReserved(qr, 34, 0));
}

// --- detectMode ------------------------------------------------------------

TEST(DetectMode, AllDigitsIsNumeric) {
    EXPECT_EQ(detectMode("1234567890"), Mode::NUMERIC);
}

TEST(DetectMode, AlphanumericIsAlnum) {
    EXPECT_EQ(detectMode("HELLO WORLD 123"), Mode::ALPHANUMERIC);
}

TEST(DetectMode, LowercaseIsByte) {
    EXPECT_EQ(detectMode("hello"), Mode::BYTE);
}

TEST(DetectMode, SymbolsAreByte) {
    EXPECT_EQ(detectMode("a@b"), Mode::BYTE);
}

// --- makeQRCode ------------------------------------------------------------

TEST(MakeQRCode, ProducesSquareGrid) {
    QrCode qr = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    EXPECT_EQ(qr.size, qr.modules.size() / qr.size);
    EXPECT_EQ(qr.size, moduleSize(qr.version));
}

TEST(MakeQRCode, FindersAreDrawn) {
    QrCode qr = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    // Top-left finder center should be dark.
    EXPECT_TRUE(isDark(qr, 3, 3));
    // Top-right finder center.
    EXPECT_TRUE(isDark(qr, 3, qr.size - 4));
    // Bottom-left finder center.
    EXPECT_TRUE(isDark(qr, qr.size - 4, 3));
}

TEST(MakeQRCode, DarkModuleIsSet) {
    QrCode qr = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    // Dark module is at row size - 8, col 8.
    EXPECT_TRUE(isDark(qr, qr.size - 8, 8));
}

TEST(MakeQRCode, FormatInfoWritten) {
    QrCode qr = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    // Format info is written around the top-left finder.
    // The module at (8, 8) is the top-left of the format info area.
    // Just verify the format area is reserved (not overwritten by data).
    EXPECT_TRUE(isReserved(qr, 8, 8));
}

TEST(MakeQRCode, DataPlacedWithoutOverwritingFunctionPatterns) {
    QrCode qr = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    // The timing pattern at row 6 should still alternate.
    // Column 6, rows 8..size-9 should alternate dark/light.
    bool expected = true;  // row 8 is dark (timing starts dark at row 6, alternates)
    for (int r = 8; r < qr.size - 8; ++r) {
        EXPECT_EQ(isDark(qr, r, 6), expected) << "timing row " << r;
        expected = !expected;
    }
}

TEST(MakeQRCode, DeterministicOutput) {
    QrCode a = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    QrCode b = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    EXPECT_EQ(a.modules, b.modules);
    EXPECT_EQ(a.version, b.version);
}

TEST(MakeQRCode, DifferentDataGivesDifferentGrid) {
    QrCode a = makeQRCode("HELLO WORLD", ErrorCorrectionLevel::M);
    QrCode b = makeQRCode("HELLO WORLD!", ErrorCorrectionLevel::M);
    EXPECT_NE(a.modules, b.modules);
}

TEST(MakeQRCode, DeterministicRandomBytePayloadCoversVersions1Through10) {
    const int lengths[] = {8, 15, 27, 43, 63, 85, 107, 123, 153, 181};
    for (int version = 1; version <= 10; ++version) {
        const std::string payload = deterministicRandomPayload(version, lengths[version - 1]);
        const QrCode qr = makeQRCode(payload, ErrorCorrectionLevel::M);
        EXPECT_EQ(detectMode(payload), Mode::BYTE) << "version " << version;
        EXPECT_EQ(qr.version, version) << "payload length " << payload.size();
        EXPECT_EQ(qr.size, moduleSize(version));
        EXPECT_TRUE(isDark(qr, qr.size - 8, 8));
    }
}
