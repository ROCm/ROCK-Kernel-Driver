/*
 * linux/include/asm-arm/arch-iop80310/memory.h
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <linux/config.h>
#include <asm/arch/iop310.h>
#include <asm/arch/iop321.h>

/*
 * Task size: 3GB
 */
#define TASK_SIZE	(0xbf000000UL)
#define TASK_SIZE_26	(0x04000000UL)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (0x40000000)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET	(0xc0000000UL)

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	(0xa0000000UL)

/*
 * physical vs virtual ram conversion
 */
#define __virt_to_phys__is_a_macro
#define __phys_to_virt__is_a_macro
#define __virt_to_phys(x)	((x) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt(x)	((x) - PHYS_OFFSET + PAGE_OFFSET)

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */
#define __virt_to_bus__is_a_macro
#define __bus_to_virt__is_a_macro

#ifdef CONFIG_ARCH_IOP310

#define __virt_to_bus(x)	(((__virt_to_phys(x)) & ~(*IOP310_SIATVR)) | ((*IOP310_SIABAR) & 0xfffffff0))
#define __bus_to_virt(x)    (__phys_to_virt(((x) & ~(*IOP310_SIALR)) | ( *IOP310_SIATVR)))

#elif defined(CONFIG_ARCH_IOP321)

#define __virt_to_bus(x)	(((__virt_to_phys(x)) & ~(*IOP321_IATVR2)) | ((*IOP321_IABAR2) & 0xfffffff0))
#define __bus_to_virt(x)    (__phys_to_virt(((x) & ~(*IOP321_IALR2)) | ( *IOP321_IATVR2)))

#endif

/* boot mem allocate global pointer for MU circular queues QBAR */
#ifdef CONFIG_IOP3XX_MU
extern void *mu_mem;
#endif

#define PFN_TO_NID(addr)	(0)

#endif
