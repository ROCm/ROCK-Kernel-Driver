/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf GmbH
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 FSMLabs, Inc.
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/config.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/pgtable-bits.h>
#include <asm/byteorder.h>
#include <asm/mipsregs.h>

#ifdef CONFIG_SGI_IP27
extern unsigned long bus_to_baddr[256];

#define bus_to_baddr(bus, addr)	(bus_to_baddr[(bus)->number] + (addr))
#define baddr_to_bus(bus, addr)	((addr) - bus_to_baddr[(bus)->number])
#else
#define bus_to_baddr(bus, addr)	(addr)
#define baddr_to_bus(bus, addr)	(addr)
#endif

/*
 * Slowdown I/O port space accesses for antique hardware.
 */
#undef CONF_SLOWDOWN_IO

/*
 * Sane hardware offers swapping of I/O space accesses in hardware; less
 * sane hardware forces software to fiddle with this ...
 */
#if defined(CONFIG_SWAP_IO_SPACE) && defined(__MIPSEB__)

#define __ioswab8(x) (x)
#ifdef CONFIG_SGI_IP22
/* IP22 seems braindead enough to swap 16bits values in hardware, but
   not 32bits.  Go figure... Can't tell without documentation. */
#define __ioswab16(x) (x)
#else
#define __ioswab16(x) swab16(x)
#endif
#define __ioswab32(x) swab32(x)

#else

#define __ioswab8(x) (x)
#define __ioswab16(x) (x)
#define __ioswab32(x) (x)

#endif

/*
 * <Bacchus> Historically I wrote this stuff the same way as Linus did
 * because I was young and clueless.  And now it's so jucky that I
 * don't want to put my eyes on it again to get rid of it :-)
 *
 * I'll do it then, because this code offends both me and my compiler
 * - particularly the bits of inline asm which end up doing crap like
 * 'lb $2,$2($5)' -- dwmw2
 */

#define IO_SPACE_LIMIT 0xffff

/*
 * On MIPS I/O ports are memory mapped, so we access them using normal
 * load/store instructions. mips_io_port_base is the virtual address to
 * which all ports are being mapped.  For sake of efficiency some code
 * assumes that this is an address that can be loaded with a single lui
 * instruction, so the lower 16 bits must be zero.  Should be true on
 * on any sane architecture; generic code does not use this assumption.
 */
extern const unsigned long mips_io_port_base;

#define set_io_port_base(base)	\
	do { * (unsigned long *) &mips_io_port_base = (base); } while (0)

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 *
 */

#define __SLOW_DOWN_IO \
	__asm__ __volatile__( \
		"sb\t$0,0x80(%0)" \
		: : "r" (mips_io_port_base));

#ifdef CONF_SLOWDOWN_IO
#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif
#else
#define SLOW_DOWN_IO
#endif

/*
 *     virt_to_phys    -       map virtual addresses to physical
 *     @address: address to remap
 *
 *     The returned physical address is the physical (CPU) mapping for
 *     the memory address given. It is only valid to use this function on
 *     addresses directly mapped or allocated via kmalloc.
 *
 *     This function does not give bus mappings for DMA transfers. In
 *     almost all conceivable cases a device driver should not be using
 *     this function
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return PHYSADDR(address);
}

/*
 *     phys_to_virt    -       map physical address to virtual
 *     @address: address to remap
 *
 *     The returned virtual address is a current CPU mapping for
 *     the memory address given. It is only valid to use this function on
 *     addresses that have a kernel mapping
 *
 *     This function does not handle bus mappings for DMA transfers. In
 *     almost all conceivable cases a device driver should not be using
 *     this function
 */
static inline void * phys_to_virt(unsigned long address)
{
	return (void *)KSEG0ADDR(address);
}

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
static inline unsigned long isa_virt_to_bus(volatile void * address)
{
	return PHYSADDR(address);
}

static inline void * isa_bus_to_virt(unsigned long address)
{
	return (void *)KSEG0ADDR(address);
}

#define isa_page_to_bus page_to_phys

/*
 * However PCI ones are not necessarily 1:1 and therefore these interfaces
 * are forbidden in portable PCI drivers.
 *
 * Allow them for x86 for legacy drivers, though.
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is mapped
 * for the processor.
 */
