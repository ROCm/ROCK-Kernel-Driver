#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/config.h>
#include <linux/types.h>
#include <asm/gsc.h>

#define virt_to_phys(a) ((unsigned long)__pa(a))
#define phys_to_virt(a) __va(a)
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

#define inb_p inb
#define inw_p inw
#define inl_p inl
#define outb_p outb
#define outw_p outw
#define outl_p outl

#define readb gsc_readb
#define readw gsc_readw
#define readl gsc_readl
#define writeb gsc_writeb
#define writew gsc_writew
#define writel gsc_writel


#if defined(CONFIG_PCI) || defined(CONFIG_ISA)
/*
 *	So we get clear link errors 
 */
extern u8 inb(unsigned long addr);
extern u16 inw(unsigned long addr);
extern u32 inl(unsigned long addr);

extern void outb(unsigned char b, unsigned long addr);
extern void outw(unsigned short b, unsigned long addr);
extern void outl(u32 b, unsigned long addr);

static inline void memcpy_toio(void *dest, void *src, int count) 
{
	while(count--)
		writeb(*((char *)src)++, (char *)dest++);
}

#endif

/* IO Port space is :      BBiiii   where BB is HBA number. */
#define IO_SPACE_LIMIT 0x00ffffff

/* Right now we don't support Dino-on-a-card and V class which do PCI MMIO
 * through address/data registers. */

#define ioremap(__offset, __size)	((void *)(__offset))
#define iounmap(__addr)

#define dma_cache_inv(_start,_size)		do { flush_kernel_dcache_range(_start,_size); } while(0)
#define dma_cache_wback(_start,_size)		do { flush_kernel_dcache_range(_start,_size); } while (0)
#define dma_cache_wback_inv(_start,_size)	do { flush_kernel_dcache_range(_start,_size); } while (0)

#endif
