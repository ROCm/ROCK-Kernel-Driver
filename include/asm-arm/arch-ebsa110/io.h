/*
 *  linux/include/asm-arm/arch-ebsa110/io.h
 *
 *  Copyright (C) 1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *  06-Dec-1997	RMK	Created.
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffff

u8  __inb(int port);
u16 __inw(int port);
u32 __inl(int port);

#define inb(p)			__inb(p)
#define inw(p)			__inw(p)
#define inl(p)			__inl(p)

void __outb(u8  val, int port);
void __outw(u16 val, int port);
void __outl(u32 val, int port);

#define outb(v,p)		__outb(v,p)
#define outw(v,p)		__outw(v,p)
#define outl(v,p)		__outl(v,p)

u8  __readb(void *addr);
u16 __readw(void *addr);
u32 __readl(void *addr);

#define readb(b)		__readb(b)
#define readw(b)		__readw(b)
#define readl(b)		__readl(b)

void __writeb(u8  val, void *addr);
void __writew(u16 val, void *addr);
void __writel(u32 val, void *addr);

#define writeb(v,b)		__writeb(v,b)
#define writew(v,b)		__writew(v,b)
#define writel(v,b)		__writel(v,b)

#define __arch_ioremap(cookie,sz,c)	((void *)(cookie))
#define __arch_iounmap(cookie)		do { } while (0)

#endif
