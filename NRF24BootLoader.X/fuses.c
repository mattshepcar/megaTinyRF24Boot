#include <avr/io.h>

const uint8_t userrow[] __attribute__ ((section (".user_signatures"))) = 
{
    '0', '0', '1', // address
    50 // channel
};

FUSES = {
	.WDTCFG = 0x08,			/* Watchdog Configuration */
	.BODCFG = 0x00,			/* BOD Configuration */
	.OSCCFG = 0x02,			/* Oscillator Configuration */
	.reserved_1 = 0xFF,
	.TCD0CFG = 0x00,
	.SYSCFG0 = 0xC4,		/* System Configuration 0 */
	.SYSCFG1 = 0x04,		/* System Configuration 1 */
	.APPEND = 0x00,			/* Application Code Section End */
	.BOOTEND = 0x01			/* Boot Section End */
};