extern unsigned long isa_slot_offset;

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

extern void * __ioremap(phys_t offset, phys_t size, unsigned long flags);


/*
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 */
#define ioremap(offset, size)						\
	__ioremap((offset), (size), _CACHE_UNCACHED)

/*
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In paticular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 */
#define ioremap_nocache(offset, size)					\
	__ioremap((offset), (size), _CACHE_UNCACHED)
#define ioremap_cacheable_cow(offset, size)				\
	__ioremap((offset), (size), _CACHE_CACHABLE_COW)
#define ioremap_uncached_accelerated(offset, size)			\
	__ioremap((offset), (size), _CACHE_UNCACHED_ACCELERATED)

extern void iounmap(void *addr);

/*
 * XXX We need system specific versions of these to handle EISA address bits
 * 24-31 on SNI.
 * XXX more SNI hacks.
 */
#define readb(addr)		(*(volatile unsigned char *)(addr))
#define readw(addr)		__ioswab16((*(volatile unsigned short *)(addr)))
#define readl(addr)		__ioswab32((*(volatile unsigned int *)(addr)))

#define __raw_readb(addr)	(*(volatile unsigned char *)(addr))
#define __raw_readw(addr)	(*(volatile unsigned short *)(addr))
#define __raw_readl(addr)	(*(volatile unsigned int *)(addr))

#define writeb(b,addr) ((*(volatile unsigned char *)(addr)) = (__ioswab8(b)))
#define writew(b,addr) ((*(volatile unsigned short *)(addr)) = (__ioswab16(b)))
#define writel(b,addr) ((*(volatile unsigned int *)(addr)) = (__ioswab32(b)))

#define __raw_writeb(b,addr)	((*(volatile unsigned char *)(addr)) = (b))
#define __raw_writew(w,addr)	((*(volatile unsigned short *)(addr)) = (w))
#define __raw_writel(l,addr)	((*(volatile unsigned int *)(addr)) = (l))

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

/*
 * ISA space is 'always mapped' on currently supported MIPS systems, no need
 * to explicitly ioremap() it. The fact that the ISA IO space is mapped
 * to PAGE_OFFSET is pure coincidence - it does not mean ISA values
 * are physical addresses. The following constant pointer can be
 * used as the IO-area pointer (it can be iounmapped as well, so the
 * analogy with PCI is quite large):
 */
#define __ISA_IO_base ((char *)(isa_slot_offset))

#define isa_readb(a) readb(__ISA_IO_base + (a))
#define isa_readw(a) readw(__ISA_IO_base + (a))
#define isa_readl(a) readl(__ISA_IO_base + (a))
#define isa_writeb(b,a) writeb(b,__ISA_IO_base + (a))
#define isa_writew(w,a) writew(w,__ISA_IO_base + (a))
#define isa_writel(l,a) writel(l,__ISA_IO_base + (a))
#define isa_memset_io(a,b,c)		memset_io(__ISA_IO_base + (a),(b),(c))
#define isa_memcpy_fromio(a,b,c)	memcpy_fromio((a),__ISA_IO_base + (b),(c))
#define isa_memcpy_toio(a,b,c)		memcpy_toio(__ISA_IO_base + (a),(b),(c))

/*
 * We don't have csum_partial_copy_fromio() yet, so we cheat here and
 * just copy it. The net code will then do the checksum later.
 */
#define eth_io_copy_and_sum(skb,src,len,unused) memcpy_fromio((skb)->data,(src),(len))
#define isa_eth_io_copy_and_sum(a,b,c,d) eth_copy_and_sum((a),(b),(c),(d))

/*
 *     check_signature         -       find BIOS signatures
 *     @io_addr: mmio address to check
 *     @signature:  signature block
 *     @length: length of signature
 *
 *     Perform a signature comparison with the mmio address io_addr. This
 *     address should have been obtained by ioremap.
 *     Returns 1 on a match.
 */
