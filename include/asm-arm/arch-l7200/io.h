/*
 * linux/include/asm-arm/arch-l7200/io.h
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Modifications:
 *  03-21-2000	SJH	Created from linux/include/asm-arm/arch-nexuspci/io.h
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/arch/hardware.h>

#define IO_SPACE_LIMIT 0xffffffff

/*
 * Translated address IO functions
 *
 * IO address has already been translated to a virtual address
 */
#define outb_t(v,p)	(*(volatile unsigned char *)(p) = (v))
#define inb_t(p)	(*(volatile unsigned char *)(p))
#define outw_t(v,p)	(*(volatile unsigned int *)(p) = (v))
#define inw_t(p)	(*(volatile unsigned int *)(p))
#define outl_t(v,p)	(*(volatile unsigned long *)(p) = (v))
#define inl_t(p)	(*(volatile unsigned long *)(p))

/*
 * FIXME - These are to allow for linking. On all the other
 *         ARM platforms, the entire IO space is contiguous.
 *         The 7200 has three separate IO spaces. The below
 *         macros will eventually become more involved. Use
 *         with caution and don't be surprised by kernel oopses!!!
 */
#define inb(p)	 	inb_t(p)
#define inw(p)	 	inw_t(p)
#define inl(p)	 	inl_t(p)
#define outb(v,p)	outb_t(v,p)
#define outw(v,p)	outw_t(v,p)
#define outl(v,p)	outl_t(v,p)

#endif
