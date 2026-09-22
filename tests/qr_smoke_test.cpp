// Dependency-free native checks used by run.sh and run.bat.

#include "qr_encoder.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

int countReserved(const QrCode& qr) {
    int count = 0;
    for (uint8_t module : qr.reserved) count += module != 0;
    return count;
}

std::uint64_t matrixHash(const QrCode& qr) {
    // FNV-1a over every module in row-major order. Expected values were
    // generated independently with Project Nayuki's C++ QR encoder.
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (uint8_t module : qr.modules) {
        hash ^= module != 0 ? 1U : 0U;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::string deterministicRandomPayload(int version, int length) {
    // Xorshift32 with a fixed per-version seed: random-looking byte-mode text
    // that is identical on every platform and every test run.
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

QrCode makeVersion1FunctionMatrix() {
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

}  // namespace

int main() {
    bool ok = true;

    QrCode matrix = makeVersion1FunctionMatrix();
    ok &= expect(matrix.size == 21, "Version 1 must be 21 x 21 modules");
    ok &= expect(matrix.size * matrix.size - countReserved(matrix) == 208,
                 "Version 1-M must leave exactly 208 payload modules");
    ok &= expect(isReserved(matrix, 7, 0) && !isDark(matrix, 7, 0),
                 "Finder separators must be reserved and white");
    ok &= expect(isDark(matrix, 13, 8), "Version 1 dark module must be at row 13, col 8");

    for (const std::string& payload : std::vector<std::string>{
             "8675309", "HELLO WORLD", "order-1234", std::string(120, 'A'), std::string(200, 'a')}) {
        for (ErrorCorrectionLevel ec : {ErrorCorrectionLevel::L, ErrorCorrectionLevel::M,
                                        ErrorCorrectionLevel::Q, ErrorCorrectionLevel::H}) {
            try {
                QrCode qr = makeQRCode(payload, ec);
                ok &= expect(qr.size == moduleSize(qr.version),
                             "Generated QR size must match its version");
                ok &= expect(isDark(qr, qr.size - 8, 8),
                             "Completed QR must retain its dark module");
            } catch (const std::invalid_argument&) {
                // Some long payload / high-EC combinations intentionally exceed
                // this learning encoder's Version 1-10 capacity table.
            }
        }
    }

    struct GoldenCase {
        const char* name;
        std::string payload;
        ErrorCorrectionLevel ec;
        int version;
        std::uint64_t matrix;
    };
    const std::vector<GoldenCase> goldenCases = {
        {"numeric L", "8675309", ErrorCorrectionLevel::L, 1, UINT64_C(0x5d38afd94e9c72f3)},
        {"HELLO WORLD M", "HELLO WORLD", ErrorCorrectionLevel::M, 1, UINT64_C(0x1c376158ce5b2c55)},
        {"byte Q", "order-1234", ErrorCorrectionLevel::Q, 1, UINT64_C(0x7289606c9d51ad8d)},
        {"HELLO WORLD H", "HELLO WORLD", ErrorCorrectionLevel::H, 2, UINT64_C(0x3823aa736c35dd14)},
        {"version 5 multi-block Q", std::string(80, 'A'), ErrorCorrectionLevel::Q, 5,
         UINT64_C(0xe0e47169988c9781)},
        {"version 7 metadata M", std::string(160, 'A'), ErrorCorrectionLevel::M, 7,
         UINT64_C(0x6e4367d916841b29)},
    };
    for (const GoldenCase& golden : goldenCases) {
        const QrCode qr = makeQRCode(golden.payload, golden.ec);
        ok &= expect(qr.version == golden.version, golden.name);
        ok &= expect(matrixHash(qr) == golden.matrix, golden.name);
    }

    // Each length is one byte beyond Version N-1's M-level byte-mode
    // capacity, so these deterministic random strings force Versions 1..10.
    const int versionLengths[] = {8, 15, 27, 43, 63, 85, 107, 123, 153, 181};
    const std::uint64_t referenceHashes[] = {
        UINT64_C(0xa10f18e6ce0fae23), UINT64_C(0x8d352a01591bf08b),
        UINT64_C(0x5fc52da106634222), UINT64_C(0x8608a5ed953db0c8),
        UINT64_C(0x02ae0382482746e5), UINT64_C(0x50ba2065451ea032),
        UINT64_C(0x005dd56676d04cd1), UINT64_C(0x52ce6d1e36637599),
        UINT64_C(0x6535a7daf498f21f), UINT64_C(0x459d062a1fcc4c03),
    };
    for (int version = 1; version <= 10; ++version) {
        const std::string payload = deterministicRandomPayload(
            version, versionLengths[version - 1]);
        const QrCode qr = makeQRCode(payload, ErrorCorrectionLevel::M);
        const std::string versionMessage = "Random byte payload must select Version " +
                                           std::to_string(version);
        const std::string matrixMessage = "Random Version " + std::to_string(version) +
                                          " matrix must match reference";
        ok &= expect(qr.version == version, versionMessage.c_str());
        ok &= expect(matrixHash(qr) == referenceHashes[version - 1], matrixMessage.c_str());
    }

    if (ok) std::cout << "QR smoke tests passed.\n";
    return ok ? 0 : 1;
}
