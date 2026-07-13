@echo off
setlocal

set "SCRIPT_DIRECTORY=%~dp0"
set "SOURCE_PATH=%SCRIPT_DIRECTORY%Program.cs"
set "OUTPUT_DIRECTORY=%SCRIPT_DIRECTORY%bin"
set "OUTPUT_PATH=%OUTPUT_DIRECTORY%\PcAiBleBridge.exe"
set "FRAMEWORK_ROOT=%SystemRoot%\Microsoft.NET\Framework64\v4.0.30319"
set "COMPILER_PATH=%FRAMEWORK_ROOT%\csc.exe"
set "METADATA_ROOT=%SystemRoot%\System32\WinMetadata"

if not exist "%COMPILER_PATH%" (
    echo The .NET Framework compiler was not found at "%COMPILER_PATH%".
    exit /b 1
)

if not exist "%SOURCE_PATH%" (
    echo The source file was not found at "%SOURCE_PATH%".
    exit /b 1
)

if exist "%OUTPUT_PATH%" if /i not "%~1"=="--overwrite" (
    echo The output already exists. Re-run with --overwrite to replace it: "%OUTPUT_PATH%"
    exit /b 2
)

if not exist "%OUTPUT_DIRECTORY%" mkdir "%OUTPUT_DIRECTORY%"

"%COMPILER_PATH%" ^
    /nologo ^
    /target:exe ^
    /platform:anycpu ^
    /optimize+ ^
    /langversion:5 ^
    /out:"%OUTPUT_PATH%" ^
    /reference:"%FRAMEWORK_ROOT%\System.Runtime.WindowsRuntime.dll" ^
    /reference:"%FRAMEWORK_ROOT%\System.Runtime.InteropServices.WindowsRuntime.dll" ^
    /reference:"%FRAMEWORK_ROOT%\System.Runtime.dll" ^
    /reference:"%FRAMEWORK_ROOT%\System.Threading.Tasks.dll" ^
    /reference:"%METADATA_ROOT%\Windows.Foundation.winmd" ^
    /reference:"%METADATA_ROOT%\Windows.Devices.winmd" ^
    /reference:"%METADATA_ROOT%\Windows.Storage.winmd" ^
    "%SOURCE_PATH%"

if errorlevel 1 exit /b 1

echo Built "%OUTPUT_PATH%"
endlocal
