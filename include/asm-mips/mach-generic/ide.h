/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * IDE routines for typical pc-like legacy IDE configurations.
 *
 * Copyright (C) 1998, 1999, 2001, 2003 by Ralf Baechle
 */
#ifndef __ASM_MACH_GENERIC_IDE_H
#define __ASM_MACH_GENERIC_IDE_H

#include <linux/config.h>

#ifndef MAX_HWIFS
# ifdef CONFIG_BLK_DEV_IDEPCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	6
# endif
#endif

static inline int ide_default_irq(unsigned long base)
{
	switch (base) {
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

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#ifdef CONFIG_BLK_DEV_IDEPCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

#endif /* __ASM_MACH_GENERIC_IDE_H */
