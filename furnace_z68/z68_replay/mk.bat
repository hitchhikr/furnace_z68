@echo off
tools\vasm -nomsg=2050 -nomsg=2054 -nomsg=2052 -quiet -devpac -D__PACKED__ -Fbin -o test.r z68_replay.asm
if errorlevel 1 goto error
:error
