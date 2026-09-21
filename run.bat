@echo off
REM Build and test the native QR encoder, then build and serve the WASM demo.
REM Usage: run.bat [all^|native^|wasm]

setlocal EnableExtensions
set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"
if /I not "%MODE%"=="all" if /I not "%MODE%"=="native" if /I not "%MODE%"=="wasm" (
  echo Usage: %~nx0 [all^|native^|wasm]
  exit /b 2
)

set "ROOT=%~dp0"
REM %~dp0 always ends in a backslash; remove it before quoting CMake paths.
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "NATIVE_BUILD=%ROOT%\build\native-mingw"
set "WASM_BUILD=%ROOT%\build\wasm"

if /I "%MODE%"=="all" call :native
if /I "%MODE%"=="native" call :native
if errorlevel 1 exit /b %errorlevel%
if /I "%MODE%"=="native" exit /b 0

where emcmake >nul 2>nul
if errorlevel 1 (
  echo Emscripten is not active. Run emsdk_env.bat, then rerun this script.
  exit /b 1
)
where emcc >nul 2>nul
if errorlevel 1 (
  echo Emscripten is not active. Run emsdk_env.bat, then rerun this script.
  exit /b 1
)

echo ==^> Configuring WASM demo
call emcmake cmake -S "%ROOT%" -B "%WASM_BUILD%" -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 exit /b %errorlevel%
echo ==^> Building WASM demo
cmake --build "%WASM_BUILD%" --config Debug
if errorlevel 1 exit /b %errorlevel%

echo ==^> Opening http://localhost:8080/index.html
start "QR Encoder Demo" "http://localhost:8080/index.html"
echo Press Ctrl+C to stop the local server.

py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 (
  py -3 -m http.server 8080 --directory "%WASM_BUILD%"
  exit /b %errorlevel%
)

python -c "import sys" >nul 2>nul
if not errorlevel 1 (
  python -m http.server 8080 --directory "%WASM_BUILD%"
  exit /b %errorlevel%
)

where emrun >nul 2>nul
if not errorlevel 1 (
  pushd "%WASM_BUILD%"
  emrun --no_browser --port 8080 index.html
  popd
  exit /b %errorlevel%
)

echo No local web server found. Install Python 3 or use emrun from Emscripten.
exit /b 1

:native
where g++ >nul 2>nul
if errorlevel 1 (
  echo MinGW g++ was not found on PATH. Install MinGW-w64 or update this script for your compiler.
  exit /b 1
)
where mingw32-make >nul 2>nul
if errorlevel 1 (
  echo mingw32-make was not found on PATH. Install MinGW-w64 or update this script for your compiler.
  exit /b 1
)
echo ==^> Configuring native tests
cmake -S "%ROOT%" -B "%NATIVE_BUILD%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 exit /b %errorlevel%
echo ==^> Building native tests
cmake --build "%NATIVE_BUILD%" --config Debug
if errorlevel 1 exit /b %errorlevel%
echo ==^> Running native tests
ctest --test-dir "%NATIVE_BUILD%" --build-config Debug --output-on-failure
exit /b %errorlevel%
