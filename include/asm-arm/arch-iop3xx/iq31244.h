/*
 * linux/include/asm/arch-iop3xx/iq31244.h
 *
 * Intel IQ31244 evaluation board registers
 */

#ifndef _IQ31244_H_
#define _IQ31244_H_

#define IQ31244_RAMBASE		0xa0000000

#define	IQ31244_FLASHBASE	0xf0000000	/* Flash */
#define	IQ31244_FLASHSIZE	0x00800000
#define	IQ31244_FLASHWIDTH	2

#define IQ31244_UART		0xfe800000	/* UART #1 */
#define IQ31244_7SEG_1		0xfe840000	/* 7-Segment MSB */
#define IQ31244_7SEG_0		0xfe850000	/* 7-Segment LSB (WO) */
#define IQ31244_ROTARY_SW	0xfe8d0000	/* Rotary Switch */
#define IQ31244_BATT_STAT	0xfe8f0000	/* Battery Status */

/*
 * IQ31244 PCI I/O and Mem space regions
 */
#define IQ31244_PCI_IO_BASE	0x90000000
#define IQ31244_PCI_IO_SIZE	0x00010000
#define IQ31244_PCI_MEM_BASE	0x80000000
//#define IQ31244_PCI_MEM_SIZE	0x04000000
#define IQ31244_PCI_MEM_SIZE	0x08000000
#define	IQ31244_PCI_IO_OFFSET	0x6e000000

#ifndef __ASSEMBLY__
extern void iq31244_map_io(void);
#endif

#endif	// _IQ31244_H_
