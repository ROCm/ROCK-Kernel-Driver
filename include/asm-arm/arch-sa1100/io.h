/*
 * linux/include/asm-arm/arch-sa1100/io.h
 *
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
#define __io_pci(a)		(PCIO_BASE + (a))
#define __mem_pci(a)		((unsigned long)(a))
#define __mem_isa(a)		((unsigned long)(a))

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

#define __arch_ioremap		__ioremap

#endif
