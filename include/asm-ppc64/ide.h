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
# define MAX_HWIFS	4
#endif

static inline int ide_default_irq(unsigned long base) { return 0; }
static inline unsigned long ide_default_io_base(int index) { return 0; }

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#define ide_init_default_irq(base)	(0)

#endif /* __KERNEL__ */

#endif /* __ASMPPC64_IDE_H */
