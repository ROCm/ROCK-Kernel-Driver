/*
 * include/asm-sh/io_mpc1211.h
 *
 * Copyright 2001 Saito.K & Jeanne
 *
 * IO functions for an Interface MPC-1211
 */

#ifndef _ASM_SH_IO_MPC1211_H
#define _ASM_SH_IO_MPC1211_H

#include <linux/time.h>
#include <asm/io_generic.h>

extern unsigned char mpc1211_inb(unsigned long port);
extern unsigned short mpc1211_inw(unsigned long port);
extern unsigned int mpc1211_inl(unsigned long port);

extern void mpc1211_outb(unsigned char value, unsigned long port);
extern void mpc1211_outw(unsigned short value, unsigned long port);
extern void mpc1211_outl(unsigned int value, unsigned long port);

extern unsigned char mpc1211_inb_p(unsigned long port);
extern void mpc1211_outb_p(unsigned char value, unsigned long port);

extern void mpc1211_insb(unsigned long port, void *addr, unsigned long count);
extern void mpc1211_insw(unsigned long port, void *addr, unsigned long count);
extern void mpc1211_insl(unsigned long port, void *addr, unsigned long count);
extern void mpc1211_outsb(unsigned long port, const void *addr, unsigned long count);
extern void mpc1211_outsw(unsigned long port, const void *addr, unsigned long count);
extern void mpc1211_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned char mpc1211_readb(unsigned long addr);
extern unsigned short mpc1211_readw(unsigned long addr);
extern unsigned int mpc1211_readl(unsigned long addr);
extern void mpc1211_writeb(unsigned char b, unsigned long addr);
extern void mpc1211_writew(unsigned short b, unsigned long addr);
extern void mpc1211_writel(unsigned int b, unsigned long addr);

extern unsigned long mpc1211_isa_port2addr(unsigned long offset);
extern int mpc1211_irq_demux(int irq);

extern void setup_mpc1211(void);
extern void init_mpc1211_IRQ(void);
extern void heartbeat_mpc1211(void);

extern void mpc1211_rtc_gettimeofday(struct timeval *tv);
extern int mpc1211_rtc_settimeofday(const struct timeval *tv);

#ifdef __WANT_IO_DEF

# define __inb			mpc1211_inb
# define __inw			mpc1211_inw
# define __inl			mpc1211_inl
# define __outb			mpc1211_outb
# define __outw			mpc1211_outw
# define __outl			mpc1211_outl

# define __inb_p		mpc1211_inb_p
# define __inw_p		mpc1211_inw
# define __inl_p		mpc1211_inl
# define __outb_p		mpc1211_outb_p
# define __outw_p		mpc1211_outw
# define __outl_p		mpc1211_outl

# define __insb			mpc1211_insb
# define __insw			mpc1211_insw
# define __insl			mpc1211_insl
# define __outsb		mpc1211_outsb
# define __outsw		mpc1211_outsw
# define __outsl		mpc1211_outsl

# define __readb		mpc1211_readb
# define __readw		mpc1211_readw
# define __readl		mpc1211_readl
# define __writeb		mpc1211_writeb
# define __writew		mpc1211_writew
# define __writel		mpc1211_writel

# define __isa_port2addr	mpc1211_isa_port2addr
# define __ioremap		generic_ioremap
# define __iounmap		generic_iounmap

#endif

#endif /* _ASM_SH_IO_MPC1211_H */
