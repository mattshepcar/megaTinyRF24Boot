;
; NRF24BootLoaderAsm.asm
;
; Created: 15/03/2020 14:50:27
; Author : Matt Shepcar
;
        
#include <avr/io.h>
#include "../include/nRF24L01.h"
#include "registers.h"

; radio settings
#define SETUP_VALUE (_BV(RF_PWR_LOW) | _BV(RF_PWR_HIGH) | _BV(RF_DR_HIGH)) 
#define ADDRESS_WIDTH 3

; PIN settings
#define CSN_PORT VPORTB_OUT
#define CSN_PIN 0
#define LED_PORT VPORTB_OUT
; Set to 4 for alternate SPI pins:
#define PORTMUXB_CFG 0
; define which 2 ports you are using:
#define VPORT1_DIR VPORTA_DIR
#define VPORT1_OUT VPORTA_OUT
#define VPORT2_DIR VPORTB_DIR
#define VPORT2_OUT VPORTB_OUT
; undefine LED_PIN if no led
#define LED_PIN 1
#define MOSI_PIN 1
#define SCK_PIN 3
#define CE_PIN 0
; which pins are on which ports:
#define PORT1_DIR_CFG _BV(MOSI_PIN) | _BV(SCK_PIN)
#define PORT2_DIR_CFG _BV(CSN_PIN) | _BV(CE_PIN) | _BV(LED_PIN)

// struct rx_return {uint8_t* packetend; uint8_t packetsize;};
// rx_return nrf24_read_rx_payload(uint8_t* dstbuf);
#define RETURN_PAYLOAD_WIDTH 1
    
    .text
    .org 0
    .global main

main:
    rjmp    start_bootloader

nrf24_status:
    ; vectors for the application to use to access the radio functions
    ldi	    r24, STATUS_NRF
    rjmp    nrf24_command
    rjmp    nrf24_command_long
    rjmp    nrf24_begin_rx
    rjmp    nrf24_begin_tx
    rjmp    nrf24_read_rx_payload
nrf24_poll_reset:    
    ; reset into bootloader when a programming packet is received
    wdr
    rcall   nrf24_status
    andi    r24, 0xE
    subi    r24, 0xA - CPU_CCP_IOREG_gc ; CCP value only when packet in pipe 5
    out	    CPU_CCP, r24
    sts	    RSTCTRL_SWRR, r25
    ret
       
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
return:
    ret  

nrf24_begin_tx:
    ldi     r21, _BV(MASK_RX_DR) | _BV(MASK_TX_DS) | _BV(MASK_MAX_RT) | _BV(CRCO) | _BV(EN_CRC) | _BV(PWR_UP)
    ldi	    r22, 1
    rjmp    nrf24_begin_rxtx
nrf24_begin_rx:
    ldi     r21, _BV(MASK_RX_DR) | _BV(MASK_TX_DS) | _BV(MASK_MAX_RT) | _BV(CRCO) | _BV(EN_CRC) | _BV(PWR_UP) | _BV(PRIM_RX)
    mov	    r22, r24
nrf24_begin_rxtx:
    ldi	    r24, W_REGISTER | EN_RXADDR
    rcall   nrf24_command
nrf24_set_config_r21:
    mov	    r22, r21
    ldi	    r24, W_REGISTER | CONFIG
nrf24_command:
    rcall   nrf24_begin
    mov	    r24, r22
nrf24_transfer_end:
    rcall   spi_transfer
nrf24_end:
    sbi	    CSN_PORT, CSN_PIN
    ret
           
nrf24_read_rx_payload:
    movw    X, r24
nrf24_read_rx_payload_to_x:
    ldi	    r24, R_RX_PL_WID
    rcall   nrf24_command
#if RETURN_PAYLOAD_WIDTH
    mov	    r22, r24
#endif
    mov	    r23, r24
    ldi	    r24, R_RX_PAYLOAD
    rcall   nrf24_begin
read_packet:
    rcall   spi_transfer
    st	    X+, r24
    dec	    r23
    brne    read_packet
    movw    r24, X
    rjmp    nrf24_end
        
radio_registers:
    .byte W_REGISTER | CONFIG, 0 ; power down 
    .byte W_REGISTER | EN_AA, 0x3F ; auto acknowledge on all pipes
    .byte W_REGISTER | SETUP_AW, (ADDRESS_WIDTH - 2)
    .byte W_REGISTER | SETUP_RETR, 0x7F ; maximum retries at 4ms
    .byte W_REGISTER | RF_SETUP, SETUP_VALUE ; power & data rate
    .byte W_REGISTER | DYNPD, 0x3F ; dynamic payload on all pipes  
    .byte W_REGISTER | RX_ADDR_P5, 'P' ; programming pipe address
    .byte W_REGISTER | FEATURE, _BV(EN_DPL)|_BV(EN_ACK_PAY)|_BV(EN_DYN_ACK) ; dynamic payloads
    .byte R_REGISTER | RF_SETUP, 0 ; read back setup register to check radio is present   
    .org 128 ; the code relies on this ending at exactly 0x8080
