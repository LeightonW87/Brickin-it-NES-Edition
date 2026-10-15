@echo off
REM Builds brickinit.nes without needing GNU make. cc65\bin must be on your PATH.
cl65 -t nes -Oisr -c crt0.s || goto :err
cl65 -t nes -C nes.cfg -Oisr -o brickinit.nes main.c logic.c crt0.o chr.s || goto :err
echo Build OK: brickinit.nes
goto :eof
:err
echo BUILD FAILED
exit /b 1
