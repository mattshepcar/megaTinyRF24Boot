# megaTinyRF24Boot

This is a small over the air boot loader for the ATtiny1614 and other tiny-0/tiny-1 series chips using nRF24L01+ radios.  It's similar to https://github.com/balrog-kun/optiboot but fits in 256 bytes and lets you use some of the radio APIs from your application.

An Arduino sketch is provided to listen for serial communications and talk to the bootloader over the air using another radio.

# Compiling
The bootloader is set up as an MPLAB X IDE project and should be straightforward to compile.  You can use an Xplained nano 416 as a UPDI programmer and debugger or you can use pyupdi or jtag2updi to program the hex file onto your ATtiny.  

Side note: pyupdi and others recommend connecting your serial adapter's TX to UPDI with a 4k7 resistor.  I couldn't get this working but found that a 1N4148 diode works reliably to allow TX to pull UPDI low.  See here https://github.com/dword1511/onewire-over-uart for schematic.  A similar setup should also be possible to enable single pin UART with the tiny's open-drain + loopback modes. 

# Radio configuration
The pins for the radio's CSN and CE pins are configured in main.S.  The bootloader just sets the radio CE pin high on boot so you can tie it high if you don't want to waste a pin on the MCU and can live with having to power the radio down to switch between RX & TX mode.

The register configuration is at the bottom of main.S if you need to change the channel, power levels, bitrate or retry behaviour.

The radio address is stored in the first 3 bytes of the USERROW memory on the MCU. The bootloader hex file sets this to '001' by default. You can reprogram the radio address over the air using the STK500NRF24 sketch as detailed below.

The bootloader always sets up the radio before starting your application.  The radio will be initialised with the programmed address in the TX, RX0 and RX1 pipes. The bootloader only listens for packets on pipe 5 which is configured with an LSB address of 'P' (so 'P01' by default).  You can easily set up different pipes/channels for your program to use by just reprogramming the first byte of TX/RXn.

# Usage
The STK500NRF24 sketch should be used on another MCU with an nRF24L01+ in order to transmit programming instructions to the bootloader.  This sketch listens on serial for the STK500 protocol as provided by avrdude etc.  The baud rate is set to 500k.  When it is not in programming mode the sketch will forward serial data to/from the slave MCU over the air.  

There is also a configuration mode that can be accessed by sending the command \*cfg over the serial link.  When in this mode you can select the address of the radio to program and also reconfigure the connected radio's address.

# pystk500
avrdude can be a bit temperamental sometimes, particularly if the application is talking back to the host over serial, so I've included a small python script that can be used instead.  It requires the pyserial and intelhex python modules.  It also has extra functionality for setting the radio ID on the command line and is a bit quicker than avrdude.

# API
The bootloader exposes a few functions that the application can make use of:

extern "C" {
    void nrf24_boot_poll(); // reset into the bootloader if any programming packets are received
    uint8_t nrf24_status(); // retrieve the value of the radio's status register
    uint8_t nrf24_command(uint8_t cmd, uint8_t data = 0); // send a two byte command to the radio
    uint8_t nrf24_command_long(uint8_t cmd, const uint8_t* data, uint8_t count); // send a longer command
    uint8_t nrf24_commands(const uint8_t* cmds, uint8_t count);  // send a sequence of 2 byte commands
	void nrf24_begin_rx(uint8_t activeRxPipes); // set active RX pipes and power up radio in RX mode
    uint8_t nrf24_set_tx_address(const uint8_t* addr); // set radio TX and RX0 addresses
    uint8_t nrf24_transfer(uint8_t cmd); // set CSN low and transfer byte to the radio over SPI
    uint8_t* nrf24_read_payload(uint8_t* dst); // read payload from RX FIFO and returned updated dst pointer
    inline void nrf24_write_register(uint8_t reg, uint8_t data) { nrf24_command(reg | W_REGISTER, data); }
    inline uint8_t nrf24_read_register(uint8_t reg) { return nrf24_command(reg, 0); }
    inline void nrf24_end() { VPORTB.OUT |= 1; }
}
    
I should probably make a library file for these but you can just add this to the linker command line:

    -Wl,--defsym,nrf24_status=2 -Wl,--defsym,nrf24_command=4 -Wl,--defsym,nrf24_command_long=6 -Wl,--defsym,nrf24_commands=8 -Wl,--defsym,vec_nrf24_set_tx_address=10 -Wl,--defsym,nrf24_transfer=12 -Wl,--defsym,nrf24_begin_rx=14 -Wl,--defsym,nrf24_read_payload=16 -Wl,--defsym,nrf24_boot_poll=18

If you want your application to respond to OTA programming requests you should keep RX pipe 5 enabled and periodically call nrf24_boot_poll which will perform a software reset if a packet is detected in that pipe.

# Arduino

If you're using megaTinyCore https://github.com/SpenceKonde/megaTinyCore you can make a copy of the atxy4o config in boards.txt and rename it to atxy4rf and makes these changes:

	atxy4rf.name=ATtiny1614/1604/814/804/414/404/214/204 (nRF24 boot)
	atxy4rf.upload.speed=500000
	atxy4rf.compiler.c.elf.extra_flags=-Wl,--defsym,nrf24_status=2 -Wl,--defsym,nrf24_command=4 -Wl,--defsym,nrf24_command_long=6 -Wl,--defsym,nrf24_commands=8 -Wl,--defsym,vec_nrf24_set_tx_address=10 -Wl,--defsym,nrf24_transfer=12 -Wl,--defsym,nrf24_begin_rx=14 -Wl,--defsym,nrf24_read_payload=16 -Wl,--defsym,nrf24_boot_poll=18
    atxy4rf.build.text_section_start=.text=0x100
