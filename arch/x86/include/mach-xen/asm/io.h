#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#define ARCH_HAS_IOREMAP_WC

#include <linux/compiler.h>
#include <asm-generic/int-ll64.h>

#define build_mmio_read(name, size, type, reg, barrier) \
static inline type name(const volatile void __iomem *addr) \
{ type ret; asm volatile("mov" size " %1,%0":reg (ret) \
:"m" (*(volatile type __force *)addr) barrier); return ret; }

#define build_mmio_write(name, size, type, reg, barrier) \
static inline void name(type val, volatile void __iomem *addr) \
{ asm volatile("mov" size " %0,%1": :reg (val), \
"m" (*(volatile type __force *)addr) barrier); }

build_mmio_read(readb, "b", unsigned char, "=q", :"memory")
build_mmio_read(readw, "w", unsigned short, "=r", :"memory")
build_mmio_read(readl, "l", unsigned int, "=r", :"memory")

build_mmio_read(__readb, "b", unsigned char, "=q", )
build_mmio_read(__readw, "w", unsigned short, "=r", )
build_mmio_read(__readl, "l", unsigned int, "=r", )

build_mmio_write(writeb, "b", unsigned char, "q", :"memory")
build_mmio_write(writew, "w", unsigned short, "r", :"memory")
build_mmio_write(writel, "l", unsigned int, "r", :"memory")

build_mmio_write(__writeb, "b", unsigned char, "q", )
build_mmio_write(__writew, "w", unsigned short, "r", )
build_mmio_write(__writel, "l", unsigned int, "r", )

#define readb_relaxed(a) __readb(a)
#define readw_relaxed(a) __readw(a)
#define readl_relaxed(a) __readl(a)
#define __raw_readb __readb
#define __raw_readw __readw
#define __raw_readl __readl

#define __raw_writeb __writeb
#define __raw_writew __writew
#define __raw_writel __writel

#define mmiowb() barrier()

#ifdef CONFIG_X86_64

build_mmio_read(readq, "q", unsigned long, "=r", :"memory")
build_mmio_write(writeq, "q", unsigned long, "r", :"memory")

#else

static inline __u64 readq(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	low = readl(p);
	high = readl(p + 1);

	return low + ((u64)high << 32);
}

static inline void writeq(__u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr+4);
}

#endif

#define readq_relaxed(a)	readq(a)

#define __raw_readq(a)		readq(a)
#define __raw_writeq(val, addr)	writeq(val, addr)

/* Let people know that we have them */
#define readq			readq
#define writeq			writeq

#define native_io_delay xen_io_delay

#ifdef CONFIG_X86_32
# include "../../asm/io_32.h"
#else
# include "../../asm/io_64.h"
#endif

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)

/* We will be supplying our own /dev/mem implementation */
#define ARCH_HAS_DEV_MEM

#define page_to_pseudophys(page) ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)
#undef page_to_phys
#define page_to_phys(page)	 (phys_to_machine(page_to_pseudophys(page)))
#define page_to_bus(page)	 (phys_to_machine(page_to_pseudophys(page)))

#define bvec_to_pseudophys(bv)	 (page_to_pseudophys((bv)->bv_page) + \
				  (unsigned long) (bv)->bv_offset)

#define BIOVEC_PHYS_MERGEABLE(vec1, vec2) \
	(bvec_to_phys(vec1) + (vec1)->bv_len == bvec_to_phys(vec2) \
	 && bvec_to_pseudophys(vec1) + (vec1)->bv_len \
	    == bvec_to_pseudophys(vec2))

#undef virt_to_bus
#undef bus_to_virt
#define virt_to_bus(_x) phys_to_machine(__pa(_x))
#define bus_to_virt(_x) __va(machine_to_phys(_x))

#include <asm/fixmap.h>

#undef isa_virt_to_bus
#undef isa_page_to_bus
#undef isa_bus_to_virt
#define isa_virt_to_bus(_x) ({ BUG(); virt_to_bus(_x); })
#define isa_page_to_bus(_x) isa_page_to_bus_is_UNSUPPORTED->_x
#define isa_bus_to_virt(_x) ((void *)__fix_to_virt(FIX_ISAMAP_BEGIN) + (_x))

#undef __ISA_IO_base
#define __ISA_IO_base ((char __iomem *)fix_to_virt(FIX_ISAMAP_BEGIN))

#endif

extern void *xlate_dev_mem_ptr(unsigned long phys);
extern void unxlate_dev_mem_ptr(unsigned long phys, void *addr);

extern void map_devmem(unsigned long pfn, unsigned long len, pgprot_t);
extern void unmap_devmem(unsigned long pfn, unsigned long len, pgprot_t);

extern int ioremap_check_change_attr(unsigned long mfn, unsigned long size,
				     unsigned long prot_val);
extern void __iomem *ioremap_wc(unsigned long offset, unsigned long size);

/*
 * early_ioremap() and early_iounmap() are for temporary early boot-time
 * mappings, before the real ioremap() is functional.
 * A boot-time mapping is currently limited to at most 16 pages.
 */
extern void early_ioremap_init(void);
extern void early_ioremap_reset(void);
extern void __iomem *early_ioremap(unsigned long offset, unsigned long size);
extern void __iomem *early_memremap(unsigned long offset, unsigned long size);
extern void early_iounmap(void __iomem *addr, unsigned long size);
extern void __iomem *fix_ioremap(unsigned idx, unsigned long phys);


#endif /* _ASM_X86_IO_H */
