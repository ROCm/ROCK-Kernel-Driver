/*
 * linux/arch/sh/kernel/io_mpc1211.c
 *
 * Copyright (C) 2001  Saito.K & Jeanne
 *
 * I/O routine for Interface MPC-1211.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/mpc1211/pci.h>

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

static inline unsigned long port2adr(unsigned long port)
{
        return port + PA_PCI_IO;
}

unsigned char mpc1211_inb(unsigned long port)
{
        return *(__u8 *)port2adr(port);
}

unsigned short mpc1211_inw(unsigned long port)
{
        return *(__u16 *)port2adr(port);
}

unsigned int mpc1211_inl(unsigned long port)
{
        return *(__u32 *)port2adr(port);
}

void mpc1211_outb(unsigned char value, unsigned long port)
{
	*(__u8 *)port2adr(port) = value; 
}

void mpc1211_outw(unsigned short value, unsigned long port)
{
	*(__u16 *)port2adr(port) = value; 
}

void mpc1211_outl(unsigned int value, unsigned long port)
{
	*(__u32 *)port2adr(port) = value; 
}

unsigned char mpc1211_inb_p(unsigned long port)
{
	unsigned char v;

	v = *(__u8 *)port2adr(port);
	delay();
	return v;
}

void mpc1211_outb_p(unsigned char value, unsigned long port)
{
	*(__u8 *)port2adr(port) = value; 
	delay();
}

void mpc1211_insb(unsigned long port, void *addr, unsigned long count)
{
	volatile __u8 *p = (__u8 *)port2adr(port);

	while (count--) {
	        *((__u8 *)addr)++ = *p;
	}
}

void mpc1211_insw(unsigned long port, void *addr, unsigned long count)
{
	volatile __u16 *p = (__u16 *)port2adr(port);

	while (count--) {
	        *((__u16 *)addr)++ = *p;
	}
}

void mpc1211_insl(unsigned long port, void *addr, unsigned long count)
{
	volatile __u32 *p = (__u32 *)port2adr(port);

	while (count--) {
	        *((__u32 *)addr)++ = *p;
	}
}

void mpc1211_outsb(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u8 *p = (__u8 *)port2adr(port);

	while (count--) {
	        *p = *((__u8 *)addr)++;
	}
}

void mpc1211_outsw(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u16 *p = (__u16 *)port2adr(port);

	while (count--) {
	        *p = *((__u16 *)addr)++;
	}
}

void mpc1211_outsl(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u32 *p = (__u32 *)port2adr(port);

	while (count--) {
	        *p = *((__u32 *)addr)++;
	}
}

unsigned char mpc1211_readb(unsigned long addr)
{
	return *(volatile unsigned char *)addr;
}

unsigned short mpc1211_readw(unsigned long addr)
{
	return *(volatile unsigned short *)addr;
}

unsigned int mpc1211_readl(unsigned long addr)
{
	return *(volatile unsigned int *)addr;
}

void mpc1211_writeb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char *)addr = b;
}

void mpc1211_writew(unsigned short b, unsigned long addr)
{
	*(volatile unsigned short *)addr = b;
}

void mpc1211_writel(unsigned int b, unsigned long addr)
{
        *(volatile unsigned int *)addr = b;
}

unsigned long mpc1211_isa_port2addr(unsigned long offset)
{
	return port2adr(offset);
}