static inline int check_signature(unsigned long io_addr,
                                  const unsigned char *signature, int length)
{
	int retval = 0;
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

/*
 *     isa_check_signature             -       find BIOS signatures
 *     @io_addr: mmio address to check
 *     @signature:  signature block
 *     @length: length of signature
 *
 *     Perform a signature comparison with the ISA mmio address io_addr.
 *     Returns 1 on a match.
 *
 *     This function is deprecated. New drivers should use ioremap and
 *     check_signature.
 */
#define isa_check_signature(io, s, l) check_signature(i,s,l)


#define outb(val,port)							\
do {									\
	*(volatile u8 *)(mips_io_port_base + (port)) = __ioswab8(val);	\
} while(0)

#define outw(val,port)							\
do {									\
	*(volatile u16 *)(mips_io_port_base + (port)) = __ioswab16(val);	\
} while(0)

#define outl(val,port)							\
do {									\
	*(volatile u32 *)(mips_io_port_base + (port)) = __ioswab32(val);\
} while(0)

#define outb_p(val,port)						\
do {									\
	*(volatile u8 *)(mips_io_port_base + (port)) = __ioswab8(val);	\
	SLOW_DOWN_IO;							\
} while(0)

#define outw_p(val,port)						\
do {									\
	*(volatile u16 *)(mips_io_port_base + (port)) = __ioswab16(val);\
	SLOW_DOWN_IO;							\
} while(0)

#define outl_p(val,port)						\
do {									\
	*(volatile u32 *)(mips_io_port_base + (port)) = __ioswab32(val);\
	SLOW_DOWN_IO;							\
} while(0)

#define inb(port) __inb(port)
#define inw(port) __inw(port)
#define inl(port) __inl(port)
#define inb_p(port) __inb_p(port)
#define inw_p(port) __inw_p(port)
#define inl_p(port) __inl_p(port)

static inline unsigned char __inb(unsigned long port)
{
	return __ioswab8(*(volatile u8 *)(mips_io_port_base + port));
}

static inline unsigned short __inw(unsigned long port)
{
	return __ioswab16(*(volatile u16 *)(mips_io_port_base + port));
}

static inline unsigned int __inl(unsigned long port)
{
	return __ioswab32(*(volatile u32 *)(mips_io_port_base + port));
}

static inline unsigned char __inb_p(unsigned long port)
{
	u8 __val;

	__val = *(volatile u8 *)(mips_io_port_base + port);
	SLOW_DOWN_IO;

	return __ioswab8(__val);
}

static inline unsigned short __inw_p(unsigned long port)
{
	u16 __val;

	__val = *(volatile u16 *)(mips_io_port_base + port);
	SLOW_DOWN_IO;

	return __ioswab16(__val);
}

static inline unsigned int __inl_p(unsigned long port)
{
	u32 __val;

	__val = *(volatile u32 *)(mips_io_port_base + port);
	SLOW_DOWN_IO;
	return __ioswab32(__val);
}

#define outsb(port, addr, count) __outsb(port, addr, count)
#define insb(port, addr, count) __insb(port, addr, count)
#define outsw(port, addr, count) __outsw(port, addr, count)
#define insw(port, addr, count) __insw(port, addr, count)
#define outsl(port, addr, count) __outsl(port, addr, count)
#define insl(port, addr, count) __insl(port, addr, count)

static inline void __outsb(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		outb(*(u8 *)addr, port);
		addr++;
	}
}

static inline void __insb(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		*(u8 *)addr = inb(port);
		addr++;
	}
}

static inline void __outsw(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		outw(*(u16 *)addr, port);
		addr += 2;
	}
}

static inline void __insw(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		*(u16 *)addr = inw(port);
		addr += 2;
	}
}

static inline void __outsl(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		outl(*(u32 *)addr, port);
		addr += 4;
	}
}

static inline void __insl(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		*(u32 *)addr = inl(port);
		addr += 4;
	}
}

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are three types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_wback(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 */
#ifdef CONFIG_NONCOHERENT_IO

extern void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
extern void (*_dma_cache_wback)(unsigned long start, unsigned long size);
extern void (*_dma_cache_inv)(unsigned long start, unsigned long size);

#define dma_cache_wback_inv(start, size)_dma_cache_wback_inv(start,size)
#define dma_cache_wback(start, size)	_dma_cache_wback(start,size)
#define dma_cache_inv(start, size)	_dma_cache_inv(start,size)

#else /* Sane hardware */

#define dma_cache_wback_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_wback(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)

#endif /* CONFIG_NONCOHERENT_IO */

#endif /* _ASM_IO_H */
