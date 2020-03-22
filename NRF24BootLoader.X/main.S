;
; NRF24BootLoaderAsm.asm
;
; Created: 15/03/2020 14:50:27
; Author : Matt Shepcar
;
        
#include <avr/io.h>
#include "nRF24L01.h"
#include "registers.h"

; radio settings
#define SETUP_VALUE (_BV(RF_PWR_LOW) | _BV(RF_PWR_HIGH) | _BV(RF_DR_HIGH))
#define CHANNEL	     76
#define ADDRESS_WIDTH 3

; PIN settings
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

    .text
    .org 0
    .global main

main:
    rjmp    start_bootloader

nrf24_status:
    ; vectors for the application to use to access the radio functions
    ldi	    r24, RF_STATUS
    rjmp    nrf24_command
    rjmp    nrf24_command_long
    rjmp    nrf24_commands
    rjmp    nrf24_set_tx_address ; not all that useful? you generally only want to reprogram the first byte
    rjmp    nrf24_begin
    rjmp    nrf24_begin_rx
    rjmp    nrf24_read_rx_payload
nrf24_poll_reset:    
    ; reset into bootloader when a programming packet is received
    rcall   nrf24_status
    andi    r24, 0xE
    cpi	    r24, 0xA ; packet in pipe 5?
    brne    return
    ldi	    r24, CPU_CCP_IOREG_gc
    out	    CPU_CCP, r24
    sts	    RSTCTRL_SWRR, r25

radio_registers:
    .byte W_REGISTER | CONFIG, 0 ; power down 
    .byte W_REGISTER | EN_AA, 0x3F ; auto acknowledge on all pipes
    .byte W_REGISTER | SETUP_AW, (ADDRESS_WIDTH - 2)
    .byte W_REGISTER | RF_CH, CHANNEL 
    .byte W_REGISTER | RF_SETUP, SETUP_VALUE ; high power, high data rate
    .byte W_REGISTER | DYNPD, 0x3F ; dynamic payload on all pipes  
    .byte W_REGISTER | RX_ADDR_P5, 'P' ; programming pipe address
    .byte W_REGISTER | FEATURE, _BV(EN_DPL) ; dynamic payloads
    .byte W_REGISTER | SETUP_RETR, 0x7F ; maximum retries at 4ms
    .byte R_REGISTER | RF_SETUP, 0 ; read back setup register to check radio is present   
radio_registers_end:
        
nrf24_begin:
    cbi	    CSN_PORT, CSN_PIN
    ldi	    ZL, lo8(SPI0_base)
    ldi	    ZH, hi8(SPI0_base)
    ldi	    r25, SPI_SSD_bm ; 0x04
    std	    Z + SPI_CTRLB_offset, r25
    ldi	    r25, SPI_ENABLE_bm | SPI_MASTER_bm | SPI_CLK2X_bm | SPI_PRESC_DIV4_gc ; 0x31
    std	    Z + SPI_CTRLA_offset, r25
spi_transfer:    
    std     Z + SPI_DATA_offset, r24
wait_spi:
    ldd	    r24, Z + SPI_INTFLAGS_offset
    sbrs    r24, SPI_RXCIF_bp
    rjmp    wait_spi
    ldd	    r24, Z + SPI_DATA_offset
return:
    ret  

nrf24_begin_rx:
    mov	    r22, r24
    ldi	    r24, W_REGISTER | EN_RXADDR
    rcall   nrf24_command
    ldi     r22, _BV(MASK_RX_DR) | _BV(MASK_TX_DS) | _BV(MASK_MAX_RT) | _BV(CRCO) | _BV(EN_CRC) | _BV(PWR_UP) | _BV(PRIM_RX)
    ldi	    r24, W_REGISTER | CONFIG    
nrf24_command:
    rcall   nrf24_begin
    mov	    r24, r22
nrf24_transfer_end:
    rcall   spi_transfer
nrf24_end:
    sbi	    CSN_PORT, CSN_PIN
    ret
        
nrf24_commands:
    movw    X, r24
    mov	    r20, r22
nrf24_commands_from_x:
    ld	    r24, X+
    ld	    r22, X+
    rcall   nrf24_command
    dec	    r20
    brne    nrf24_commands_from_x
    ret

nrf24_set_tx_address:
    ldi	    r24, W_REGISTER | TX_ADDR
    rcall   nrf24_set_address
    ldi	    r24, W_REGISTER | RX_ADDR_P0
