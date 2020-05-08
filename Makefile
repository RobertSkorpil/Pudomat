PART = m168
AVRFLAGS = -mmcu=atmega168 -g -Isrc/
AVRCFLAGS = $(AVRFLAGS) -Os -std=gnu99 -mcall-prologues -DF_CPU=12000000
AVRSFLAGS = $(AVRFLAGS) -x assembler-with-cpp
CFLAGS = -Os -std=gnu99

all: bin/firmware.dump bin/pudomat

.PHONY : upload fuses clean setuid install

install: bin/pudomat
	sudo cp -f bin/pudomat /usr/local/bin/
	sudo chown root /usr/local/bin/pudomat 
	sudo chmod 4777 /usr/local/bin/pudomat

upload: bin/firmware.elf
	sudo avrdude -c dapa -p $(PART) -U flash:w:bin/firmware.elf

fuses:
	sudo avrdude -c dapa -p $(PART) -U efuse:w:0xff:m -U hfuse:w:0xD7:m -U lfuse:w:0xFF:m

clean:
	rm -f bin/* obj/*

setuid: bin/pudomat
	sudo chown root bin/pudomat 
	sudo chmod 4777 bin/pudomat

bin/pudomat: obj/app.o
	gcc $(CFLAGS) $^ -lusb-1.0 -o$@

obj/app.o: src/app.c src/comm.h
	gcc $(CFLAGS) -c -o$@ $<

bin/firmware.dump: bin/firmware.elf
	avr-objdump -xd $< > $@

bin/firmware.elf: obj/firmware.o obj/usbdrv.o obj/usbdrvasm.o obj/ds18b20.o obj/onewire.o obj/romsearch.o
	avr-gcc $(AVRCFLAGS) -o$@ $^

obj/firmware.o: src/firmware.c src/comm.h
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/usbdrvasm.o: src/usbdrvasm.S
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/usbdrv.o: src/usbdrv.c src/usbconfig.h
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/ds18b20.o: src/ds18b20.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/onewire.o: src/onewire.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<

obj/romsearch.o: src/romsearch.c
	avr-gcc $(AVRCFLAGS) -c -o$@ $<
