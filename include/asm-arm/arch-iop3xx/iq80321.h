/*
 * linux/include/asm/arch-iop3xx/iq80321.h
 *
 * Intel IQ80321 evaluation board registers
 */

#ifndef _IQ80321_H_
#define _IQ80321_H_

#define IQ80321_RAMBASE		0xa0000000

#define	IQ80321_FLASHBASE	0xf0000000	/* Flash */
#define	IQ80321_FLASHSIZE	0x00800000
#define	IQ80321_FLASHWIDTH	1

#define IQ80321_UART		0xfe800000	/* UART #1 */
#define IQ80321_7SEG_1		0xfe840000	/* 7-Segment MSB */
#define IQ80321_7SEG_0		0xfe850000	/* 7-Segment LSB (WO) */
#define IQ80321_ROTARY_SW	0xfe8d0000	/* Rotary Switch */
#define IQ80321_BATT_STAT	0xfe8f0000	/* Battery Status */

/*
 * IQ80321 PCI I/O and Mem space regions
 */
#define IQ80321_PCI_IO_BASE	0x90000000
#define IQ80321_PCI_IO_SIZE	0x00010000
#define IQ80321_PCI_MEM_BASE	0x80000000
#define IQ80321_PCI_MEM_SIZE	0x04000000
#define	IQ80321_PCI_IO_OFFSET	0x6e000000

#ifndef __ASSEMBLY__
extern void iq80321_map_io(void);
#endif

#endif	// _IQ80321_H_
