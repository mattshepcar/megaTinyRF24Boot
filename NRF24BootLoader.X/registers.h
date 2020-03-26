#ifndef REGISTERS_H
#define	REGISTERS_H

#define SPI0_base 0x801 
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

#endif	/* REGISTERS_H */

