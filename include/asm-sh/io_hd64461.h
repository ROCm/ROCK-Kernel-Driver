/*
 * include/asm-sh/io_hd64461.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an HD64461
 */

#ifndef _ASM_SH_IO_HD64461_H
#define _ASM_SH_IO_HD64461_H

#include <asm/io_generic.h>

extern unsigned long hd64461_inb(unsigned int port);
extern unsigned long hd64461_inw(unsigned int port);
extern unsigned long hd64461_inl(unsigned int port);

extern void hd64461_outb(unsigned long value, unsigned int port);
extern void hd64461_outw(unsigned long value, unsigned int port);
extern void hd64461_outl(unsigned long value, unsigned int port);

extern unsigned long hd64461_inb_p(unsigned int port);
extern void hd64461_outb_p(unsigned long value, unsigned int port);

extern void hd64461_insb(unsigned int port, void *addr, unsigned long count);
extern void hd64461_insw(unsigned int port, void *addr, unsigned long count);
extern void hd64461_insl(unsigned int port, void *addr, unsigned long count);
extern void hd64461_outsb(unsigned int port, const void *addr, unsigned long count);
extern void hd64461_outsw(unsigned int port, const void *addr, unsigned long count);
extern void hd64461_outsl(unsigned int port, const void *addr, unsigned long count);

#ifdef __WANT_IO_DEF

# define __inb			hd64461_inb
# define __inw			hd64461_inw
# define __inl			hd64461_inl
# define __outb			hd64461_outb
# define __outw			hd64461_outw
# define __outl			hd64461_outl

# define __inb_p		hd64461_inb_p
# define __inw_p		hd64461_inw
# define __inl_p		hd64461_inl
# define __outb_p		hd64461_outb_p
# define __outw_p		hd64461_outw
# define __outl_p		hd64461_outl

# define __insb			hd64461_insb
# define __insw			hd64461_insw
# define __insl			hd64461_insl
# define __outsb		hd64461_outsb
# define __outsw		hd64461_outsw
# define __outsl		hd64461_outsl

# define __readb		generic_readb
# define __readw		generic_readw
# define __readl		generic_readl
# define __writeb		generic_writeb
# define __writew		generic_writew
# define __writel		generic_writel

# define __isa_port2addr	generic_isa_port2addr
# define __ioremap		generic_ioremap
# define __iounmap		generic_iounmap

#endif

#endif /* _ASM_SH_IO_HD64461_H */
