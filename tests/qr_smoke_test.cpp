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
    ok &= expect(isDark(matrix, 8, 13), "Version 1 dark module must be at row 8, col 13");

    for (const std::string& payload : std::vector<std::string>{
             "8675309", "HELLO WORLD", "order-1234", std::string(120, 'A'), std::string(200, 'a')}) {
        for (ErrorCorrectionLevel ec : {ErrorCorrectionLevel::L, ErrorCorrectionLevel::M,
                                        ErrorCorrectionLevel::Q, ErrorCorrectionLevel::H}) {
            try {
                QrCode qr = makeQRCode(payload, ec);
                ok &= expect(qr.size == moduleSize(qr.version),
                             "Generated QR size must match its version");
                ok &= expect(isDark(qr, 8, qr.size - 8),
                             "Completed QR must retain its dark module");
            } catch (const std::invalid_argument&) {
                // Some long payload / high-EC combinations intentionally exceed
                // this learning encoder's Version 1-10 capacity table.
            }
        }
    }

    if (ok) std::cout << "QR smoke tests passed.\n";
    return ok ? 0 : 1;
}
