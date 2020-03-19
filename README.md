# megaTinyRF24Boot

This is a small over the air boot loader for the ATtiny1614 and other tiny-0/tiny-1 series chips using nRF24L01+ radios.  It's similar to https://github.com/balrog-kun/optiboot but fits in 256 bytes and lets you use some of the radio APIs from your application.

An Arduino sketch is provided to listen for serial communications and talk to the bootloader over the air using another radio.

# Compiling
The bootloader is set up as an MPLAB X IDE project and should be straightforward to compile.  You can use an Xplained nano 416 as a UPDI programmer and debugger or you can use pyupdi or jtag2updi to program the hex file onto your ATtiny.  

# Radio configuration
The pins for the radio's CSN and CE pins are configured in main.S.  The bootloader just sets the radio CE pin high on boot so you can tie it high if you don't want to waste a pin on the MCU and can live with having to power the radio down to switch between RX & TX mode.

The register configuration is at the bottom of main.S if you need to change the channel, power levels, bitrate or retry behaviour.

The radio address is stored in the first 3 bytes of the USERROW memory on the MCU. The bootloader hex file sets this to '001' by default. You can reprogram the radio address over the air using the STK500NRF24 sketch as detailed below.

The bootloader always sets up the radio before starting your application.  The radio will be initialised with the programmed address in the TX, RX0 and RX1 pipes. The bootloader only listens for packets on pipe 5 which is configured with an LSB address of 'P' (so 'P01' by default).  You can easily set up different pipes/channels for your program to use by just reprogramming the first byte of TX/RXn.

# Flashing
The STK500NRF24 sketch should be used on another MCU with an nRF24L01+ in order to transmit programming instructions to the bootloader.  This sketch listens on serial for the STK500 protocol as provided by avrdude etc.  The baud rate is set to 5000000.  When it is not in programming mode the sketch will forward serial data to/from the slave MCU over the air.  

There is also a configuration mode that can be accessed by sending the command \*cfg over the serial link.  When in this mode you can select the address of the radio to program and also reconfigure the connected radio's address.

# API
The bootloader exposes a few functions that the application can make use of:

    uint8_t nrf24_status(); // retrieve the value of the radio's status register
    uint8_t nrf24_command(uint8_t cmd, uint8_t data = RF24_NOP); // send a two byte command to the radio
    uint8_t nrf24_begin(uint8_t cmd); // send first byte of a command to the radio (set CSN low and transfer byte)
    uint8_t spi_transfer(uint8_t x); // equivalent to SPI.transfer
    void nrf24_end(); // end command (set CSN high)
    void nrf24_boot_poll(); // will reset into the bootloader if any programming packets are detected

I should probably make a library file for these but you can just add this to the linker command line:

    -Wl,--defsym,nrf24_status=2 -Wl,--defsym,nrf24_command=4 -Wl,--defsym,nrf24_begin=6 -Wl,--defsym,spi_transfer=8 -Wl,--defsym,nrf24_end=10 -Wl,--defsym,nrf24_boot_poll=12

Useful functions to define (I'll add a more complete lib at some point):

    inline void nrf24_write_register(uint8_t reg, uint8_t data) { nrf24_command(reg | W_REGISTER, data); }
    inline uint8_t nrf24_read_register(uint8_t reg) { return nrf24_command(reg, 0); }

# Arduino

If you're using megaTinyCore https://github.com/SpenceKonde/megaTinyCore you can make a copy of the atxy4o config in boards.txt and rename it to atxy4rf and makes these changes:

    atxy4rf.compiler.c.elf.extra_flags=-Wl,--defsym,nrf24_status=2 -Wl,--defsym,nrf24_command=4 -Wl,--defsym,nrf24_begin=6 -Wl,--defsym,spi_transfer=8 -Wl,--defsym,nrf24_end=10 -Wl,--defsym,nrf24_boot_poll=12
    atxy4rf.build.text_section_start=.text=0x100
