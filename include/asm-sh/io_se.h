/*
 * include/asm-sh/io_se.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an Hitachi SolutionEngine
 */

#ifndef _ASM_SH_IO_SE_H
#define _ASM_SH_IO_SE_H

#include <asm/io_generic.h>

extern unsigned long se_inb(unsigned int port);
extern unsigned long se_inw(unsigned int port);
extern unsigned long se_inl(unsigned int port);

extern void se_outb(unsigned long value, unsigned int port);
extern void se_outw(unsigned long value, unsigned int port);
extern void se_outl(unsigned long value, unsigned int port);

extern unsigned long se_inb_p(unsigned int port);
extern void se_outb_p(unsigned long value, unsigned int port);

extern void se_insb(unsigned int port, void *addr, unsigned long count);
extern void se_insw(unsigned int port, void *addr, unsigned long count);
extern void se_insl(unsigned int port, void *addr, unsigned long count);
extern void se_outsb(unsigned int port, const void *addr, unsigned long count);
extern void se_outsw(unsigned int port, const void *addr, unsigned long count);
extern void se_outsl(unsigned int port, const void *addr, unsigned long count);

extern unsigned long se_readb(unsigned long addr);
extern unsigned long se_readw(unsigned long addr);
extern unsigned long se_readl(unsigned long addr);
extern void se_writeb(unsigned char b, unsigned long addr);
extern void se_writew(unsigned short b, unsigned long addr);
extern void se_writel(unsigned int b, unsigned long addr);

extern unsigned long se_isa_port2addr(unsigned long offset);

#ifdef __WANT_IO_DEF

# define __inb			se_inb
# define __inw			se_inw
# define __inl			se_inl
# define __outb			se_outb
# define __outw			se_outw
# define __outl			se_outl

# define __inb_p		se_inb_p
# define __inw_p		se_inw
# define __inl_p		se_inl
# define __outb_p		se_outb_p
# define __outw_p		se_outw
# define __outl_p		se_outl

# define __insb			se_insb
# define __insw			se_insw
# define __insl			se_insl
# define __outsb		se_outsb
# define __outsw		se_outsw
# define __outsl		se_outsl

# define __readb		se_readb
# define __readw		se_readw
# define __readl		se_readl
# define __writeb		se_writeb
# define __writew		se_writew
# define __writel		se_writel

# define __isa_port2addr	se_isa_port2addr
# define __ioremap		generic_ioremap
# define __ioremap_nocache	generic_ioremap_nocache
# define __iounmap		generic_iounmap

#endif

#endif /* _ASM_SH_IO_SE_H */
