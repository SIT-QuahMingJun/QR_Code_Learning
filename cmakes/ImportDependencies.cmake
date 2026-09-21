# ===========================================================================
# ImportDependencies.cmake
#
#   This project has TWO build targets:
#   Native test build → GoogleTest (unit tests for the QR encoder logic).
#   The WASM target uses the browser Canvas 2D API, so it has no external C++
#   dependencies and can build offline once Emscripten is installed.
# ===========================================================================

include(FetchContent)   # Enables CMake's built-in FetchContent module

function(importDependencies)

    if(EMSCRIPTEN)
        message(STATUS "[ImportDependencies] WASM build: no native test dependencies required.")
        return()
    endif()

    # --- GoogleTest: unit testing framework (native build only) ---
    message(STATUS "[ImportDependencies] Fetching GoogleTest ...")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    # Prevent GoogleTest from overriding the parent project's compiler settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Don't build GoogleTest's own samples/tests (keeps build fast)
    set(gtest_build_samples OFF CACHE BOOL "" FORCE)
    set(gtest_build_tests    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    message(STATUS "[ImportDependencies] Native test dependencies ready.")

endfunction()
