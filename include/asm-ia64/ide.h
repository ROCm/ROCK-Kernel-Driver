/*
 *  linux/include/asm-ia64/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the ia64 architecture specific IDE code.
 */

#ifndef __ASM_IA64_IDE_H
#define __ASM_IA64_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#include <linux/irq.h>

#ifndef MAX_HWIFS
# ifdef CONFIG_PCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	6
# endif
#endif

static inline int ide_default_irq(unsigned long base)
{
	switch (base) {
	      case 0x1f0: return isa_irq_to_vector(14);
	      case 0x170: return isa_irq_to_vector(15);
	      case 0x1e8: return isa_irq_to_vector(11);
	      case 0x168: return isa_irq_to_vector(10);
	      case 0x1e0: return isa_irq_to_vector(8);
	      case 0x160: return isa_irq_to_vector(12);
	      default:
		return 0;
	}
}

static inline unsigned long ide_default_io_base(int index)
{
	switch (index) {
	      case 0: return 0x1f0;
	      case 1: return 0x170;
	      case 2: return 0x1e8;
	      case 3: return 0x168;
	      case 4: return 0x1e0;
	      case 5: return 0x160;
	      default:
		return 0;
	}
}

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

#ifdef CONFIG_PCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASM_IA64_IDE_H */
