;
; NRF24BootLoaderAsm.asm
;
; Created: 15/03/2020 14:50:27
; Author : Matt Shepcar
;
        
#include <avr/io.h>
#include "nRF24L01.h"
  
; PIN configuration settings:
#define CSN_PORT VPORTB_OUT
#define CSN_PIN 0
#define CE_PORT VPORTB_OUT
#define CE_PIN 0 
#define LED_PORT VPORTB_OUT
#define LED_PIN 1
#define MOSI_PIN 1
#define SCK_PIN 3
; make sure to update these with the right pins on the right ports:
#define PORTA_DIR_CFG _BV(MOSI_PIN) | _BV(SCK_PIN)
#define PORTB_DIR_CFG _BV(CSN_PIN) | _BV(CE_PIN) | _BV(LED_PIN)

; define to skip 1 second power-up delay if there's no radio chip connected
#define CHECK_RADIO 1

; check that command packets start with 0x8899 signature
; (not strictly needed as only programming packets should go to this port)
#define CHECK_COMMAND_SIGNATURES 1
 
#define SPI0_base 0x800  
#define SPI_CTRLA_offset (SPI0_CTRLA - SPI0_base)
#define SPI_CTRLB_offset (SPI0_CTRLB - SPI0_base)
#define SPI_DATA_offset (SPI0_DATA - SPI0_base)
#define SPI_INTFLAGS_offset (SPI0_INTFLAGS - SPI0_base)
#define SPI_PRESC_DIV4_gc (0x00<<1)       ; System Clock / 4
#define SPI_PRESC_DIV16_gc (0x01<<1)      ; System Clock / 16
#define SPI_PRESC_DIV64_gc (0x02<<1)      ; System Clock / 64
#define SPI_PRESC_DIV128_gc (0x03<<1)     ; System Clock / 128
#define CPU_CCP_SPM_gc (0x9D)          ; SPM Instruction Protection
#define CPU_CCP_IOREG_gc (0xD8)        ; IO Register Protection
#define NVMCTRL_CMD_PAGEERASEWRITE_gc (0x03) ; Erase and write page
    
    .text
    .org 0
   .global main

; vectors for the application to use to access the radio functions
main:
    rjmp    start_bootloader
vec_nrf24_status:
    rjmp    nrf24_status
vec_nrf24_command:
    rjmp    nrf24_command
vec_nrf24_transfer:
    rjmp    nrf24_begin
vec_nrf24_read_rx_payload:
    rjmp    nrf24_read_rx_payload
vec_poll_reset: 
    ; reset into bootloader when a programming packet is received
    rcall   nrf24_status
    andi    r24, 0xE
    cpi	    r24, 0xA ; packet in pipe 5?
    brne    return
    ldi	    r24, CPU_CCP_IOREG_gc
    out	    CPU_CCP, r24
    sts	    RSTCTRL_SWRR, r25

nrf24_read_rx_payload:
    movw    X, r24
nrf24_read_rx_payload_to_x:
    ldi	    r24, R_RX_PAYLOAD
    rcall   nrf24_begin
read_packet:
    rcall   spi_transfer
    st	    X+, r24
    dec	    r22
    brne    read_packet
return:
    rjmp    nrf24_end
    
start_bootloader:   
    ; configure pin dirs and set CE, CSN & LED high
    ldi	    r24, PORTA_DIR_CFG
    out	    VPORTA_DIR, r24
    ;out	    VPORTA_OUT, r24
    ldi	    r24, PORTB_DIR_CFG
    out	    VPORTB_DIR, r24
    out	    VPORTB_OUT, r24
   
    ; initialise radio registers
    ldi	    YL, lo8(radio_registers+MAPPED_PROGMEM_START)
    ldi	    YH, hi8(radio_registers+MAPPED_PROGMEM_START)
init_radio:
    rcall   nrf24_command_from_y
    cpi	    YL, lo8(radio_registers_end)
    brne    init_radio

    ; set watchdog timer (uses ZH = 8 from SPI0_base)
    ldi	    r25, CPU_CCP_IOREG_gc
    out	    CPU_CCP, r25
    sts	    WDT_CTRLA, ZH ; watchdog = 1s

#if CHECK_RADIO
    ; last command in radio_registers reads back the RF_SETUP register.
    ; here we verify it to check if radio is available and if not just run the app
    cpi	    r24, SETUP_VALUE
    brne    jump_to_app
#endif
    
    ; set radio addresses from user signature area
    ldi	    r24, W_REGISTER | TX_ADDR
    rcall   nrf24_setaddress_from_usersig
    ldi	    r24, W_REGISTER | RX_ADDR_P0
    rcall   nrf24_setaddress_from_usersig
    ldi	    r24, W_REGISTER | RX_ADDR_P1
    rcall   nrf24_setaddress_from_usersig
    
    ; power up the radio in RX mode
    rcall   nrf24_command_from_y
    
    ; if reset was from watchdog then start the app
    lds	    r0, RSTCTRL_RSTFR
    sts	    RSTCTRL_RSTFR, r0
    sbrc    r0, RSTCTRL_WDRF_bp 
