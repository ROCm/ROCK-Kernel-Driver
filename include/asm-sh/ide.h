/*
 *  linux/include/asm-sh/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 *  In future, SuperH code.
 */

#ifndef __ASM_SH_IDE_H
#define __ASM_SH_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/machvec.h>

#ifndef MAX_HWIFS
/* Should never have less than 2, ide-pci.c(ide_match_hwif) requires it */
#define MAX_HWIFS	2
#endif

static inline int ide_default_irq_hp600(unsigned long base)
{
	switch (base) {
		case 0x01f0: return 93;
		case 0x0170: return 94;
		default:
			return 0;
	}
}

static inline int ide_default_irq(unsigned long base)
{
	if (MACH_HP600) {
		return ide_default_irq_hp600(base);
	}
	switch (base) {
		case 0x01f0: return 14;
		case 0x0170: return 15;
		default:
			return 0;
	}
}

static inline unsigned long ide_default_io_base_hp600(int index)
{
	switch (index) {
		case 0:	
			return 0x01f0;
		case 1:	
			return 0x0170;
		default:
			return 0;
	}
}

static inline unsigned long ide_default_io_base(int index)
{
	if (MACH_HP600) {
		return ide_default_io_base_hp600(index);
	}
	switch (index) {
		case 0:	
			return 0x1f0;
		case 1:	
			return 0x170;
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

static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_PCI
	hw_regs_t hw;
	int index;

	for(index = 0; index < MAX_HWIFS; index++) {
		memset(&hw, 0, sizeof hw);
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif /* CONFIG_PCI */
}

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SH_IDE_H */
