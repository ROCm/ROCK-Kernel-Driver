/*
 * linux/include/asm-arm/arch-omap/io.h
 *
 * Copied from linux/include/asm-arm/arch-sa1100/io.h
 * Copyright (C) 1997-1999 Russell King
 *
 * Modifications:
 *  06-12-1997	RMK	Created.
 *  07-04-1999	RMK	Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)			(PCIO_BASE + (a))
#define __mem_pci(a)		((unsigned long)(a))
#define __mem_isa(a)		((unsigned long)(a))

/*
 * Functions to access the OMAP IO region
 *
 * NOTE: - Use omap_read/write[bwl] for physical register addresses
 *	 - Use __raw_read/write[bwl]() for virtual register addresses
 *	 - Use IO_ADDRESS(phys_addr) to convert registers to virtual addresses
 *	 - DO NOT use hardcoded virtual addresses to allow changing the
 *	   IO address space again if needed
 */
#define omap_readb(a)		(*(volatile unsigned char  *)IO_ADDRESS(a))
#define omap_readw(a)		(*(volatile unsigned short *)IO_ADDRESS(a))
#define omap_readl(a)		(*(volatile unsigned int   *)IO_ADDRESS(a))

#define omap_writeb(v,a)	(*(volatile unsigned char  *)IO_ADDRESS(a) = (v))
#define omap_writew(v,a)	(*(volatile unsigned short *)IO_ADDRESS(a) = (v))
#define omap_writel(v,a)	(*(volatile unsigned int   *)IO_ADDRESS(a) = (v))

#endif
