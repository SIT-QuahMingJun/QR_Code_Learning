cmake_minimum_required(VERSION 3.16)

project(QR_Code_Learning LANGUAGES CXX)

# C++17 required by GoogleTest
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Import dependencies (GoogleTest, GLM, etc.)
# ---------------------------------------------------------------------------
include(cmakes/ImportDependencies.cmake)
importDependencies()

# ---------------------------------------------------------------------------
# QR encoder library (pure C++ logic — shared by tests and WASM build)
# ---------------------------------------------------------------------------
add_library(qr_encoder STATIC
    src/qr_encoder.cpp
)
target_include_directories(qr_encoder PUBLIC src)

# ---------------------------------------------------------------------------
# Tests (native build only — runs on CI runner, no GL needed)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    enable_testing()
    add_executable(qr_tests
        tests/qr_encoder_test.cpp
    )
    target_link_libraries(qr_tests PRIVATE qr_encoder GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(qr_tests)
endif()

# ---------------------------------------------------------------------------
# WebGL/WASM build (Emscripten only)
#
# Built in a SEPARATE build dir (e.g. build-wasm) using the emsdk toolchain,
# so it never disturbs the native MSVC test build above.
#   cmake -S . -B build-wasm -DCMAKE_TOOLCHAIN_FILE=<emsdk>/upstream/emscripten/cmake/Platforms/Wasm32.cmake
#   cmake --build build-wasm
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    add_executable(qr_wasm
        src/wasm_glue.cpp
        src/qr_encoder.cpp
    )
    target_include_directories(qr_wasm PRIVATE src)

    # Emit index.js/index.wasm. The project-owned web/index.html loads the
    # modularized WASM output and renders the grid with a white quiet zone.
    set_target_properties(qr_wasm PROPERTIES OUTPUT_NAME "index" SUFFIX ".js")

    # Export the bridge functions so JS can call them, and build as a
    # modularized module (createQRModule) rather than a standalone program.
    # shell.html loads index.js via <script src> and calls createQRModule().
    # ccall/cwrap let JS pass JS strings to C functions (auto-converts to
    # C strings) — required because _qr_encode takes a const char*.
    target_link_options(qr_wasm PRIVATE
        "-sEXPORTED_FUNCTIONS=_qr_encode,_qr_get_size,_qr_get_module,_qr_free"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createQRModule"
    )

    add_custom_command(TARGET qr_wasm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
            "$<TARGET_FILE_DIR:qr_wasm>/index.html"
        COMMENT "Copying QR web renderer"
    )
endif()
