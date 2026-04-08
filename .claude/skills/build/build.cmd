@echo off
setlocal

:: DOSBox-X local build helper
:: Usage: build.cmd [Configuration] [Platform] [Target]
:: Defaults: Debug x64 dosbox-x

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

set PLATFORM=%~2
if "%PLATFORM%"=="" set PLATFORM=x64

set TARGET=%~3
if "%TARGET%"=="" set TARGET=dosbox-x

:: Resolve repo root from this script's location (skills/build/build.cmd -> repo root)
set SCRIPTDIR=%~dp0
set REPOROOT=%SCRIPTDIR%..\..\..

set VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community
set SLN=%REPOROOT%\vs\dosbox-x.sln

echo === DOSBox-X Build ===
echo Configuration: %CONFIG%
echo Platform:      %PLATFORM%
echo Target:        %TARGET%

call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -no_logo

:: Detect available platform toolset by finding PlatformToolsets dir
set TOOLSET=
for /f "tokens=*" %%i in ('dir /b /ad "%VSDIR%\MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\" 2^>nul') do set TOOLSET=%%i
if not defined TOOLSET (
    :: Fallback: try v170
    for /f "tokens=*" %%i in ('dir /b /ad "%VSDIR%\MSBuild\Microsoft\VC\v170\Platforms\x64\PlatformToolsets\" 2^>nul') do set TOOLSET=%%i
)
if not defined TOOLSET set TOOLSET=v143

echo Toolset:       %TOOLSET%
echo.

msbuild "%SLN%" /t:%TARGET% /p:Configuration="%CONFIG%" /p:Platform=%PLATFORM% /p:PlatformToolset=%TOOLSET% /m /nologo /v:minimal /clp:ErrorsOnly;WarningsOnly
set EXITCODE=%ERRORLEVEL%

if %EXITCODE%==0 (
    echo.
    echo === BUILD SUCCEEDED ===
) else (
    echo.
    echo === BUILD FAILED (exit code %EXITCODE%) ===
)

exit /b %EXITCODE%
