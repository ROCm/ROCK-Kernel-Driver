/*
 * linux/include/asm/arch-iop3xx/iq80321.h
 *
 * Intel IQ-80321 evaluation board registers
 */

#ifndef _IQ80321_H_
#define _IQ80321_H_

#define IQ80321_RAMBASE      0xa0000000
#define IQ80321_UART1        0xfe800000    /* UART #1 */
#define IQ80321_7SEG_1       0xfe840000    /* 7-Segment MSB */
#define IQ80321_7SEG_0       0xfe850000    /* 7-Segment LSB (WO) */
#define IQ80321_ROTARY_SW    0xfe8d0000    /* Rotary Switch */
#define IQ80321_BATT_STAT    0xfe8f0000    /* Battery Status */

#endif	// _IQ80321_H_
