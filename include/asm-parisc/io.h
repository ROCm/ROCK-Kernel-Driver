#ifndef _ASM_IO_H
#define _ASM_IO_H

/* USE_HPPA_IOREMAP IS THE MAGIC FLAG TO ENABLE OR DISABLE REAL IOREMAP() FUNCTIONALITY */
/* FOR 712 or 715 MACHINES THIS SHOULD BE ENABLED, 
   NEWER MACHINES STILL HAVE SOME ISSUES IN THE SCSI AND/OR NETWORK DRIVERS AND 
   BECAUSE OF THAT I WILL LEAVE IT DISABLED FOR NOW <deller@gmx.de> */
/* WHEN THOSE ISSUES ARE SOLVED, USE_HPPA_IOREMAP WILL GO AWAY */
#define USE_HPPA_IOREMAP 0


#include <linux/config.h>
#include <linux/types.h>
#include <asm/pgtable.h>

#define virt_to_phys(a) ((unsigned long)__pa(a))
#define phys_to_virt(a) __va(a)
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((page - mem_map) << PAGE_SHIFT)

/* Memory mapped IO */

extern void * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);

extern inline void * ioremap(unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size, 0);
}

/*
 * This one maps high address device memory and turns off caching for that area.
 * it's useful if some control registers are in such an area and write combining
 * or read caching is not desirable:
 */
extern inline void * ioremap_nocache(unsigned long offset, unsigned long size)
{
        return __ioremap(offset, size, _PAGE_NO_CACHE /* _PAGE_PCD */);
}

extern void iounmap(void *addr);

/*
 * __raw_ variants have no defined meaning.  on hppa, it means `i was
 * too lazy to ioremap first'.  kind of like isa_, except that there's
 * no additional base address to add on.
 */
#define __raw_readb(a) ___raw_readb((unsigned long)(a))
extern __inline__ unsigned char ___raw_readb(unsigned long addr)
{
	long flags;
	unsigned char ret;

	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	ldbx	0(%2),%1\n"
	"	mtsm	%0\n"
	: "=&r" (flags), "=r" (ret) : "r" (addr) );

	return ret;
}

#define __raw_readw(a) ___raw_readw((unsigned long)(a))
extern __inline__ unsigned short ___raw_readw(unsigned long addr)
{
	long flags;
	unsigned short ret;

	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	ldhx	0(%2),%1\n"
	"	mtsm	%0\n"
	: "=&r" (flags), "=r" (ret) : "r" (addr) );

	return ret;
}

#define __raw_readl(a) ___raw_readl((unsigned long)(a))
extern __inline__ unsigned int ___raw_readl(unsigned long addr)
{
	u32 ret;

	__asm__ __volatile__(
	"	ldwax	0(%1),%0\n"
	: "=r" (ret) : "r" (addr) );

	return ret;
}

#define __raw_readq(a) ___raw_readq((unsigned long)(a))
extern __inline__ unsigned long long ___raw_readq(unsigned long addr)
{
	unsigned long long ret;
#ifdef __LP64__
	__asm__ __volatile__(
	"	ldda	0(%1),%0\n"
	:  "=r" (ret) : "r" (addr) );
#else
	/* two reads may have side effects.. */
	ret = ((u64) __raw_readl(addr)) << 32;
	ret |= __raw_readl(addr+4);
#endif
	return ret;
}

#define __raw_writeb(a,b) ___raw_writeb(a, (unsigned long)(b))
extern __inline__ void ___raw_writeb(unsigned char val, unsigned long addr)
{
	long flags;
	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	stbs	%1,0(%2)\n"
	"	mtsm	%0\n"
	: "=&r" (flags) :  "r" (val), "r" (addr) );
}

#define __raw_writew(a,b) ___raw_writew(a, (unsigned long)(b))
extern __inline__ void ___raw_writew(unsigned short val, unsigned long addr)
{
	long flags;
	__asm__ __volatile__(
	"	rsm	2,%0\n"
	"	sths	%1,0(%2)\n"
	"	mtsm	%0\n"
	: "=&r" (flags) :  "r" (val), "r" (addr) );
}

#define __raw_writel(a,b) ___raw_writel(a, (unsigned long)(b))
extern __inline__ void ___raw_writel(unsigned int val, unsigned long addr)
{
	__asm__ __volatile__(
	"	stwas	%0,0(%1)\n"
	: :  "r" (val), "r" (addr) );
}

#define __raw_writeq(a,b) ___raw_writeq(a, (unsigned long)(b))
extern __inline__ void ___raw_writeq(unsigned long long val, unsigned long addr)
{
#ifdef __LP64__
	__asm__ __volatile__(
	"	stda	%0,0(%1)\n"
	: :  "r" (val), "r" (addr) );
#else
	/* two writes may have side effects.. */
	__raw_writel(val >> 32, addr);
	__raw_writel(val, addr+4);
#endif
}

#if USE_HPPA_IOREMAP
#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) (*(volatile unsigned short *) (addr))
#define readl(addr) (*(volatile unsigned int *) (addr))
#define readq(addr) (*(volatile u64 *) (addr))
#define writeb(b,addr) (*(volatile unsigned char *) (addr) = (b))
#define writew(b,addr) (*(volatile unsigned short *) (addr) = (b))
#define writel(b,addr) (*(volatile unsigned int *) (addr) = (b))
#define writeq(b,addr) (*(volatile u64 *) (addr) = (b))
#else /* !USE_HPPA_IOREMAP */
#define readb(addr) __raw_readb(addr)
#define readw(addr) le16_to_cpu(__raw_readw(addr))
#define readl(addr) le32_to_cpu(__raw_readl(addr))
#define readq(addr) le64_to_cpu(__raw_readq(addr))
#define writeb(b,addr) __raw_writeb(b,addr)
#define writew(b,addr) __raw_writew(cpu_to_le16(b),addr)
#define writel(b,addr) __raw_writel(cpu_to_le32(b),addr)
#define writeq(b,addr) __raw_writeq(cpu_to_le64(b),addr)
#endif /* !USE_HPPA_IOREMAP */

