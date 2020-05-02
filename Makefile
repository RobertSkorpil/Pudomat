AVRFLAGS = -mmcu=atmega168 -g -Isrc/
AVRCFLAGS = $(AVRFLAGS) -Os -std=gnu99 -mcall-prologues -DF_CPU=12000000
AVRSFLAGS = $(AVRFLAGS) -x assembler-with-cpp
CFLAGS = -Os -std=gnu99

all: bin/firmware.dump bin/pudomat

.PHONY : upload clean
upload: bin/firmware.elf
	sudo avrdude -c dapa -p m168 -U flash:w:bin/firmware.elf

clean:
	rm -f bin/* obj/*

bin/pudomat: obj/app.o
	gcc $(CFLAGS) $^ -lusb-1.0 -o$@

obj/app.o: src/app.c
	gcc $(CFLAGS) -c -o$@ $<

bin/firmware.dump: bin/firmware.elf
	avr-objdump -d $< > $@

bin/firmware.elf: obj/firmware.o obj/usbdrv.o obj/usbdrvasm.o obj/ds18b20.o obj/onewire.o obj/romsearch.o
	avr-gcc $(AVRCFLAGS) -o$@ $^

obj/firmware.o: src/firmware.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/usbdrvasm.o: src/usbdrvasm.S
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/usbdrv.o: src/usbdrv.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/ds18b20.o: src/ds18b20.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/onewire.o: src/onewire.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/romsearch.o: src/romsearch.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<
