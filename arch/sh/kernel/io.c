/*
 * linux/arch/sh/kernel/io.c
 *
 * Copyright (C) 2000  Stuart Menefy
 *
 * Provide real functions which expand to whatever the header file defined.
 * Also definitions of machine independant IO functions.
 */

#include <asm/io.h>

unsigned int _inb(unsigned long port)
{
	return __inb(port);
}

unsigned int _inw(unsigned long port)
{
	return __inw(port);
}

unsigned int _inl(unsigned long port)
{
	return __inl(port);
}

void _outb(unsigned char b, unsigned long port)
{
	__outb(b, port);
}

void _outw(unsigned short b, unsigned long port)
{
	__outw(b, port);
}

void _outl(unsigned int b, unsigned long port)
{
	__outl(b, port);
}

unsigned int _inb_p(unsigned long port)
{
	return __inb_p(port);
}

unsigned int _inw_p(unsigned long port)
{
	return __inw_p(port);
}

void _outb_p(unsigned char b, unsigned long port)
{
	__outb_p(b, port);
}

void _outw_p(unsigned short b, unsigned long port)
{
	__outw_p(b, port);
}

void _insb(unsigned long port, void *buffer, unsigned long count)
{
	return __insb(port, buffer, count);
}

void _insw(unsigned long port, void *buffer, unsigned long count)
{
	__insw(port, buffer, count);
}

void _insl(unsigned long port, void *buffer, unsigned long count)
{
	__insl(port, buffer, count);
}

void _outsb(unsigned long port, const void *buffer, unsigned long count)
{
	__outsb(port, buffer, count);
}

void _outsw(unsigned long port, const void *buffer, unsigned long count)
{
	__outsw(port, buffer, count);
}

void _outsl(unsigned long port, const void *buffer, unsigned long count)
{
	__outsl(port, buffer, count);

}

unsigned long ___raw_readb(unsigned long addr)
{
	return __readb(addr);
}

unsigned long ___raw_readw(unsigned long addr)
{
	return __readw(addr);
}

unsigned long ___raw_readl(unsigned long addr)
{
	return __readl(addr);
}

unsigned long _readb(unsigned long addr)
{
	unsigned long r = __readb(addr);
	mb();
	return r;
}

unsigned long _readw(unsigned long addr)
{
	unsigned long r = __readw(addr);
	mb();
	return r;
}

unsigned long _readl(unsigned long addr)
{
	unsigned long r = __readl(addr);
	mb();
	return r;
}

void ___raw_writeb(unsigned char b, unsigned long addr)
{
	__writeb(b, addr);
}

void ___raw_writew(unsigned short b, unsigned long addr)
{
	__writew(b, addr);
}

void ___raw_writel(unsigned int b, unsigned long addr)
{
	__writel(b, addr);
}

void _writeb(unsigned char b, unsigned long addr)
{
	__writeb(b, addr);
	mb();
}

void _writew(unsigned short b, unsigned long addr)
{
	__writew(b, addr);
	mb();
}

void _writel(unsigned int b, unsigned long addr)
{
	__writel(b, addr);
	mb();
}

/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void  memcpy_fromio(void * to, unsigned long from, unsigned long count)
{
        while (count) {
                count--;
                *(char *) to = readb(from);
                ((char *) to)++;
                from++;
        }
}
 
/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void  memcpy_toio(unsigned long to, const void * from, unsigned long count)
{
        while (count) {
                count--;
                writeb(*(char *) from, to);
                ((char *) from)++;
                to++;
        }
}
 
/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void  memset_io(unsigned long dst, int c, unsigned long count)
{
        while (count) {
                count--;
                writeb(c, dst);
                dst++;
        }
}
