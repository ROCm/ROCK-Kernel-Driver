/*
 *  linux/include/asm-arm/arch-ebsa285/io.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Modifications:
 *   06-12-1997	RMK	Created.
 *   07-04-1999	RMK	Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffff

/*
 * Translation of various region addresses to virtual addresses
 */
#define __io_pci(a)		(PCIO_BASE + (a))
#if 1
#define __mem_pci(a)		((unsigned long)(a))
#define __mem_isa(a)		(PCIMEM_BASE + (unsigned long)(a))
#else

extern __inline__ unsigned long ___mem_pci(unsigned long a)
{
	if (a <= 0xc0000000 || a >= 0xe0000000)
		BUG();
	return a;
}

extern __inline__ unsigned long ___mem_isa(unsigned long a)
{
	if (a >= 16*1048576)
		BUG();
	return PCIMEM_BASE + a;
}
#define __mem_pci(a)		___mem_pci((unsigned long)(a))
#define __mem_isa(a)		___mem_isa((unsigned long)(a))
#endif

/* the following macro is depreciated */
#define __ioaddr(p)		__io_pci(p)

/*
 * Generic virtual read/write
 */
#define __arch_getb(a)		(*(volatile unsigned char *)(a))
#define __arch_getl(a)		(*(volatile unsigned int  *)(a))

extern __inline__ unsigned int __arch_getw(unsigned long a)
{
	unsigned int value;
	__asm__ __volatile__("ldr%?h	%0, [%1, #0]	@ getw"
		: "=&r" (value)
		: "r" (a));
	return value;
}


#define __arch_putb(v,a)	(*(volatile unsigned char *)(a) = (v))
#define __arch_putl(v,a)	(*(volatile unsigned int  *)(a) = (v))

extern __inline__ void __arch_putw(unsigned int value, unsigned long a)
{
	__asm__ __volatile__("str%?h	%0, [%1, #0]	@ putw"
		: : "r" (value), "r" (a));
}

#define inb(p)			__arch_getb(__io_pci(p))
#define inw(p)			__arch_getw(__io_pci(p))
#define inl(p)			__arch_getl(__io_pci(p))

#define outb(v,p)		__arch_putb(v,__io_pci(p))
#define outw(v,p)		__arch_putw(v,__io_pci(p))
#define outl(v,p)		__arch_putl(v,__io_pci(p))

#include <asm/hardware/dec21285.h>

/*
 * ioremap support - validate a PCI memory address,
 * and convert a PCI memory address to a physical
 * address for the page tables.
 */
#define valid_ioaddr(off,sz)	((off) < 0x80000000 && (off) + (sz) <= 0x80000000)
#define io_to_phys(off)		((off) + DC21285_PCI_MEM)

/*
 * ioremap takes a PCI memory address, as specified in
 * linux/Documentation/IO-mapping.txt
 */
#define __arch_ioremap(off,size,nocache)			\
({								\
	unsigned long _off = (off), _size = (size);		\
	void *_ret = (void *)0;					\
	if (valid_ioaddr(_off, _size))				\
		_ret = __ioremap(io_to_phys(_off), _size, 0);	\
	_ret;							\
})

#endif
