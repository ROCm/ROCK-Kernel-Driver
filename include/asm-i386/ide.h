/*
 *  linux/include/asm-i386/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMi386_IDE_H
#define __ASMi386_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifndef MAX_HWIFS
# ifdef CONFIG_BLK_DEV_IDEPCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	6
# endif
#endif

#define IDE_ARCH_OBSOLETE_DEFAULTS

static __inline__ int ide_default_irq(unsigned long base)
{
	switch (base) {
#ifdef CONFIG_X86_PC9800
		case 0x640: return 9;
#endif
		case 0x1f0: return 14;
		case 0x170: return 15;
		case 0x1e8: return 11;
		case 0x168: return 10;
		case 0x1e0: return 8;
		case 0x160: return 12;
		default:
			return 0;
	}
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	switch (index) {
#ifdef CONFIG_X86_PC9800
		case 0:
		case 1:	return 0x640;
#else
		case 0:	return 0x1f0;
		case 1:	return 0x170;
		case 2: return 0x1e8;
		case 3: return 0x168;
		case 4: return 0x1e0;
		case 5: return 0x160;
#endif
		default:
			return 0;
	}
}

#ifdef CONFIG_X86_PC9800
static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
	 unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i;

	unsigned long increment = data_port == 0x640 ? 2 : 1;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += increment;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else if (data_port == 0x640) {
		hw->io_ports[IDE_CONTROL_OFFSET] = 0x74c;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = hw->io_ports[IDE_DATA_OFFSET] + 0x206;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}
#endif

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#ifdef CONFIG_BLK_DEV_IDEPCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASMi386_IDE_H */
