@echo off
set SCRIPTDIR=%~dp0
set REPOROOT=%SCRIPTDIR%..\..\..
start "DOSBox Debug" "%REPOROOT%\bin\x64\Debug\dosbox-x.exe" -defaultconf -console -set "log tcp_debug_port=12345" -break-start
