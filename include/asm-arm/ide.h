/*
 *  linux/include/asm-arm/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMARM_IDE_H
#define __ASMARM_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#if defined(CONFIG_ARCH_LH7A40X) || defined(CONFIG_ARCH_SA1100)
# include <asm/arch/ide.h>	/* obsolete + broken */
#endif

/*
 * We always use the new IDE port registering,
 * so these are fixed here.
 */
#define ide_default_io_base(i)		(0)
#define ide_default_irq(b)		(0)

#if !defined(CONFIG_ARCH_L7200) && !defined(CONFIG_ARCH_LH7A40X)
# define IDE_ARCH_OBSOLETE_INIT
# ifdef CONFIG_ARCH_CLPS7500
#  define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */
# else
#  define ide_default_io_ctl(base)	(0)
# endif
#endif /* !ARCH_L7200 && !ARCH_LH7A40X */

#define ide_init_default_irq(base)	(0)

#define __ide_mm_insw(port,addr,len)	readsw(port,addr,len)
#define __ide_mm_insl(port,addr,len)	readsl(port,addr,len)
#define __ide_mm_outsw(port,addr,len)	writesw(port,addr,len)
#define __ide_mm_outsl(port,addr,len)	writesl(port,addr,len)

#endif /* __KERNEL__ */

#endif /* __ASMARM_IDE_H */
