#ifdef __KERNEL__
#ifndef _PPC_IO_H
#define _PPC_IO_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/byteorder.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define SLOW_DOWN_IO

#define PMAC_ISA_MEM_BASE 	0
#define PMAC_PCI_DRAM_OFFSET 	0
#define CHRP_ISA_IO_BASE 	0xf8000000
#define CHRP_ISA_MEM_BASE 	0xf7000000
#define CHRP_PCI_DRAM_OFFSET 	0
#define PREP_ISA_IO_BASE 	0x80000000
#define PREP_ISA_MEM_BASE 	0xc0000000
#define PREP_PCI_DRAM_OFFSET 	0x80000000

#if defined(CONFIG_4xx)
#include <asm/board.h>
#elif defined(CONFIG_8xx)
#include <asm/mpc8xx.h>
#elif defined(CONFIG_8260)
#include <asm/mpc8260.h>
#else /* 4xx/8xx/8260 */
#ifdef CONFIG_APUS
#define _IO_BASE 0
#define _ISA_MEM_BASE 0
#define PCI_DRAM_OFFSET 0
#else /* CONFIG_APUS */
extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_dram_offset;
#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	pci_dram_offset
#endif /* CONFIG_APUS */
#endif

#define readb(addr) in_8((volatile u8 *)(addr))
#define writeb(b,addr) out_8((volatile u8 *)(addr), (b))
#if defined(CONFIG_APUS)
#define readw(addr) (*(volatile u16 *) (addr))
#define readl(addr) (*(volatile u32 *) (addr))
#define writew(b,addr) ((*(volatile u16 *) (addr)) = (b))
#define writel(b,addr) ((*(volatile u32 *) (addr)) = (b))
#else
#define readw(addr) in_le16((volatile u16 *)(addr))
#define readl(addr) in_le32((volatile u32 *)(addr))
#define writew(b,addr) out_le16((volatile u16 *)(addr),(b))
#define writel(b,addr) out_le32((volatile u32 *)(addr),(b))
#endif


#define __raw_readb(addr)	(*(volatile unsigned char *)(addr))
#define __raw_readw(addr)	(*(volatile unsigned short *)(addr))
#define __raw_readl(addr)	(*(volatile unsigned int *)(addr))
#define __raw_writeb(v, addr)	(*(volatile unsigned char *)(addr) = (v))
#define __raw_writew(v, addr)	(*(volatile unsigned short *)(addr) = (v))
#define __raw_writel(v, addr)	(*(volatile unsigned int *)(addr) = (v))

/*
 * The insw/outsw/insl/outsl macros don't do byte-swapping.
 * They are only used in practice for transferring buffers which
 * are arrays of bytes, and byte-swapping is not appropriate in
 * that case.  - paulus
 */
#define insb(port, buf, ns)	_insb((u8 *)((port)+_IO_BASE), (buf), (ns))
#define outsb(port, buf, ns)	_outsb((u8 *)((port)+_IO_BASE), (buf), (ns))
#define insw(port, buf, ns)	_insw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define outsw(port, buf, ns)	_outsw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define insl(port, buf, nl)	_insl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))
#define outsl(port, buf, nl)	_outsl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))

#ifdef CONFIG_ALL_PPC
/*
 * We have to handle possible machine checks here on powermacs
 * and potentially some CHRPs -- paulus.
 */
