@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not defined VSCMD_VER (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [!] Visual Studio Installer / vswhere.exe not found.
        pause
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualCpp.Tools.HostX64.TargetX64 -property installationPath`) do set "VSROOT=%%i"
    if not defined VSROOT (
        echo [!] Install the Visual C++ build tools workload in Visual Studio.
        pause
        exit /b 1
    )
    call "!VSROOT!\VC\Auxiliary\Build\vcvarsall.bat" x64
    if errorlevel 1 exit /b 1
)

set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "OUT=%ROOT%dist"
set "OBJ=%ROOT%build\obj"
set "IMGUI=%SRC%\ImGui"
set "MINHOOK=%SRC%\MinHook"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

cl /nologo /EHsc /O2 /fp:fast /MT /std:c++20 /Zc:threadSafeInit- /DNDEBUG /DWIN32_LEAN_AND_MEAN ^
 /I"%SRC%" /I"%SRC%\Client Manager" /I"%SRC%\EU4 SDK" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%MINHOOK%\include" ^
 /Fo"%OBJ%\\" /c ^
 "%SRC%\TurretEditorStandalone.cpp" "%SRC%\Logger\Logger.cpp" "%SRC%\EU4 SDK\SDK.cpp" ^
 "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" ^
 "%IMGUI%\backends\imgui_impl_win32.cpp" "%IMGUI%\backends\imgui_impl_dx11.cpp" ^
 "%MINHOOK%\src\buffer.c" "%MINHOOK%\src\hook.c" "%MINHOOK%\src\trampoline.c" "%MINHOOK%\src\hde\hde64.c"
if errorlevel 1 goto failed

link /nologo /DLL /INCREMENTAL:NO /OPT:REF /OPT:ICF /OUT:"%OUT%\paidini.dll" ^
 "%OBJ%\TurretEditorStandalone.obj" "%OBJ%\Logger.obj" "%OBJ%\SDK.obj" ^
 "%OBJ%\imgui.obj" "%OBJ%\imgui_draw.obj" "%OBJ%\imgui_tables.obj" "%OBJ%\imgui_widgets.obj" ^
 "%OBJ%\imgui_impl_win32.obj" "%OBJ%\imgui_impl_dx11.obj" ^
 "%OBJ%\buffer.obj" "%OBJ%\hook.obj" "%OBJ%\trampoline.obj" "%OBJ%\hde64.obj" ^
 d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 goto failed

cl /nologo /EHsc /O1 /Os /Gy /GS- /MD /DNDEBUG /W3 /Fo"%OBJ%\\" ^
 "%ROOT%injector.cpp" ^
 /link /INCREMENTAL:NO /OPT:REF /OPT:ICF /SUBSYSTEM:CONSOLE /OUT:"%OUT%\injector.exe" ^
 kernel32.lib ntdll.lib advapi32.lib user32.lib gdi32.lib
if errorlevel 1 goto failed

echo [+] Built standalone files in %OUT%
pause
exit /b 0
:failed
echo [!] Build failed; see compiler/linker diagnostics above.
pause
exit /b 1