extern void __memcpy_fromio(unsigned long dest, unsigned long src, int count);
extern void __memcpy_toio(unsigned long dest, unsigned long src, int count);
extern void __memset_io(unsigned long dest, char fill, int count);

#define memcpy_fromio(a,b,c) __memcpy_fromio((unsigned long)(a), (unsigned long)(b), (c))
#define memcpy_toio(a,b,c)   __memcpy_toio((unsigned long)(a), (unsigned long)(b), (c))
#define memset_io(a,b,c)     __memset_io((unsigned long)(a), (b), (c))

/* Support old drivers which don't ioremap.
 * NB this interface is scheduled to disappear in 2.5
 */

#define EISA_BASE 0xfffffffffc000000UL
#define isa_readb(a) readb(EISA_BASE | (a))
#define isa_readw(a) readw(EISA_BASE | (a))
#define isa_readl(a) readl(EISA_BASE | (a))
#define isa_writeb(b,a) writeb((b), EISA_BASE | (a))
#define isa_writew(b,a) writew((b), EISA_BASE | (a))
#define isa_writel(b,a) writel((b), EISA_BASE | (a))
#define isa_memset_io(a,b,c) memset_io(EISA_BASE | (a), (b), (c))
#define isa_memcpy_fromio(a,b,c) memcpy_fromio((a), EISA_BASE | (b), (c))
#define isa_memcpy_toio(a,b,c) memcpy_toio(EISA_BASE | (a), (b), (c))

/* 
 * These functions support PA-RISC drivers which don't yet call ioremap().
 * They will disappear once the last of these drivers is gone.
 */
#define gsc_readb(x) __raw_readb(x)
#define gsc_readw(x) __raw_readw(x)
#define gsc_readl(x) __raw_readl(x)
#define gsc_writeb(x, y) __raw_writeb(x, y)
#define gsc_writew(x, y) __raw_writew(x, y)
#define gsc_writel(x, y) __raw_writel(x, y)


/*
 * XXX - We don't have csum_partial_copy_fromio() yet, so we cheat here and 
 * just copy it. The net code will then do the checksum later. Presently 
 * only used by some shared memory 8390 Ethernet cards anyway.
 */

#define eth_io_copy_and_sum(skb,src,len,unused) \
  memcpy_fromio((skb)->data,(src),(len))
#define isa_eth_io_copy_and_sum(skb,src,len,unused) \
  isa_memcpy_fromio((skb)->data,(src),(len))

/* Port-space IO */

#define inb_p inb
#define inw_p inw
#define inl_p inl
#define outb_p outb
#define outw_p outw
#define outl_p outl

extern unsigned char eisa_in8(unsigned short port);
extern unsigned short eisa_in16(unsigned short port);
extern unsigned int eisa_in32(unsigned short port);
extern void eisa_out8(unsigned char data, unsigned short port);
extern void eisa_out16(unsigned short data, unsigned short port);
extern void eisa_out32(unsigned int data, unsigned short port);

#if defined(CONFIG_PCI)
extern unsigned char inb(int addr);
extern unsigned short inw(int addr);
extern unsigned int inl(int addr);

extern void outb(unsigned char b, int addr);
extern void outw(unsigned short b, int addr);
extern void outl(unsigned int b, int addr);
#elif defined(CONFIG_EISA)
#define inb eisa_in8
#define inw eisa_in16
#define inl eisa_in32
#define outb eisa_out8
#define outw eisa_out16
#define outl eisa_out32
#else
static inline char inb(unsigned long addr)
{
	BUG();
	return -1;
}

static inline short inw(unsigned long addr)
{
	BUG();
	return -1;
}

static inline int inl(unsigned long addr)
{
	BUG();
	return -1;
}

#define outb(x, y)	BUG()
#define outw(x, y)	BUG()
#define outl(x, y)	BUG()
#endif

/*
 * String versions of in/out ops:
 */
extern void insb (unsigned long port, void *dst, unsigned long count);
extern void insw (unsigned long port, void *dst, unsigned long count);
extern void insl (unsigned long port, void *dst, unsigned long count);
extern void outsb (unsigned long port, const void *src, unsigned long count);
extern void outsw (unsigned long port, const void *src, unsigned long count);
extern void outsl (unsigned long port, const void *src, unsigned long count);


/* IO Port space is :      BBiiii   where BB is HBA number. */
#define IO_SPACE_LIMIT 0x00ffffff


#define dma_cache_inv(_start,_size)		do { flush_kernel_dcache_range(_start,_size); } while (0)
#define dma_cache_wback(_start,_size)		do { flush_kernel_dcache_range(_start,_size); } while (0)
#define dma_cache_wback_inv(_start,_size)	do { flush_kernel_dcache_range(_start,_size); } while (0)

/* PA machines have an MM I/O space from 0xf0000000-0xffffffff in 32
 * bit mode and from 0xfffffffff0000000-0xfffffffffffffff in 64 bit
 * mode (essentially just sign extending.  This macro takes in a 32
 * bit I/O address (still with the leading f) and outputs the correct
 * value for either 32 or 64 bit mode */
#define F_EXTEND(x) ((unsigned long)((x) | (0xffffffff00000000ULL)))

#endif
