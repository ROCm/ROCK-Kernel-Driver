/*
 * Mitsubishi M32R 32102 group
 * Typical I/O routines.
 *
 * Copyright (c) 2001 Hitoshi Yamamoto
 */

/* $Id$ */

#include <linux/config.h>
#include <asm/page.h>

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
#include <linux/types.h>

#define M32R_PCC_IOMAP_SIZE 0x1000

#define M32R_PCC_IOSTART0 0x1000
#define M32R_PCC_IOEND0   (M32R_PCC_IOSTART0 + M32R_PCC_IOMAP_SIZE - 1)
#define M32R_PCC_IOSTART1 0x2000
#define M32R_PCC_IOEND1   (M32R_PCC_IOSTART1 + M32R_PCC_IOMAP_SIZE - 1)

extern void pcc_ioread(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite(int, unsigned long, void *, size_t, size_t, int);
#endif /* CONFIG_PCMCIA && CONFIG_M32RPCC */


/*
 * Function prototypes
 */
unsigned char  ne_inb(unsigned long);
void  ne_outb(unsigned char, unsigned long);
void  ne_insb(unsigned int, void *, unsigned long);
void  ne_insw(unsigned int, void *, unsigned long);
void  ne_outsb(unsigned int, const void *, unsigned long);
void  ne_outsw(unsigned int, const void *, unsigned long);

#define PORT2ADDR(port)  m32102_port2addr(port)

static __inline__ unsigned long
m32102_port2addr(unsigned long port)
{
	unsigned long  ul;
	ul = port + PAGE_OFFSET + 0x20000000;
	return (ul);
}

unsigned char
m32102_inb(unsigned long port)
{
#ifdef CONFIG_PLAT_MAPPI
	if(port >= 0x300 && port < 0x320)
		return ne_inb(port);
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned char b;
	   pcc_ioread(0, port, &b, sizeof(b), 1, 0);
	   return b;
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	  unsigned char b;
	   pcc_ioread(1, port, &b, sizeof(b), 1, 0);
	   return b;
	} else
#endif
#endif /*  CONFIG_PLAT_MAPPI  */
	return *(unsigned char *)PORT2ADDR(port);
}

unsigned short
m32102_inw(unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned short w;
	   pcc_ioread(0, port, &w, sizeof(w), 1, 0);
	   return w;
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   unsigned short w;
	   pcc_ioread(1, port, &w, sizeof(w), 1, 0);
	   return w;
	} else
#endif
	return *(unsigned short *)PORT2ADDR(port);
}

unsigned long
m32102_inl(unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned long l;
	   pcc_ioread(0, port, &l, sizeof(l), 1, 0);
	   return l;
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   unsigned short l;
	   pcc_ioread(1, port, &l, sizeof(l), 1, 0);
	   return l;
	} else
#endif
	return *(unsigned long *)PORT2ADDR(port);
}

void
m32102_outb(unsigned char b, unsigned long port)
{
#ifdef CONFIG_PLAT_MAPPI
	if(port >= 0x300 && port < 0x320)
		ne_outb(b,port);
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite(0, port, &b, sizeof(b), 1, 0);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_iowrite(1, port, &b, sizeof(b), 1, 0);
	} else
#endif
#endif /*  CONFIG_PLAT_MAPPI  */
	*(unsigned volatile char *)PORT2ADDR(port) = b;
}

void
m32102_outw(unsigned short w, unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite(0, port, &w, sizeof(w), 1, 0);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_iowrite(1, port, &w, sizeof(w), 1, 0);
	} else
#endif
*(unsigned volatile short *)PORT2ADDR(port) = w;
}

void
m32102_outl(unsigned long l, unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite(0, port, &l, sizeof(l), 1, 0);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_iowrite(1, port, &l, sizeof(l), 1, 0);
	} else
#endif
	*(unsigned volatile long *)PORT2ADDR(port) = l;
}

void
m32102_insb(unsigned int port, void * addr, unsigned long count)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_ioread(0, port, (void *)addr, sizeof(unsigned char), count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_ioread(1, port, (void *)addr, sizeof(unsigned char), count, 1);
	} else
#endif
	while(count--){
		*(unsigned char *)addr = *(unsigned volatile char *)PORT2ADDR(port);
		addr+=1;
	}
}

void
m32102_insw(unsigned int port, void * addr, unsigned long count)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_ioread(0, port, (void *)addr, sizeof(unsigned short), count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_ioread(1, port, (void *)addr, sizeof(unsigned short), count, 1);
	} else
#endif
while(count--){
		*(unsigned short *)addr = *(unsigned volatile short *)PORT2ADDR(port);
		addr+=2;
	}
}

void
m32102_outsb(unsigned int port, const void * addr, unsigned long count)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite(0, port, (void *)addr, sizeof(unsigned char), count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_iowrite(1, port, (void *)addr, sizeof(unsigned char), count, 1);
	} else
#endif
while(count--){
		*(unsigned volatile char *)PORT2ADDR(port) = *(unsigned char *)addr;
		addr+=1;
	}
}
void
m32102_outsw(unsigned int port, const void * addr, unsigned long count)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32RPCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite(0, port, (void *)addr, sizeof(unsigned short), count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
	   pcc_iowrite(1, port, (void *)addr, sizeof(unsigned short), count, 1);
	} else
#endif
while(count--){
		*(unsigned volatile short *)PORT2ADDR(port) = *(unsigned short *)addr;
		addr+=2;
	}
}

#ifdef CONFIG_PLAT_MAPPI
unsigned char
ne_inb(unsigned long port)
{
	unsigned short tmp;
	port <<= 1;
	port+= PAGE_OFFSET + 0x20000000 + 0x0c000000;
	tmp = *(unsigned short *)port;
	return (unsigned char)tmp;
}
void
ne_outb(unsigned char b, unsigned long port)
{
	port <<= 1;
	port += PAGE_OFFSET + 0x20000000 + 0x0c000000;
	*(unsigned volatile short *)port = (unsigned short)b;
}
void
ne_insb(unsigned int port, void * addr, unsigned long count)
{

	unsigned short tmp;
	port <<= 1;
	port+= PAGE_OFFSET + 0x20000000 + 0x0c000000;
	tmp = *(unsigned short *)port;
	while(count--){
		*(unsigned char *)addr = *(unsigned volatile char *)port;
		addr+=1;
	}
}

void
ne_insw(unsigned int port, void * addr, unsigned long count) {
	unsigned short tmp;
	port <<= 1;
	port+= PAGE_OFFSET + 0x20000000 + 0x0c000000;
	while(count--){
		tmp = *(unsigned volatile short *)port;
		*(unsigned short *)addr = (tmp>>8) | (tmp <<8);
		addr+=2;
	}
}

void
ne_outsb(unsigned int port, const void * addr, unsigned long count)
{
	port <<= 1;
	port += PAGE_OFFSET + 0x20000000 + 0x0c000000;
	while(count--){
		*(unsigned volatile short *)port = *(unsigned char *)addr;
		addr+=1;
	}
}
void
ne_outsw(unsigned int port, const void * addr, unsigned long count)
{
	unsigned short tmp;
	port <<= 1;
	port += PAGE_OFFSET + 0x20000000 + 0x0c000000;
	while(count--){
		tmp = *(unsigned short *)addr;
		*(unsigned volatile short *)port = (tmp>>8)|(tmp<<8);
		addr+=2;
	}
}

#endif /*  CONFIG_PLAT_MAPPI  */
