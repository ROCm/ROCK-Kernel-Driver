/*
 * linux/include/asm-arm/arch-tbox/io.h
 *
 * Copyright (C) 1996-1999 Russell King
 * Copyright (C) 1998, 1999 Philip Blundell
 *
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

#define __io_pc(_x)	((_x) << 2)

/*
 * Generic virtual read/write
 */
#define __arch_getb(a)		(*(volatile unsigned char *)(a))
#define __arch_getl(a)		(*(volatile unsigned long *)(a))

extern __inline__ unsigned int __arch_getw(unsigned long a)
{
	unsigned int value;
	__asm__ __volatile__("ldr%?h	%0, [%1, #0]	@ getw"
		: "=&r" (value)
		: "r" (a));
	return value;
}


#define __arch_putb(v,a)	(*(volatile unsigned char *)(a) = (v))
#define __arch_putl(v,a)	(*(volatile unsigned long *)(a) = (v))

extern __inline__ void __arch_putw(unsigned int value, unsigned long a)
{
	__asm__ __volatile__("str%?h	%0, [%1, #0]	@ putw"
		: : "r" (value), "r" (a));
}

#define inb(p)			__arch_getb(__io_pc(p))
#define inw(p)			__arch_getw(__io_pc(p))
#define inl(p)			__arch_getl(__io_pc(p))

#define outb(v,p)		__arch_putb(v,__io_pc(p))
#define outw(v,p)		__arch_putw(v,__io_pc(p))
#define outl(v,p)		__arch_putl(v,__io_pc(p))

/* Idem, for devices on the upper byte lanes */
#define inb_u(p)		__arch_getb(__io_pc(p) + 2)
#define inw_u(p)		__arch_getw(__io_pc(p) + 2)

#define outb_u(v,p)		__arch_putb(v,__io_pc(p) + 2)
#define outw_u(v,p)		__arch_putw(v,__io_pc(p) + 2)

#endif