nrf24_set_address:
    ldi	    r20, ADDRESS_WIDTH
nrf24_command_long: ; for W_TX_PAYLOAD etc..
    movw    X, r22
write_loop:
    rcall   nrf24_begin
    ld      r24, X+
    dec	    r20
    brne    write_loop
    rjmp    nrf24_transfer_end

nrf24_read_rx_payload:
    movw    X, r24
nrf24_read_rx_payload_to_x:
    ldi	    r24, R_RX_PL_WID
    rcall   nrf24_command
    mov	    r22, r24
    ldi	    r24, R_RX_PAYLOAD
    rcall   nrf24_begin
read_packet:
    rcall   spi_transfer
    st	    X+, r24
    dec	    r22
    brne    read_packet
    movw    r24, X
    rjmp    nrf24_end

start_bootloader:
    ; configure pin dirs and set CE & LED high
    ldi	    r16, PORTA_DIR_CFG
    out	    VPORTA_DIR, r16
.if PORTB_DIR_CFG != 0    
    ldi	    r17, PORTB_DIR_CFG
    out	    VPORTB_DIR, r17
.endif
    
    ; initialise radio registers
    ldi	    XL, lo8(radio_registers + MAPPED_PROGMEM_START)
    ldi	    XH, hi8(radio_registers + MAPPED_PROGMEM_START)
    ldi	    r20, (radio_registers_end - radio_registers) / 2
    rcall   nrf24_commands_from_x

    ; last command in radio_registers reads back the RF_SETUP register.
    ; here we verify it to check if radio is available and if not just run the app
    cpi	    r24, SETUP_VALUE
    brne    app

    ; set radio addresses from user signature area
    ;ldi     r22, lo8(USER_SIGNATURES_START) ; already 0 from radio check cmd
    ldi	    r23, hi8(USER_SIGNATURES_START)    
    rcall   nrf24_set_tx_address
    ldi	    r24, W_REGISTER | RX_ADDR_P1
    rcall   nrf24_set_address
    
    ; power up the radio in RX mode on pipe 5
    ldi	    r24, _BV(5)
    rcall   nrf24_begin_rx
    
    ; if reset was from watchdog then start the app
    lds	    r0, RSTCTRL_RSTFR
    sts	    RSTCTRL_RSTFR, r0
    sbrc    r0, RSTCTRL_WDRF_bp 
    rjmp    app

    ; Main programming loop
write_nvm:
    ; first time through this does nothing (r16 is wrong)
    ldi	    ZH, hi8(NVMCTRL_CTRLA) ; ZL = 0 from SPI0_base
    ldi     r24, NVMCTRL_CMD_PAGEERASEWRITE_gc
    out	    CPU_CCP, r16
    st	    Z, r24
wait_nvm:
    ldd	    r24, Z + (NVMCTRL_STATUS - NVMCTRL_CTRLA)
    andi    r24, NVMCTRL_FBUSY_bm|NVMCTRL_EEBUSY_bm
    brne    wait_nvm    
    
wait_for_command:
    ; turn on LED and radio CE
.if PORTA_DIR_CFG != (_BV(MOSI_PIN) | _BV(SCK_PIN))
    out     VPORTA_OUT, r16
.endif
.if PORTB_DIR_CFG != 0    
    out	    VPORTB_OUT, r17
.endif
    ldi     XH, hi8(INTERNAL_SRAM_START) ; packet buffer
read_page:
    ; save start address in Y
    movw    Y, X
wait_for_packet:
    rcall   nrf24_status
    subi    r24, 0xE 
    brhc    wait_for_packet ; low nybble less than 0xE?
    ; got a packet, reset watchdog
    wdr 	
#ifdef LED_PIN
    cbi	    LED_PORT, LED_PIN
#endif
    rcall   nrf24_read_rx_payload_to_x
    subi    r20, 1
    breq    write_nvm
    brcc    wait_for_packet
read_command:
    ld	    r16, Y ; CPU_CCP_SPM_gc to continue, anything else to end
    ldd	    r20, Y + 1 ; number of packets
    ldd	    XL, Y + 2 ; address to program
    ldd     XH, Y + 3
    cpi     r16, CPU_CCP_SPM_gc
    breq    read_page
        
    .org 0x100
app:    
#ifdef LED_PIN
    sbi	    LED_PORT, LED_PIN
#endif
    ; dummy app that just waits for an incoming reboot message
    wdr
    rcall nrf24_poll_reset
    rjmp app
    
.end

