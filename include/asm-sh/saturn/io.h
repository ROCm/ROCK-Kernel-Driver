/*
 * include/asm-sh/saturn/io.h
 *
 * I/O functions for use on the Sega Saturn.
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#ifndef __ASM_SH_SATURN_IO_H
#define __ASM_SH_SATURN_IO_H

#include <asm/io_generic.h>

/* arch/sh/boards/saturn/io.c */
extern unsigned long saturn_isa_port2addr(unsigned long offset);
extern void *saturn_ioremap(unsigned long offset, unsigned long size);
extern void saturn_iounmap(void *addr);

#ifdef __WANT_IO_DEF

# define __inb			generic_inb
# define __inw			generic_inw
# define __inl			generic_inl
# define __outb			generic_outb
# define __outw			generic_outw
# define __outl			generic_outl

# define __inb_p		generic_inb_p
# define __inw_p		generic_inw
# define __inl_p		generic_inl
# define __outb_p		generic_outb_p
# define __outw_p		generic_outw
# define __outl_p		generic_outl

# define __insb			generic_insb
# define __insw			generic_insw
# define __insl			generic_insl
# define __outsb		generic_outsb
# define __outsw		generic_outsw
# define __outsl		generic_outsl

# define __readb		generic_readb
# define __readw		generic_readw
# define __readl		generic_readl
# define __writeb		generic_writeb
# define __writew		generic_writew
# define __writel		generic_writel

# define __isa_port2addr	saturn_isa_port2addr
# define __ioremap		saturn_ioremap
# define __iounmap		saturn_iounmap

#endif

#endif /* __ASM_SH_SATURN_IO_H */

