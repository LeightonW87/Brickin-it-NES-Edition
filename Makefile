# Requires cc65 (bin folder on PATH) and GNU make.
CL65 = cl65
NAME = brickinit

$(NAME).nes: main.c logic.c logic.h config.h themes.h logo.h crt0.o chr.s tiles.chr neslib.h nes.cfg
	$(CL65) -t nes -C nes.cfg -Oisr -o $@ main.c logic.c crt0.o chr.s

crt0.o: crt0.s neslib.sinc display.sinc famitone2.sinc lz4vram.s
	$(CL65) -t nes -Oisr -c crt0.s

clean:
	-del /Q *.o *.nes 2>nul
	-rm -f *.o *.nes
.PHONY: clean
