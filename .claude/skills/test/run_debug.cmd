@echo off
start "DOSBox Debug" bin\x64\Debug\dosbox-x.exe -defaultconf -console -set "log tcp_debug_port=12345" -break-start
