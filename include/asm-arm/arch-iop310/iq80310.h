/*
 * linux/include/asm/arch-iop80310/iq80310.h
 *
 * Intel IQ-80310 evaluation board registers
 */

#ifndef _IQ80310_H_
#define _IQ80310_H_

#define IQ80310_RAMBASE      0xa0000000
#define IQ80310_UART1        0xfe800000    /* UART #1 */
#define IQ80310_UART2        0xfe810000    /* UART #2 */
#define IQ80310_INT_STAT     0xfe820000    /* Interrupt (XINT3#) Status */
#define IQ80310_BOARD_REV    0xfe830000    /* Board revision register */
#define IQ80310_CPLD_REV     0xfe840000    /* CPLD revision register */
#define IQ80310_7SEG_1       0xfe840000    /* 7-Segment MSB */
#define IQ80310_7SEG_0       0xfe850000    /* 7-Segment LSB (WO) */
#define IQ80310_PCI_INT_STAT 0xfe850000    /* PCI Interrupt  Status */
#define IQ80310_INT_MASK     0xfe860000    /* Interrupt (XINT3#) Mask */
#define IQ80310_BACKPLANE    0xfe870000    /* Backplane Detect */
#define IQ80310_TIMER_LA0    0xfe880000    /* Timer LA0 */
#define IQ80310_TIMER_LA1    0xfe890000    /* Timer LA1 */
#define IQ80310_TIMER_LA2    0xfe8a0000    /* Timer LA2 */
#define IQ80310_TIMER_LA3    0xfe8b0000    /* Timer LA3 */
#define IQ80310_TIMER_EN     0xfe8c0000    /* Timer Enable */
#define IQ80310_ROTARY_SW    0xfe8d0000    /* Rotary Switch */
#define IQ80310_JTAG         0xfe8e0000    /* JTAG Port Access */
#define IQ80310_BATT_STAT    0xfe8f0000    /* Battery Status */

#endif	// _IQ80310_H_