radio_registers_end:   
    
crcadjust:
    ; this value is patched to ensure the CRC of the bootloader is 0xFFFF
    .byte 0xCC, 0xCC
    
nrf24_set_tx_address:
    ldi	    r24, W_REGISTER | TX_ADDR
    rcall   nrf24_set_address
    ldi	    r24, W_REGISTER | RX_ADDR_P0
nrf24_set_address:
    ldi	    r20, ADDRESS_WIDTH
nrf24_command_long: ; for W_TX_PAYLOAD etc..
    movw    X, r22
write_loop:
    subi    r20, 1
    brlt    nrf24_transfer_end
nrf24_command_data_x:
    rcall   nrf24_begin
    ld      r24, X+
    rjmp    write_loop
    
start_bootloader:
    ldi	    YL, lo8(radio_registers + MAPPED_PROGMEM_START)
    ldi	    YH, hi8(radio_registers + MAPPED_PROGMEM_START)

    ; configure pins
    ldi	    r16, PORT1_DIR_CFG
    ldi	    r17, PORT2_DIR_CFG
    ldi     r18, PORTMUXB_CFG
    sts     PORTMUX_CTRLB, r18
    out	    VPORT1_DIR, r16
    out	    VPORT2_DIR, r17    
   
init_radio:
    ld	    r24, Y+
    ld	    r22, Y+
    rcall   nrf24_command
    cpi	    YL, lo8(radio_registers_end)
    brne    init_radio

    ; last command in radio_registers reads back the RF_SETUP register.
    ; here we verify it to check if radio is available and if not just run the app
    cpi	    r24, SETUP_VALUE
    brne    app

    ; set radio addresses and channel from user signature area
    ;ldi     r22, lo8(USER_SIGNATURES_START) ; already 0 from radio check cmd
    ldi	    r23, hi8(USER_SIGNATURES_START)    
    rcall   nrf24_set_tx_address
    ldi	    r24, W_REGISTER | RX_ADDR_P1
    rcall   nrf24_set_address

start_bootloader_custom_channel:
    ; set radio channel from next byte of user signature area
    ldi	    r24, W_REGISTER | RF_CH
    rcall   nrf24_command_data_x
    
    ; power up the radio in RX mode on pipe 5
    ldi	    r24, _BV(5)
    rcall   nrf24_begin_rx
        
    ; if reset was from watchdog then start the app
    ldi	    ZH, hi8(RSTCTRL_RSTFR)
    ldd	    r0, Z + RSTCTRL_RSTFR - 3
    std	    Z + RSTCTRL_RSTFR - 3, r0
    sbrc    r0, RSTCTRL_WDRF_bp 
    rjmp    app

    ; command packet buffer at 0x3F80 (safe on all chips)
    ldi     YH, 0x3F
    ;ldi     YL, 0x80  ; already 0x80 from radio_registers_end
    
    ; Main programming loop
write_nvm:
    ; first time through this does nothing because r21 is wrong
    out	    CPU_CCP, r21
    sts	    NVMCTRL_CTRLA, ZL ; ZL = 3
    
    ; send a byte back
    ldi	    r24, W_ACK_PAYLOAD + 5
    rcall   nrf24_command_data_x
wait_for_command:
    ; turn on LED and radio CE
    out     VPORT1_OUT, r16
    out	    VPORT2_OUT, r17
    ; read next packet to command buffer
    movw    X, Y
read_page:
wait_for_packet:
    rcall   nrf24_status
    subi    r24, 0xE
    brhc    wait_for_packet ; low nybble less than 0xE?
    ; got a packet, reset watchdog
    wdr
#ifdef LED_PIN
    cbi     LED_PORT, LED_PIN
#else
    nop
#endif
    rcall   nrf24_read_rx_payload_to_x
    subi    r20, 1
    breq    write_nvm
    brge    wait_for_packet
read_command:
    ld	    r21, Y ; CPU_CCP_SPM_gc to continue, anything else to end
    ldd	    r20, Y + 1 ; number of packets
    ldd	    XL, Y + 2 ; address to program
    ldd     XH, Y + 3
    cpi     r21, CPU_CCP_SPM_gc
    breq    read_page
  
    .org 0x100
app:    
    ;sbrc r0, RSTCTRL_WDRF_bp 
    ;rjmp wait_for_command
    ;rcall nrf24_set_config_r21
    ;rjmp start_bootloader_custom_channel  
    
    ; dummy app that just waits for an incoming reboot message
#ifdef LED_PIN
    sbi	    LED_PORT, LED_PIN
#endif
    rcall   nrf24_poll_reset
    rjmp    app
 
.end
