/*
 *  linux/include/asm-ppc/ide.h
 *
 *  Copyright (C) 1994-1996 Linus Torvalds & authors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */ 

/*
 *  This file contains the ppc64 architecture specific IDE code.
 */

#ifndef __ASMPPC64_IDE_H
#define __ASMPPC64_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#define ide__sti()	__sti()

void ppc64_ide_fix_driveid(struct hd_driveid *id);
#define ide_fix_driveid(id)	ppc64_ide_fix_driveid((id))

static __inline__ int ide_default_irq(ide_ioreg_t base) { return 0; }
static __inline__ ide_ioreg_t ide_default_io_base(int index) { return 0; }

static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = hw->io_ports[IDE_DATA_OFFSET] + 0x206;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}

static __inline__ void ide_init_default_hwifs(void)
{
}

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned head		: 4;	/* always zeros here */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
	} b;
} select_t;
	
/* XXX is this correct? - Anton */
typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned HOB		: 1;	/* 48-bit address ordering */
		unsigned reserved456	: 3;
		unsigned bit3		: 1;	/* ATA-2 thingy */
		unsigned SRST		: 1;	/* host soft reset bit */
		unsigned nIEN		: 1;	/* device INTRQ to host */
		unsigned bit0		: 1;
	} b;
} control_t;

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

#endif /* __KERNEL__ */

#endif /* __ASMPPC64_IDE_H */
