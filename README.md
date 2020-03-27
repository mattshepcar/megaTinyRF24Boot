# megaTinyRF24Boot

This is a small over the air boot loader for the ATtiny1614 and other tiny-0/tiny-1 series chips using nRF24L01+ radios.  It's similar to https://github.com/balrog-kun/optiboot but fits in 256 bytes and lets you use some of the radio APIs from your application.

An Arduino sketch is provided to listen for serial communications and talk to the bootloader over the air using another radio.

# Compiling
The bootloader is set up as an MPLAB X IDE project and should be straightforward to compile.  You can use an Xplained nano 416 as a UPDI programmer and debugger or you can use pyupdi or jtag2updi to program the hex file onto your ATtiny.  

Side note: pyupdi and others recommend connecting your serial adapter's TX to UPDI with a 4k7 resistor.  I couldn't get this working but found that a 1N4148 diode works reliably to allow TX to pull UPDI low.  See here https://github.com/dword1511/onewire-over-uart for schematic.  A similar setup should also be possible to enable single pin UART with the tiny's open-drain + loopback modes. 

# Radio configuration
The pins for the radio's CSN and CE pins are configured in main.S along with the power and data rate settings.  The bootloader just sets the radio CE pin high on boot so you can tie it high if you don't want to waste a pin on the MCU and can live with having to power the radio down to switch between RX & TX mode or are just using PTX mode with ack payloads.

The radio address is stored in the first 3 bytes of the USERROW memory on the MCU. The bootloader hex file sets this to '001' by default. The fourth byte contains the radio channel to use.  You can reprogram the radio address and channel over the air using the STK500NRF24 sketch as detailed below.

The bootloader always sets up the radio before starting your application.  The radio will be initialised with the programmed address in the TX, RX0 and RX1 pipes and with all feature bits enabled (variable size payloads and ack payloads are required). The bootloader only listens for packets on pipe 5 which is configured with an LSB address of 'P' (so 'P01' by default).  You can easily set up different pipes/channels for your program to use by just reprogramming the first byte of TX/RXn.

# Usage
The STK500NRF24 sketch should be used on another MCU with an nRF24L01+ in order to transmit programming instructions to the bootloader.  This sketch listens on serial for the STK500 protocol as provided by avrdude or by pystk500 or writestk500.  The baud rate is set to 500k.  When it is not in programming mode the sketch will forward serial data to/from the slave MCU over the air.  

There is also a configuration mode that can be accessed by sending the command \*cfg over the serial link.  When in this mode you can select the address of the radio to program and also reconfigure the connected radio's address.

# pystk500/writestk500
avrdude can be a bit temperamental sometimes, particularly if the application is talking back to the host over serial, so I've included a small python script and C++ program that can be used instead.  The C++ version has a few more features and is a bit more lightweight but is Windows only right now.  The python script requires the pyserial and intelhex python modules.  The radio ID and channel can be passed on the commandline.

# CRC validation

The bootloader only provides functionality for reading back one byte at a time from the target device which can be quite slow for doing a verify.  However, the flash can be checked for correctness using the built-in CRC hardware so it's not required to read back the entire flash to check it.  WriteSTK500 has a --crc commandline option to append the CRC automatically.

# API
The bootloader exposes a few functions that the application can make use of, see megaTinyNrf24.h.  You need to add this to the linker command line in order to use them:

    -Wl,--defsym,nrf24_status=2 -Wl,--defsym,nrf24_command=4 -Wl,--defsym,nrf24_command_long=6 -Wl,--defsym,nrf24_begin_rx=8 -Wl,--defsym,nrf24_begin_tx=10 -Wl,--defsym,nrf24_read_payload=12 -Wl,--defsym,nrf24_boot_poll=14

If you want your application to respond to OTA programming requests you should keep RX pipe 5 enabled and periodically call nrf24_boot_poll which will perform a software reset if a packet is detected in that pipe.

# Arduino

If you're using megaTinyCore https://github.com/SpenceKonde/megaTinyCore you can make a copy of the atxy4o config in boards.txt and rename it to atxy4rf and makes these changes:

	atxy4rf.name=ATtiny1614/1604/814/804/414/404/214/204 (nRF24 boot)
	atxy4rf.upload.speed=500000
	atxy4rf.compiler.c.elf.extra_flags=-Wl,--defsym,nrf24_status=2 -Wl,--defsym,nrf24_command=4 -Wl,--defsym,nrf24_command_long=6 -Wl,--defsym,nrf24_begin_rx=8 -Wl,--defsym,nrf24_begin_tx=10 -Wl,--defsym,nrf24_read_payload=12 -Wl,--defsym,nrf24_boot_poll=14
    atxy4rf.build.text_section_start=.text=0x100
