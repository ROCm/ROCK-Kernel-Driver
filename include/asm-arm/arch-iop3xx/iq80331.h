/*
 * linux/include/asm/arch-iop3xx/iq80331.h
 *
 * Intel IQ80331 evaluation board registers
 */

#ifndef _IQ80331_H_
#define _IQ80331_H_

#define IQ80331_RAMBASE		0x00000000

#define	IQ80331_FLASHBASE	0xc0000000	/* Flash */
#define	IQ80331_FLASHSIZE	0x00800000
#define	IQ80331_FLASHWIDTH	1

#define IQ80331_UART0_PHYS  (IOP331_PHYS_MEM_BASE | 0x00001700)	/* UART #1 physical */
#define IQ80331_UART1_PHYS  (IOP331_PHYS_MEM_BASE | 0x00001740)	/* UART #2 physical */
#define IQ80331_UART0_VIRT  (IOP331_VIRT_MEM_BASE | 0x00001700) /* UART #1 virtual addr */
#define IQ80331_UART1_VIRT  (IOP331_VIRT_MEM_BASE | 0x00001740) /* UART #2 virtual addr */
#define IQ80331_7SEG_1		0xce840000	/* 7-Segment MSB */
#define IQ80331_7SEG_0		0xce850000	/* 7-Segment LSB (WO) */
#define IQ80331_ROTARY_SW	0xce8d0000	/* Rotary Switch */
#define IQ80331_BATT_STAT	0xce8f0000	/* Battery Status */

/*
 * IQ80331 PCI I/O and Mem space regions
 */
#define IQ80331_PCI_IO_BASE	0x90000000
#define IQ80331_PCI_IO_SIZE	0x00010000
#define IQ80331_PCI_MEM_BASE	0x80000000
#define IQ80331_PCI_MEM_SIZE	0x08000000
#define	IQ80331_PCI_IO_OFFSET	0x6e000000

#ifndef __ASSEMBLY__
extern void iq80331_map_io(void);
#endif

#endif	// _IQ80331_H_
