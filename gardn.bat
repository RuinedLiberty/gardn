@echo off
setlocal ENABLEDELAYEDEXPANSION

rem --- paths & env ---
set "REPO=%~dp0"
if exist "C:\Program Files\CMake\bin\cmake.exe" (
  set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
) else (
  set "CMAKE=cmake"
)
call C:\emsdk\emsdk_env.bat >nul

set "CMD=%~1"
if "%CMD%"=="" set "CMD=all"

if /I "%CMD%"=="client"  goto :client
if /I "%CMD%"=="server"  goto :server
if /I "%CMD%"=="run"     goto :run
if /I "%CMD%"=="kill"    goto :kill
if /I "%CMD%"=="clean"   goto :clean
if /I "%CMD%"=="all"     goto :all

echo Usage: gardn [client^|server^|run^|kill^|clean^|all]
exit /b 1

:client
pushd "%REPO%Client"
if not exist build\CMakeCache.txt (
  "%CMAKE%" -S . -B build -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=%EMSDK%\upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER_LAUNCHER="cmd.exe;/c" ^
    -DCMAKE_CXX_COMPILER_LAUNCHER="cmd.exe;/c"
)
"%CMAKE%" --build build --target gardn-client --parallel || (popd & exit /b 1)
popd

rem ensure index.html exists in repo root (don’t overwrite if user customized)
if not exist "%REPO%index.html" (
  > "%REPO%index.html" (
    echo ^<!doctype html^>
    echo ^<meta charset="utf-8"^>^<title^>gardn^</title^>
    echo ^<style^>html,body{margin:0;height:100%%;background:#111;color:#ddd}canvas{display:block;width:100vw;height:100vh}^</style^>
    echo ^<canvas id="canvas" tabindex="1"^>^</canvas^>
    echo ^<script src="gardn-client.js"^>^</script^>
  )
)

rem prefer hardlinks so future builds auto-update; fallback to copy if needed
del /q "%REPO%gardn-client.js" "%REPO%gardn-client.wasm" 2>nul
mklink /H "%REPO%gardn-client.js"   "%REPO%Client\build\gardn-client.js"   >nul 2>nul || copy /Y "%REPO%Client\build\gardn-client.js"   "%REPO%" >nul
mklink /H "%REPO%gardn-client.wasm" "%REPO%Client\build\gardn-client.wasm" >nul 2>nul || copy /Y "%REPO%Client\build\gardn-client.wasm" "%REPO%" >nul
exit /b 0

:server
pushd "%REPO%Server"
if not exist build\CMakeCache.txt (
  "%CMAKE%" -S . -B build -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=%EMSDK%\upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DWASM_SERVER=1 ^
    -DCMAKE_C_COMPILER_LAUNCHER="cmd.exe;/c" ^
    -DCMAKE_CXX_COMPILER_LAUNCHER="cmd.exe;/c"
)
"%CMAKE%" --build build --target gardn-server --parallel || (popd & exit /b 1)
if not exist node_modules\ws (
  npm i --silent ws
)
popd
exit /b 0

:run
call :kill >nul
pushd "%REPO%"
node Server\build\gardn-server.js
popd
exit /b 0

:all
call "%~f0" client  || exit /b 1
call "%~f0" server  || exit /b 1
call "%~f0" run
exit /b 0

:kill
for /f "tokens=5" %%p in ('netstat -ano ^| findstr :9001') do taskkill /PID %%p /F >nul
exit /b 0

:clean
rd /s /q "%REPO%Client\build" 2>nul
rd /s /q "%REPO%Server\build" 2>nul
exit /b 0