#define __do_in_asm(name, op)				\
extern __inline__ unsigned int name(unsigned int port)	\
{							\
	unsigned int x;					\
	__asm__ __volatile__(				\
		op " %0,0,%1\n"				\
		"1:	sync\n"				\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li	%0,-1\n"		\
		"	b	2b\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align	2\n"			\
		"	.long	1b,3b\n"		\
		".previous"				\
		: "=&r" (x)				\
		: "r" (port + _IO_BASE));		\
	return x;					\
}

#define __do_out_asm(name, op)				\
extern __inline__ void name(unsigned int val, unsigned int port) \
{							\
	__asm__ __volatile__(				\
		op " %0,0,%1\n"				\
		"1:	sync\n"				\
		"2:\n"					\
		".section __ex_table,\"a\"\n"		\
		"	.align	2\n"			\
		"	.long	1b,2b\n"		\
		".previous"				\
		: : "r" (val), "r" (port + _IO_BASE));	\
}

__do_in_asm(inb, "lbzx")
__do_in_asm(inw, "lhbrx")
__do_in_asm(inl, "lwbrx")
__do_out_asm(outb, "stbx")
__do_out_asm(outw, "sthbrx")
__do_out_asm(outl, "stwbrx")

#elif defined(CONFIG_APUS)
#define inb(port)		in_8((u8 *)((port)+_IO_BASE))
#define outb(val, port)		out_8((u8 *)((port)+_IO_BASE), (val))
#define inw(port)		in_be16((u16 *)((port)+_IO_BASE))
#define outw(val, port)		out_be16((u16 *)((port)+_IO_BASE), (val))
#define inl(port)		in_be32((u32 *)((port)+_IO_BASE))
#define outl(val, port)		out_be32((u32 *)((port)+_IO_BASE), (val))

#else /* not APUS or ALL_PPC */
#define inb(port)		in_8((u8 *)((port)+_IO_BASE))
#define outb(val, port)		out_8((u8 *)((port)+_IO_BASE), (val))
#define inw(port)		in_le16((u16 *)((port)+_IO_BASE))
#define outw(val, port)		out_le16((u16 *)((port)+_IO_BASE), (val))
#define inl(port)		in_le32((u32 *)((port)+_IO_BASE))
#define outl(val, port)		out_le32((u32 *)((port)+_IO_BASE), (val))
#endif

#define inb_p(port)		inb((port))
#define outb_p(val, port)	outb((val), (port))
#define inw_p(port)		inw((port))
#define outw_p(val, port)	outw((val), (port))
#define inl_p(port)		inl((port))
#define outl_p(val, port)	outl((val), (port))

extern void _insb(volatile u8 *port, void *buf, int ns);
extern void _outsb(volatile u8 *port, const void *buf, int ns);
extern void _insw(volatile u16 *port, void *buf, int ns);
extern void _outsw(volatile u16 *port, const void *buf, int ns);
extern void _insl(volatile u32 *port, void *buf, int nl);
extern void _outsl(volatile u32 *port, const void *buf, int nl);
extern void _insw_ns(volatile u16 *port, void *buf, int ns);
extern void _outsw_ns(volatile u16 *port, const void *buf, int ns);
extern void _insl_ns(volatile u32 *port, void *buf, int nl);
extern void _outsl_ns(volatile u32 *port, const void *buf, int nl);

/*
 * The *_ns versions below don't do byte-swapping.
 * Neither do the standard versions now, these are just here
 * for older code.
 */
#define insw_ns(port, buf, ns)	_insw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define outsw_ns(port, buf, ns)	_outsw_ns((u16 *)((port)+_IO_BASE), (buf), (ns))
#define insl_ns(port, buf, nl)	_insl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))
#define outsl_ns(port, buf, nl)	_outsl_ns((u32 *)((port)+_IO_BASE), (buf), (nl))


#define IO_SPACE_LIMIT ~0

#define memset_io(a,b,c)       memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)   memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

#ifdef __KERNEL__
/*
 * Map in an area of physical address space, for accessing
 * I/O devices etc.
 */
extern void *__ioremap(unsigned long address, unsigned long size,
		       unsigned long flags);
extern void *__ioremap_at(unsigned long phys, unsigned long size,
			  unsigned long flags);
extern void *ioremap(unsigned long address, unsigned long size);
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
extern void iounmap(void *addr);
extern unsigned long iopa(unsigned long addr);
#ifdef CONFIG_APUS
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));
#endif

