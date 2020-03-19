;
; NRF24BootLoaderAsm.asm
;
; Created: 15/03/2020 14:50:27
; Author : Matt Shepcar
;
    
#include <avr/io.h>
#include "nRF24L01.h"

; PIN configuration settings:

CSN_PORT_DIR = VPORTB_DIR
CSN_PORT_OUT = VPORTB_OUT
CSN_PIN = 0

CE_PORT_DIR = VPORTB_DIR
CE_PORT_OUT = VPORTB_OUT
CE_PIN = 0

LED_PORT_DIR = VPORTB_DIR
LED_PORT_OUT = VPORTB_OUT
LED_PIN = 1

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

; vectors for the application to use to access the radio/SPI functions

main:		    	rjmp    start_bootloader
vec_nrf24_status:   rjmp    nrf24_status
vec_nrf24_command:  rjmp    nrf24_command
vec_nrf24_begin:    rjmp    nrf24_begin
vec_spi_transfer:   rjmp    spi_transfer
vec_nrf24_end:	    rjmp    nrf24_end
vec_poll_reset: 
	; resets into bootloader when a programming packet is received
    rcall   nrf24_status
    andi    r24, 0xE
    cpi	    r24, 0xA ; packet in pipe 5?
    brne    vec_nrf24_end
    ldi	    r24, CPU_CCP_IOREG_gc
    out	    CPU_CCP, r24
    sts	    RSTCTRL_SWRR, r25
    
start_bootloader: 
    ; set radio CSN and CE pins high
    sbi	    CSN_PORT_DIR, CSN_PIN
    sbi	    CSN_PORT_OUT, CSN_PIN
    sbi	    CE_PORT_DIR, CE_PIN
    sbi	    CE_PORT_OUT, CE_PIN

    ; turn LED on
    sbi	    LED_PORT_DIR, LED_PIN
    sbi	    LED_PORT_OUT, LED_PIN    

    ; initialise SPI pins
    sbi	    VPORTA_DIR, 1
    sbi	    VPORTA_DIR, 3

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
    
    ; last command in radio_registers reads back the RF_SETUP register.
    ; here we verify it to check if radio is available and if not just run the app
    cpi	    r24, SETUP_VALUE
    brne    jump_to_app
    
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
    ldi     YH, hi8(INTERNAL_SRAM_START) ; packet buffer
    ldi	    r23, 0 ; packets left in page (0 = awaiting new command)

wait_for_packet:
    rcall   nrf24_status
    andi    r24, 0xE ; RX FIFO status
    cpi	    r24, 0xE ; 0xE = empty
    breq    wait_for_packet

	; got a packet, reset watchdog
    wdr 	

	; read fixed payload size of 32 bytes
    ldi	    r24, R_RX_PAYLOAD
    rcall   nrf24_begin
    ldi	    r16, 32
read_packet:
    rcall   spi_transfer
    st	    Y+, r24
    dec	    r16
    brne    read_packet
    sbi	    CSN_PORT_OUT, CSN_PIN

    subi    r23, 1
    breq    write_page
    brcc    wait_for_packet

read_command:
    sbiw    Y, 32
    ldd	    r23, Y+5 ; number of packets (or single byte data)
    ldd     r24, Y+2
    ldd	    r25, Y+3
    ldd     YH, Y+4  ; address to program
    mov	    YL, r25
    cpi	    r24, 'F' ; flash
    breq    wait_for_packet
    brcc    jump_to_app

write_byte:
    st      Y, r23
write_page:
    ldi	    ZH, hi8(NVMCTRL_CTRLA) ; ZL = 0 from SPI0_base
    ldi     r24, NVMCTRL_CMD_PAGEERASEWRITE_gc
    ldi	    r16, CPU_CCP_SPM_gc
    out	    CPU_CCP, r16
    st	    Z, r24
wait_nvm:
    ldd	    r24, Z + (NVMCTRL_STATUS - NVMCTRL_CTRLA)
    andi    r24, NVMCTRL_FBUSY_bm|NVMCTRL_EEBUSY_bm
    brne    wait_nvm
    rjmp    wait_for_command
       
spi_transfer_from_x:
    ld      r24, X+
nrf24_begin:
    cbi	    CSN_PORT_OUT, CSN_PIN
spi_transfer:
    ldi	    ZL, lo8(SPI0_base)
    ldi	    ZH, hi8(SPI0_base)
    ldi	    r25, SPI_SSD_bm
    std	    Z + SPI_CTRLB_offset, r25
    ldi	    r25, SPI_ENABLE_bm | SPI_MASTER_bm | SPI_CLK2X_bm | SPI_PRESC_DIV4_gc
    std	    Z + SPI_CTRLA_offset, r25
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
    sbi	    CSN_PORT_OUT, CSN_PIN
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
    .byte W_REGISTER | DYNPD, 0x1F ; disable dynamic payload length on pipe 5
    .byte W_REGISTER | RX_PW_P5, 32 ; fixed payload of 32 bytes
    .byte W_REGISTER | EN_RXADDR, (1 << 5) ; enable rx5 pipe
    .byte W_REGISTER | RX_ADDR_P5, 'P' ; programming pipe address
    .byte W_REGISTER | FEATURE, (1 << EN_DPL) ; dynamic payloads
    .byte W_REGISTER | SETUP_RETR, 0x7F ; maximum retries at 4ms
    .byte R_REGISTER | RF_SETUP, 0 ; read back setup register to check radio is present
radio_registers_end: ; power up command:
    .byte W_REGISTER | CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP) | (1 << PRIM_RX)

; a few ways to reclaim some more bytes:
;  - combine register setting for SPI, CSN & CE
;  - get rid of radio existence check (3 words)
;  - get rid of LED (2 words)
;  - store packets backwards and use st -Y to remove sbiw Y,32 (1 word)
;  - remove jump table (6 words)

    .org 0x100
app:
    ; dummy app that just waits for an incoming reboot message
    wdr
    rcall vec_poll_reset
    rjmp app
    
.end