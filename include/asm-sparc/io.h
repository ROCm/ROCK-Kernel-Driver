/*
 * $Id: io.h,v 1.30 2001/12/21 01:23:21 davem Exp $
 */
#ifndef __SPARC_IO_H
#define __SPARC_IO_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>  /* struct resource */

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>

#define page_to_phys(page)	((page - mem_map) << PAGE_SHIFT)

static inline u32 flip_dword (u32 d)
{
	return ((d&0xff)<<24) | (((d>>8)&0xff)<<16) | (((d>>16)&0xff)<<8)| ((d>>24)&0xff);
}

static inline u16 flip_word (u16 d)
{
	return ((d&0xff) << 8) | ((d>>8)&0xff);
}

/*
 * Memory mapped I/O to PCI
 *
 * Observe that ioremap returns void* cookie, but accessors, such
 * as readb, take unsigned long as address, by API. This mismatch
 * happened historically. The ioremap is much older than accessors,
 * so at one time ioremap's cookie was used as address (*a = val).
 * When accessors came about, they were designed to be compatible across
 * buses, so that drivers can select proper ones like sunhme.c did.
 * To make that easier, they use same aruments (ulong) for sbus, pci, isa.
 * The offshot is, we must cast readb et. al. arguments with a #define.
 */

static inline u8 __raw_readb(unsigned long addr)
{
	return *(volatile u8 *)addr;
}

static inline u16 __raw_readw(unsigned long addr)
{
	return *(volatile u16 *)addr;
}

static inline u32 __raw_readl(unsigned long addr)
{
	return *(volatile u32 *)addr;
}

static inline void __raw_writeb(u8 b, unsigned long addr)
{
	*(volatile u8 *)addr = b;
}

static inline void __raw_writew(u16 b, unsigned long addr)
{
	*(volatile u16 *)addr = b;
}

static inline void __raw_writel(u32 b, unsigned long addr)
{
	*(volatile u32 *)addr = b;
}

static inline u8 __readb(unsigned long addr)
{
	return *(volatile u8 *)addr;
}

static inline u16 __readw(unsigned long addr)
{
	return flip_word(*(volatile u16 *)addr);
}

static inline u32 __readl(unsigned long addr)
{
	return flip_dword(*(volatile u32 *)addr);
}

static inline void __writeb(u8 b, unsigned long addr)
{
	*(volatile u8 *)addr = b;
}

static inline void __writew(u16 b, unsigned long addr)
{
	*(volatile u16 *)addr = flip_word(b);
}

static inline void __writel(u32 b, unsigned long addr)
{
	*(volatile u32 *)addr = flip_dword(b);
}

#define readb(addr)	__readb((unsigned long)(addr))
#define readw(addr)	__readw((unsigned long)(addr))
#define readl(addr)	__readl((unsigned long)(addr))

#define writeb(b, addr)	__writeb((b),(unsigned long)(addr))
#define writew(b, addr)	__writew((b),(unsigned long)(addr))
#define writel(b, addr)	__writel((b),(unsigned long)(addr))

/*
 * I/O space operations
 *
 * Arrangement on a Sun is somewhat complicated.
 *
 * First of all, we want to use standard Linux drivers
 * for keyboard, PC serial, etc. These drivers think
 * they access I/O space and use inb/outb.
 * On the other hand, EBus bridge accepts PCI *memory*
 * cycles and converts them into ISA *I/O* cycles.
 * Ergo, we want inb & outb to generate PCI memory cycles.
 *
 * If we want to issue PCI *I/O* cycles, we do this
 * with a low 64K fixed window in PCIC. This window gets
 * mapped somewhere into virtual kernel space and we
 * can use inb/outb again.
 */
#define inb_local(addr)		__readb(addr)
#define inb(addr)		__readb(addr)
#define inw(addr)		__readw(addr)
#define inl(addr)		__readl(addr)

#define outb_local(b, addr)	__writeb(b, addr)
#define outb(b, addr)		__writeb(b, addr)
#define outw(b, addr)		__writew(b, addr)
#define outl(b, addr)		__writel(b, addr)

#define inb_p inb
#define outb_p outb
#define inw_p inw
#define outw_p outw
#define inl_p inl
#define outl_p outl

extern void outsb(unsigned long addr, const void *src, unsigned long cnt);
extern void outsw(unsigned long addr, const void *src, unsigned long cnt);
extern void outsl(unsigned long addr, const void *src, unsigned long cnt);
extern void insb(unsigned long addr, void *dst, unsigned long count);
extern void insw(unsigned long addr, void *dst, unsigned long count);
extern void insl(unsigned long addr, void *dst, unsigned long count);

#define IO_SPACE_LIMIT 0xffffffff

/*
 * SBus accessors.
 *
 * SBus has only one, memory mapped, I/O space.
 * We do not need to flip bytes for SBus of course.
 */
static inline u8 _sbus_readb(unsigned long addr)
{
	return *(volatile u8 *)addr;
}

static inline u16 _sbus_readw(unsigned long addr)
{
	return *(volatile u16 *)addr;
}

static inline u32 _sbus_readl(unsigned long addr)
{
	return *(volatile u32 *)addr;
}

static inline void _sbus_writeb(u8 b, unsigned long addr)
{
	*(volatile u8 *)addr = b;
}

static inline void _sbus_writew(u16 b, unsigned long addr)
{
	*(volatile u16 *)addr = b;
}

static inline void _sbus_writel(u32 b, unsigned long addr)
{
	*(volatile u32 *)addr = b;
}

/*
 * The only reason for #define's is to hide casts to unsigned long.
 */
#define sbus_readb(a)		_sbus_readb((unsigned long)(a))
#define sbus_readw(a)		_sbus_readw((unsigned long)(a))
#define sbus_readl(a)		_sbus_readl((unsigned long)(a))
#define sbus_writeb(v, a)	_sbus_writeb(v, (unsigned long)(a))
#define sbus_writew(v, a)	_sbus_writew(v, (unsigned long)(a))
#define sbus_writel(v, a)	_sbus_writel(v, (unsigned long)(a))

static inline void *sbus_memset_io(void *__dst, int c, __kernel_size_t n)
{
	unsigned long dst = (unsigned long)__dst;

	while(n--) {
		sbus_writeb(c, dst);
		dst++;
	}
	return (void *) dst;
}

#ifdef __KERNEL__

/*
 * Bus number may be embedded in the higher bits of the physical address.
 * This is why we have no bus number argument to ioremap().
 */
extern void *ioremap(unsigned long offset, unsigned long size);
#define ioremap_nocache(X,Y)	ioremap((X),(Y))
extern void iounmap(void *addr);

/*
 * Bus number may be in res->flags... somewhere.
 */
extern unsigned long sbus_ioremap(struct resource *res, unsigned long offset,
    unsigned long size, char *name);
extern void sbus_iounmap(unsigned long vaddr, unsigned long size);


/*
 * At the moment, we do not use CMOS_READ anywhere outside of rtc.c,
 * so rtc_port is static in it. This should not change unless a new
 * hardware pops up.
 */
#define RTC_PORT(x)   (rtc_port + (x))
#define RTC_ALWAYS_BCD  0

/* Nothing to do */
/* P3: Only IDE DMA may need these. XXX Verify that it still does... */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#endif

#endif /* !(__SPARC_IO_H) */
