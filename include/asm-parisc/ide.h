/*
 *  linux/include/asm-parisc/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the PARISC architecture specific IDE code.
 */

#ifndef __ASM_PARISC_IDE_H
#define __ASM_PARISC_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifndef MAX_HWIFS
#define MAX_HWIFS	2
#endif

#define ide_default_irq(base) (0)
#define ide_default_io_base(index) (0)

static inline void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
				       unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
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

#define ide_init_default_irq(base)	(0)

#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))
#define ide_check_region(from,extent)		check_region((from), (extent))
#define ide_request_region(from,extent,name)	request_region((from), (extent), (name))
#define ide_release_region(from,extent)		release_region((from), (extent))
/* Generic I/O and MEMIO string operations.  */

#define __ide_insw	insw
#define __ide_insl	insl
#define __ide_outsw	outsw
#define __ide_outsl	outsl

static __inline__ void __ide_mm_insw(unsigned long port, void *addr, u32 count)
{
	while (count--) {
		*(u16 *)addr = __raw_readw(port);
		addr += 2;
	}
}

static __inline__ void __ide_mm_insl(unsigned long port, void *addr, u32 count)
{
	while (count--) {
		*(u32 *)addr = __raw_readl(port);
		addr += 4;
	}
}

static __inline__ void __ide_mm_outsw(unsigned long port, void *addr, u32 count)
{
	while (count--) {
		__raw_writew(*(u16 *)addr, port);
		addr += 2;
	}
}

static __inline__ void __ide_mm_outsl(unsigned long port, void *addr, u32 count)
{
	while (count--) {
		__raw_writel(*(u32 *)addr, port);
		addr += 4;
	}
}

#endif /* __KERNEL__ */

#endif /* __ASM_PARISC_IDE_H */