jump_to_app:
    rjmp    app
   
wait_for_command:
    ldi     XH, hi8(INTERNAL_SRAM_START) ; packet buffer
    ldi	    r23, 0 ; packets left in page (0 = awaiting new command)
    
read_page:
    ; save start address in Y
    movw    Y, X
    
wait_for_packet:
    rcall   nrf24_status
    subi    r24, 0xE 
    brhc    wait_for_packet ; low nybble less than 0xE?

    ; got a packet, reset watchdog
    wdr 	

    ; read fixed payload size of 32 bytes
    ldi	    r22, 32
    rcall   nrf24_read_rx_payload_to_x

    subi    r23, 1
    breq    write_page
    brcc    wait_for_packet

read_command:
#if CHECK_COMMAND_SIGNATURES
    ld	    r24, Y
    subi    r24, 0x88
    ldd	    r24, Y + 1
    sbci    r24, 0x99
    brne    wait_for_command
#endif
    ldd     r24, Y + 2 ; command
    ldd	    XL, Y + 3 ; address to program
    ldd     XH, Y + 4  
    ldd	    r23, Y + 5 ; number of packets (or single byte data)
    cpi	    r24, 'F' ; flash
    breq    read_page
    brcc    jump_to_app

write_byte:
    st      X, r23
write_page:
    ldi	    ZH, hi8(NVMCTRL_CTRLA) ; ZL = 0 from SPI0_base
    ldi     r24, NVMCTRL_CMD_PAGEERASEWRITE_gc
    ldi	    r25, CPU_CCP_SPM_gc
    out	    CPU_CCP, r25
    st	    Z, r24
wait_nvm:
    ldd	    r24, Z + (NVMCTRL_STATUS - NVMCTRL_CTRLA)
    andi    r24, NVMCTRL_FBUSY_bm|NVMCTRL_EEBUSY_bm
    brne    wait_nvm
    rjmp    wait_for_command
       
spi_transfer_from_x:
    ld      r24, X+
nrf24_begin:
    ldi	    ZL, lo8(SPI0_base)
    ldi	    ZH, hi8(SPI0_base)
    ldi	    r25, SPI_SSD_bm ; 0x04
    std	    Z + SPI_CTRLB_offset, r25
    ldi	    r25, SPI_ENABLE_bm | SPI_MASTER_bm | SPI_CLK2X_bm | SPI_PRESC_DIV4_gc ; 0x31
    std	    Z + SPI_CTRLA_offset, r25
    cbi	    CSN_PORT, CSN_PIN
spi_transfer:    
    std     Z + SPI_DATA_offset, r24
wait_spi:
    ldd	    r24, Z + SPI_INTFLAGS_offset
    sbrs    r24, SPI_RXCIF_bp
    rjmp    wait_spi
    ldd	    r24, Z + SPI_DATA_offset
    ret

nrf24_command_from_y:
    ld	    r24, Y+
    ld	    r22, Y+
nrf24_command:
    rcall   nrf24_begin
    mov	    r24, r22
    rcall   spi_transfer
nrf24_end:    
    sbi	    CSN_PORT, CSN_PIN
    ret
  
nrf24_setaddress_from_usersig:
    ldi	    XH, hi8(USER_SIGNATURES_START)
    ldi	    XL, lo8(USER_SIGNATURES_START)
nrf24_setaddress_from_x:
    rcall   nrf24_begin
    rcall   spi_transfer_from_x
    rcall   spi_transfer_from_x
    rcall   spi_transfer_from_x
nrf24_status:    
    ldi	    r24, RF_STATUS
    rjmp    nrf24_command
    
      
SETUP_VALUE = (1 << RF_PWR_LOW) | (1 << RF_PWR_HIGH) | (1 << RF_DR_HIGH)

radio_registers:
    .byte W_REGISTER | CONFIG, 0 ; power down 
    .byte W_REGISTER | EN_AA, 0x3F ; auto acknowledge on all pipes
    .byte W_REGISTER | SETUP_AW, 1 ; address field width = 3 bytes
    .byte W_REGISTER | RF_CH, 76 ; channel 76
    .byte W_REGISTER | RF_SETUP, SETUP_VALUE ; high power, high data rate
    .byte W_REGISTER | DYNPD, 0x3F ; dynamic payload on all pipes
    .byte W_REGISTER | EN_RXADDR, (1 << 5) ; enable rx5 pipe
    .byte W_REGISTER | RX_ADDR_P5, 'P' ; programming pipe address
    .byte W_REGISTER | FEATURE, (1 << EN_DPL) ; dynamic payloads
    .byte W_REGISTER | SETUP_RETR, 0x7F ; maximum retries at 4ms
#if CHECK_RADIO
    .byte R_REGISTER | RF_SETUP, 0 ; read back setup register to check radio is present
#endif
radio_registers_end: ; power up command:
    .byte W_REGISTER | CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP) | (1 << PRIM_RX)

    .org 0x100
app:
    ; dummy app that just waits for an incoming reboot message
    wdr
    rcall vec_poll_reset
    rjmp app
    
.end