/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf GmbH
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/processor.h>
#include <asm/byteorder.h>
#include <mangle-port.h>

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
/*
 * IP22 seems braindead enough to swap 16bits values in hardware, but
 * not 32bits.  Go figure... Can't tell without documentation.
 */
#define __ioswab16(x) (x)
#else
#define __ioswab16(x) swab16(x)
#endif
#define __ioswab32(x) swab32(x)
#define __ioswab64(x) swab64(x)

#else

#define __ioswab8(x) (x)
#define __ioswab16(x) (x)
#define __ioswab32(x) (x)
#define __ioswab64(x) (x)

#endif

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
	return (unsigned long)address - PAGE_OFFSET;
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
	return (void *)(address + PAGE_OFFSET);
}

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
static inline unsigned long isa_virt_to_bus(volatile void * address)
{
	return (unsigned long)address - PAGE_OFFSET;
}

static inline void * isa_bus_to_virt(unsigned long address)
{
	return (void *)(address + PAGE_OFFSET);
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
 * for the processor.  This implies the assumption that there is only
 * one of these busses.
 */
extern unsigned long isa_slot_offset;

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

extern void * __ioremap(phys_t offset, phys_t size, unsigned long flags);
extern void __iounmap(volatile void __iomem *addr);

static inline void * __ioremap_mode(phys_t offset, unsigned long size,
	unsigned long flags)
{
	if (cpu_has_64bit_addresses) {
		u64 base = UNCAC_BASE;

		/*
		 * R10000 supports a 2 bit uncached attribute therefore
		 * UNCAC_BASE may not equal IO_BASE.
		 */
		if (flags == _CACHE_UNCACHED)
			base = (u64) IO_BASE;
		return (void *) (unsigned long) (base + offset);
	}

	return __ioremap(offset, size, flags);
}

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
	__ioremap_mode((offset), (size), _CACHE_UNCACHED)

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
	__ioremap_mode((offset), (size), _CACHE_UNCACHED)

/*
 * These two are MIPS specific ioremap variant.  ioremap_cacheable_cow
 * requests a cachable mapping, ioremap_uncached_accelerated requests a
 * mapping using the uncached accelerated mode which isn't supported on
 * all processors.
 */
#define ioremap_cacheable_cow(offset, size)				\
	__ioremap_mode((offset), (size), _CACHE_CACHABLE_COW)
#define ioremap_uncached_accelerated(offset, size)			\
	__ioremap_mode((offset), (size), _CACHE_UNCACHED_ACCELERATED)

static inline void iounmap(volatile void __iomem *addr)
{
	if (cpu_has_64bits)
		return;

	__iounmap(addr);
}

#define __raw_readb(addr)						\
	(*(volatile unsigned char *) __swizzle_addr_b((unsigned long)(addr)))
#define __raw_readw(addr)						\
	(*(volatile unsigned short *) __swizzle_addr_w((unsigned long)(addr)))
#define __raw_readl(addr)						\
	(*(volatile unsigned int *) __swizzle_addr_l((unsigned long)(addr)))
#ifdef CONFIG_MIPS32
#define ____raw_readq(addr)						\
({									\
	u64 __res;							\
									\
	__asm__ __volatile__ (						\
		"	.set	mips3		# ____raw_readq	\n"	\
		"	ld	%L0, (%1)			\n"	\
		"	dsra32	%M0, %L0, 0			\n"	\
		"	sll	%L0, %L0, 0			\n"	\
		"	.set	mips0				\n"	\
		: "=r" (__res)						\
		: "r" (__swizzle_addr_q((unsigned long)(addr))));	\
	__res;								\
})
#define __raw_readq(addr)						\
({									\
	unsigned long __flags;						\
	u64 __res;							\
									\
	local_irq_save(__flags);					\
	__res = ____raw_readq(addr);					\
	local_irq_restore(__flags);					\
	__res;								\
})
#endif
#ifdef CONFIG_MIPS64
#define ____raw_readq(addr)						\
	(*(volatile unsigned long *)__swizzle_addr_q((unsigned long)(addr)))
#define __raw_readq(addr)	____raw_readq(addr)
#endif

#define readb(addr)		__ioswab8(__raw_readb(addr))
#define readw(addr)		__ioswab16(__raw_readw(addr))
#define readl(addr)		__ioswab32(__raw_readl(addr))
#define readq(addr)		__ioswab64(__raw_readq(addr))
#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)
#define readq_relaxed(addr)	readq(addr)

#define __raw_writeb(b,addr)						\
do {									\
	((*(volatile unsigned char *)__swizzle_addr_b((unsigned long)(addr))) = (b));	\
} while (0)

#define __raw_writew(w,addr)						\
do {									\
	((*(volatile unsigned short *)__swizzle_addr_w((unsigned long)(addr))) = (w));	\
} while (0)

#define __raw_writel(l,addr)						\
do {									\
	((*(volatile unsigned int *)__swizzle_addr_l((unsigned long)(addr))) = (l));	\
} while (0)

#ifdef CONFIG_MIPS32
#define ____raw_writeq(val,addr)					\
do {									\
	u64 __tmp;							\
									\
	__asm__ __volatile__ (						\
		"	.set	mips3				\n"	\
		"	dsll32	%L0, %L0, 0	# ____raw_writeq\n"	\
		"	dsrl32	%L0, %L0, 0			\n"	\
		"	dsll32	%M0, %M0, 0			\n"	\
		"	or	%L0, %L0, %M0			\n"	\
		"	sd	%L0, (%2)			\n"	\
		"	.set	mips0				\n"	\
		: "=r" (__tmp)						\
		: "0" ((unsigned long long)val),			\
		  "r" (__swizzle_addr_q((unsigned long)(addr))));	\
} while (0)

#define __raw_writeq(val,addr)						\
do {									\
	unsigned long __flags;						\
									\
	local_irq_save(__flags);					\
	____raw_writeq(val, addr);					\
	local_irq_restore(__flags);					\
} while (0)
#endif
#ifdef CONFIG_MIPS64
#define ____raw_writeq(q,addr)						\
do {									\
	*(volatile unsigned long *)__swizzle_addr_q((unsigned long)(addr)) = (q);	\
} while (0)

#define __raw_writeq(q,addr)	____raw_writeq(q, addr)
#endif

#define writeb(b,addr)		__raw_writeb(__ioswab8(b),(addr))
#define writew(w,addr)		__raw_writew(__ioswab16(w),(addr))
#define writel(l,addr)		__raw_writel(__ioswab32(l),(addr))
#define writeq(q,addr)		__raw_writeq(__ioswab64(q),(addr))

/* Depends on MIPS II instruction set */
#define mmiowb() asm volatile ("sync" ::: "memory")

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

#define isa_readb(a)		readb(__ISA_IO_base + (a))
#define isa_readw(a)		readw(__ISA_IO_base + (a))
#define isa_readl(a)		readl(__ISA_IO_base + (a))
#define isa_readq(a)		readq(__ISA_IO_base + (a))
#define isa_writeb(b,a)		writeb(b,__ISA_IO_base + (a))
#define isa_writew(w,a)		writew(w,__ISA_IO_base + (a))
#define isa_writel(l,a)		writel(l,__ISA_IO_base + (a))
#define isa_writeq(q,a)		writeq(q,__ISA_IO_base + (a))
#define isa_memset_io(a,b,c)	memset_io(__ISA_IO_base + (a),(b),(c))
#define isa_memcpy_fromio(a,b,c) memcpy_fromio((a),__ISA_IO_base + (b),(c))
#define isa_memcpy_toio(a,b,c)	memcpy_toio(__ISA_IO_base + (a),(b),(c))

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

static inline void __outb(unsigned char val, unsigned long port)
{
	port = __swizzle_addr_b(port);

	*(volatile u8 *)(mips_io_port_base + port) = __ioswab8(val);
}

static inline void __outw(unsigned short val, unsigned long port)
{
	port = __swizzle_addr_w(port);

	*(volatile u16 *)(mips_io_port_base + port) = __ioswab16(val);
}

static inline void __outl(unsigned int val, unsigned long port)
{
	port = __swizzle_addr_l(port);

	*(volatile u32 *)(mips_io_port_base + port) = __ioswab32(val);
}

static inline void __outb_p(unsigned char val, unsigned long port)
{
	port = __swizzle_addr_b(port);

	*(volatile u8 *)(mips_io_port_base + port) = __ioswab8(val);
	SLOW_DOWN_IO;
}

static inline void __outw_p(unsigned short val, unsigned long port)
{
	port = __swizzle_addr_w(port);

	*(volatile u16 *)(mips_io_port_base + port) = __ioswab16(val);
	SLOW_DOWN_IO;
}

static inline void __outl_p(unsigned int val, unsigned long port)
{
	port = __swizzle_addr_l(port);

	*(volatile u32 *)(mips_io_port_base + port) = __ioswab32(val);
	SLOW_DOWN_IO;
}

#define outb(val, port)		__outb(val, port)
#define outw(val, port)		__outw(val, port)
#define outl(val, port)		__outl(val, port)
#define outb_p(val, port)	__outb_p(val, port)
#define outw_p(val, port)	__outw_p(val, port)
#define outl_p(val, port)	__outl_p(val, port)

static inline unsigned char __inb(unsigned long port)
{
	port = __swizzle_addr_b(port);

	return __ioswab8(*(volatile u8 *)(mips_io_port_base + port));
}

static inline unsigned short __inw(unsigned long port)
{
	port = __swizzle_addr_w(port);

	return __ioswab16(*(volatile u16 *)(mips_io_port_base + port));
}

static inline unsigned int __inl(unsigned long port)
{
	port = __swizzle_addr_l(port);

	return __ioswab32(*(volatile u32 *)(mips_io_port_base + port));
}

static inline unsigned char __inb_p(unsigned long port)
{
	u8 __val;

	port = __swizzle_addr_b(port);

	__val = *(volatile u8 *)(mips_io_port_base + port);
	SLOW_DOWN_IO;

	return __ioswab8(__val);
}

static inline unsigned short __inw_p(unsigned long port)
{
	u16 __val;

	port = __swizzle_addr_w(port);

	__val = *(volatile u16 *)(mips_io_port_base + port);
	SLOW_DOWN_IO;

	return __ioswab16(__val);
}

static inline unsigned int __inl_p(unsigned long port)
{
	u32 __val;

	port = __swizzle_addr_l(port);

	__val = *(volatile u32 *)(mips_io_port_base + port);
	SLOW_DOWN_IO;

	return __ioswab32(__val);
}

#define inb(port)	__inb(port)
#define inw(port)	__inw(port)
#define inl(port)	__inl(port)
#define inb_p(port)	__inb_p(port)
#define inw_p(port)	__inw_p(port)
#define inl_p(port)	__inl_p(port)

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

#define outsb(port, addr, count)	__outsb(port, addr, count)
#define insb(port, addr, count)		__insb(port, addr, count)
#define outsw(port, addr, count)	__outsw(port, addr, count)
#define insw(port, addr, count)		__insw(port, addr, count)
#define outsl(port, addr, count)	__outsl(port, addr, count)
#define insl(port, addr, count)		__insl(port, addr, count)

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
#ifdef CONFIG_DMA_NONCOHERENT

extern void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
extern void (*_dma_cache_wback)(unsigned long start, unsigned long size);
extern void (*_dma_cache_inv)(unsigned long start, unsigned long size);

#define dma_cache_wback_inv(start, size)	_dma_cache_wback_inv(start,size)
#define dma_cache_wback(start, size)		_dma_cache_wback(start,size)
#define dma_cache_inv(start, size)		_dma_cache_inv(start,size)

#else /* Sane hardware */

#define dma_cache_wback_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_wback(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)

#endif /* CONFIG_DMA_NONCOHERENT */

/*
 * Read a 32-bit register that requires a 64-bit read cycle on the bus.
 * Avoid interrupt mucking, just adjust the address for 4-byte access.
 * Assume the addresses are 8-byte aligned.
 */
#ifdef __MIPSEB__
#define __CSR_32_ADJUST 4
#else
#define __CSR_32_ADJUST 0
#endif

#define csr_out32(v,a) (*(volatile u32 *)((unsigned long)(a) + __CSR_32_ADJUST) = (v))
#define csr_in32(a)    (*(volatile u32 *)((unsigned long)(a) + __CSR_32_ADJUST))

#endif /* _ASM_IO_H */
