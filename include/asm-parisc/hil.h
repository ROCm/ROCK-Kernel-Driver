#ifndef _ASM_HIL_H
#define _ASM_HIL_H

/*
 *	linux/asm-parisc/hil.h
 *
 *	(c) 1999 Matthew Wilcox
 */

extern unsigned long hil_base;	/* declared in drivers/gsc/hil.c */
extern unsigned int hil_irq;

#define HILBASE			hil_base /* 0xf0821000 (old) or 0xf0201000 (new) */
#define HIL_DATA		0x800
#define HIL_CMD			0x801

#define HIL_IRQ			hil_irq

#define hil_busy()		(gsc_readb(HILBASE + HIL_CMD) & HIL_BUSY)
#define hil_data_available()	(gsc_readb(HILBASE + HIL_CMD) & HIL_DATA_RDY)
#define hil_status()		(gsc_readb(HILBASE + HIL_CMD))
#define hil_command(x)		do { gsc_writeb((x), HILBASE + HIL_CMD); } while (0)
#define hil_read_data()		(gsc_readb(HILBASE + HIL_DATA))
#define hil_write_data(x)	do { gsc_writeb((x), HILBASE + HIL_DATA); } while (0)

#endif
