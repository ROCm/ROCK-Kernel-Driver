/* $Id: io_generic.c,v 1.3 2000/05/07 23:31:58 gniibe Exp $
 *
 * linux/arch/sh/kernel/io_generic.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * Generic I/O routine. These can be used where a machine specific version
 * is not required.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/io.h>
#include <asm/machvec.h>

#if defined(__sh3__)
/* I'm not sure SH7709 has this kind of bug */
#define SH3_PCMCIA_BUG_WORKAROUND 1
#define DUMMY_READ_AREA6	  0xba000000
#endif

#define PORT2ADDR(x) (sh_mv.mv_isa_port2addr(x))

unsigned long generic_io_base;

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

unsigned long generic_inb(unsigned int port)
{
	return *(volatile unsigned char*)PORT2ADDR(port);
}

unsigned long generic_inw(unsigned int port)
{
	return *(volatile unsigned short*)PORT2ADDR(port);
}

unsigned long generic_inl(unsigned int port)
{
	return *(volatile unsigned long*)PORT2ADDR(port);
}

unsigned long generic_inb_p(unsigned int port)
{
	unsigned long v = *(volatile unsigned char*)PORT2ADDR(port);

	delay();
	return v;
}

unsigned long generic_inw_p(unsigned int port)
{
	unsigned long v = *(volatile unsigned short*)PORT2ADDR(port);

	delay();
	return v;
}

unsigned long generic_inl_p(unsigned int port)
{
	unsigned long v = *(volatile unsigned long*)PORT2ADDR(port);

	delay();
	return v;
}

void generic_insb(unsigned int port, void *buffer, unsigned long count)
{
	unsigned char *buf=buffer;
	while(count--) *buf++=inb(port);
}

void generic_insw(unsigned int port, void *buffer, unsigned long count)
{
	unsigned short *buf=buffer;
	while(count--) *buf++=inw(port);
#ifdef SH3_PCMCIA_BUG_WORKAROUND
	ctrl_inb (DUMMY_READ_AREA6);
#endif
}

void generic_insl(unsigned int port, void *buffer, unsigned long count)
{
	unsigned long *buf=buffer;
	while(count--) *buf++=inl(port);
#ifdef SH3_PCMCIA_BUG_WORKAROUND
	ctrl_inb (DUMMY_READ_AREA6);
#endif
}

void generic_outb(unsigned long b, unsigned int port)
{
	*(volatile unsigned char*)PORT2ADDR(port) = b;
}

void generic_outw(unsigned long b, unsigned int port)
{
	*(volatile unsigned short*)PORT2ADDR(port) = b;
}

void generic_outl(unsigned long b, unsigned int port)
{
        *(volatile unsigned long*)PORT2ADDR(port) = b;
}

void generic_outb_p(unsigned long b, unsigned int port)
{
	*(volatile unsigned char*)PORT2ADDR(port) = b;
	delay();
}

void generic_outw_p(unsigned long b, unsigned int port)
{
	*(volatile unsigned short*)PORT2ADDR(port) = b;
	delay();
}

void generic_outl_p(unsigned long b, unsigned int port)
{
	*(volatile unsigned long*)PORT2ADDR(port) = b;
	delay();
}

void generic_outsb(unsigned int port, const void *buffer, unsigned long count)
{
	const unsigned char *buf=buffer;
	while(count--) outb(*buf++, port);
}

void generic_outsw(unsigned int port, const void *buffer, unsigned long count)
{
	const unsigned short *buf=buffer;
	while(count--) outw(*buf++, port);
#ifdef SH3_PCMCIA_BUG_WORKAROUND
	ctrl_inb (DUMMY_READ_AREA6);
#endif
}

void generic_outsl(unsigned int port, const void *buffer, unsigned long count)
{
	const unsigned long *buf=buffer;
	while(count--) outl(*buf++, port);
#ifdef SH3_PCMCIA_BUG_WORKAROUND
	ctrl_inb (DUMMY_READ_AREA6);
#endif
}

unsigned long generic_readb(unsigned long addr)
{
	return *(volatile unsigned char*)addr;
}

unsigned long generic_readw(unsigned long addr)
{
	return *(volatile unsigned short*)addr;
}

unsigned long generic_readl(unsigned long addr)
{
	return *(volatile unsigned long*)addr;
}

void generic_writeb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char*)addr = b;
}

void generic_writew(unsigned short b, unsigned long addr)
{
	*(volatile unsigned short*)addr = b;
}

void generic_writel(unsigned int b, unsigned long addr)
{
        *(volatile unsigned long*)addr = b;
}

void * generic_ioremap(unsigned long offset, unsigned long size)
{
	return (void *) P2SEGADDR(offset);
}

void * generic_ioremap_nocache (unsigned long offset, unsigned long size)
{
	return (void *) P2SEGADDR(offset);
}

void generic_iounmap(void *addr)
{
}

unsigned long generic_isa_port2addr(unsigned long offset)
{
	return offset + generic_io_base;
}
