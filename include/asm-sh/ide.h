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

#define IDE_ARCH_OBSOLETE_DEFAULTS

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

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#ifdef CONFIG_PCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SH_IDE_H */
