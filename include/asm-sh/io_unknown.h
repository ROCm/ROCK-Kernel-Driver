/*
 * include/asm-sh/io_unknown.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for use when we don't know what machine we are on
 */

#ifndef _ASM_SH_IO_UNKNOWN_H
#define _ASM_SH_IO_UNKNOWN_H

extern unsigned long unknown_inb(unsigned int port);
extern unsigned long unknown_inw(unsigned int port);
extern unsigned long unknown_inl(unsigned int port);

extern void unknown_outb(unsigned long value, unsigned int port);
extern void unknown_outw(unsigned long value, unsigned int port);
extern void unknown_outl(unsigned long value, unsigned int port);

extern unsigned long unknown_inb_p(unsigned int port);
extern unsigned long unknown_inw_p(unsigned int port);
extern unsigned long unknown_inl_p(unsigned int port);
extern void unknown_outb_p(unsigned long value, unsigned int port);
extern void unknown_outw_p(unsigned long value, unsigned int port);
extern void unknown_outl_p(unsigned long value, unsigned int port);

extern void unknown_insb(unsigned int port, void *addr, unsigned long count);
extern void unknown_insw(unsigned int port, void *addr, unsigned long count);
extern void unknown_insl(unsigned int port, void *addr, unsigned long count);
extern void unknown_outsb(unsigned int port, const void *addr, unsigned long count);
extern void unknown_outsw(unsigned int port, const void *addr, unsigned long count);
extern void unknown_outsl(unsigned int port, const void *addr, unsigned long count);

extern unsigned long unknown_readb(unsigned long addr);
extern unsigned long unknown_readw(unsigned long addr);
extern unsigned long unknown_readl(unsigned long addr);
extern void unknown_writeb(unsigned char b, unsigned long addr);
extern void unknown_writew(unsigned short b, unsigned long addr);
extern void unknown_writel(unsigned int b, unsigned long addr);

extern unsigned long unknown_isa_port2addr(unsigned long offset);
extern void * unknown_ioremap(unsigned long offset, unsigned long size);
extern void * unknown_ioremap_nocache (unsigned long offset, unsigned long size);
extern void unknown_iounmap(void *addr);

#ifdef __WANT_IO_DEF

# define __inb			unknown_inb
# define __inw			unknown_inw
# define __inl			unknown_inl
# define __outb			unknown_outb
# define __outw			unknown_outw
# define __outl			unknown_outl

# define __inb_p		unknown_inb_p
# define __inw_p		unknown_inw_p
# define __inl_p		unknown_inl_p
# define __outb_p		unknown_outb_p
# define __outw_p		unknown_outw_p
# define __outl_p		unknown_outl_p

# define __insb			unknown_insb
# define __insw			unknown_insw
# define __insl			unknown_insl
# define __outsb		unknown_outsb
# define __outsw		unknown_outsw
# define __outsl		unknown_outsl

# define __readb		unknown_readb
# define __readw		unknown_readw
# define __readl		unknown_readl
# define __writeb		unknown_writeb
# define __writew		unknown_writew
# define __writel		unknown_writel

# define __isa_port2addr	unknown_isa_port2addr
# define __ioremap		unknown_ioremap
# define __ioremap_nocache	unknown_ioremap_nocache
# define __iounmap		unknown_iounmap

#endif

#endif /* _ASM_SH_IO_UNKNOWN_H */