/*
 * The PCI bus is inherently Little-Endian.  The PowerPC is being
 * run Big-Endian.  Thus all values which cross the [PCI] barrier
 * must be endian-adjusted.  Also, the local DRAM has a different
 * address from the PCI point of view, thus buffer addresses also
 * have to be modified [mapped] appropriately.
 */
extern inline unsigned long virt_to_bus(volatile void * address)
{
#ifndef CONFIG_APUS
        if (address == (void *)0)
		return 0;
        return (unsigned long)address - KERNELBASE + PCI_DRAM_OFFSET;
#else
	return iopa ((unsigned long) address);
#endif
}

extern inline void * bus_to_virt(unsigned long address)
{
#ifndef CONFIG_APUS
        if (address == 0)
		return 0;
        return (void *)(address - PCI_DRAM_OFFSET + KERNELBASE);
#else
	return (void*) mm_ptov (address);
#endif
}

/*
 * The PCI bus bridge can translate addresses issued by the processor(s)
 * into a different address on the PCI bus.  On 32-bit cpus, we assume
 * this mapping is 1-1, but on 64-bit systems it often isn't.
 */
#ifndef CONFIG_PPC64BRIDGE
#define phys_to_bus(x)	(x)
#define bus_to_phys(x)	(x)

#else
extern unsigned long phys_to_bus(unsigned long pa);
extern unsigned long bus_to_phys(unsigned int ba, int busnr);
#endif /* CONFIG_PPC64BRIDGE */

/*
 * Change virtual addresses to physical addresses and vv, for
 * addresses in the area where the kernel has the RAM mapped.
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
#ifndef CONFIG_APUS
	return (unsigned long) address - KERNELBASE;
#else
	return iopa ((unsigned long) address);
#endif
}

extern inline void * phys_to_virt(unsigned long address)
{
#ifndef CONFIG_APUS
	return (void *) (address + KERNELBASE);
#else
	return (void*) mm_ptov (address);
#endif
}

#endif /* __KERNEL__ */

/*
 * Enforce In-order Execution of I/O:
 * Acts as a barrier to ensure all previous I/O accesses have
 * completed before any further ones are issued.
 */
extern inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
}

/* Enforce in-order execution of data I/O. 
 * No distinction between read/write on PPC; use eieio for all three.
 */
#define iobarrier_rw() eieio()
#define iobarrier_r()  eieio()
#define iobarrier_w()  eieio()

/*
 * 8, 16 and 32 bit, big and little endian I/O operations, with barrier.
 */
extern inline int in_8(volatile unsigned char *addr)
{
	int ret;

	__asm__ __volatile__("lbz%U1%X1 %0,%1; eieio" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_8(volatile unsigned char *addr, int val)
{
	__asm__ __volatile__("stb%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

extern inline int in_le16(volatile unsigned short *addr)
{
	int ret;

	__asm__ __volatile__("lhbrx %0,0,%1; eieio" : "=r" (ret) :
			      "r" (addr), "m" (*addr));
	return ret;
}

extern inline int in_be16(volatile unsigned short *addr)
{
	int ret;

	__asm__ __volatile__("lhz%U1%X1 %0,%1; eieio" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le16(volatile unsigned short *addr, int val)
{
	__asm__ __volatile__("sthbrx %1,0,%2; eieio" : "=m" (*addr) :
			      "r" (val), "r" (addr));
}

extern inline void out_be16(volatile unsigned short *addr, int val)
{
	__asm__ __volatile__("sth%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

extern inline unsigned in_le32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwbrx %0,0,%1; eieio" : "=r" (ret) :
			     "r" (addr), "m" (*addr));
	return ret;
}

extern inline unsigned in_be32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwz%U1%X1 %0,%1; eieio" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stwbrx %1,0,%2; eieio" : "=m" (*addr) :
			     "r" (val), "r" (addr));
}

extern inline void out_be32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stw%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

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

/* Nothing to do */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#endif
#endif /* __KERNEL__ */
