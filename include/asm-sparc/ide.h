/* $Id: ide.h,v 1.7 2002/01/16 20:58:40 davem Exp $
 * ide.h: SPARC PCI specific IDE glue.
 *
 * Copyright (C) 1997  David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998  Eddie C. Dost   (ecd@skynet.be)
 * Adaptation from sparc64 version to sparc by Pete Zaitcev.
 */

#ifndef _SPARC_IDE_H
#define _SPARC_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/hdreg.h>
#include <asm/psr.h>

#undef  MAX_HWIFS
#define MAX_HWIFS	2

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	return 0;
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	return 0;
}

/*
 * Doing any sort of ioremap() here does not work
 * because this function may be called with null aguments.
 */
static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg =  data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = 0;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_PCI
	hw_regs_t hw;
	int index;

	for (index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif
}

#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))
#define ide_check_region(from,extent)		check_region((from), (extent))
#define ide_request_region(from,extent,name)	request_region((from), (extent), (name))
#define ide_release_region(from,extent)		release_region((from), (extent))

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

/* XXX Known to be broken.  Axboe will fix the problems this
 * XXX has by making seperate IN/OUT macros for IDE_DATA
 * XXX register and rest of IDE regs and also using
 * XXX ide_ioreg_t instead of u32 for ports. -DaveM
 */

#define HAVE_ARCH_IN_BYTE
#define IN_BYTE(p)		(*((volatile u8 *)(p)))
#define IN_WORD(p)		(*((volatile u16 *)(p)))
#define IN_LONG(p)		(*((volatile u32 *)(p)))
#define IN_BYTE_P		IN_BYTE
#define IN_WORD_P		IN_WORD
#define IN_LONG_P		IN_LONG

#define HAVE_ARCH_OUT_BYTE
#define OUT_BYTE(b,p)		((*((volatile u8 *)(p))) = (b))
#define OUT_WORD(w,p)		((*((volatile u16 *)(p))) = (w))
#define OUT_LONG(l,p)		((*((volatile u32 *)(p))) = (l))
#define OUT_BYTE_P		OUT_BYTE
#define OUT_WORD_P		OUT_WORD
#define OUT_LONG_P		OUT_LONG

#endif /* __KERNEL__ */

#endif /* _SPARC_IDE_H */
