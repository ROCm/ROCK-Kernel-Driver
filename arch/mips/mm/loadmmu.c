/*
 * loadmmu.c: Setup cpu/cache specific function ptrs at boot time.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/* memory functions */
void (*_clear_page)(void * page);
void (*_copy_page)(void * to, void * from);

/* Cache operations. */
void (*_flush_cache_all)(void);
void (*___flush_cache_all)(void);
void (*_flush_cache_mm)(struct mm_struct *mm);
void (*_flush_cache_range)(struct mm_struct *mm, unsigned long start,
			  unsigned long end);
void (*_flush_cache_page)(struct vm_area_struct *vma, unsigned long page);
void (*_flush_cache_sigtramp)(unsigned long addr);
void (*_flush_page_to_ram)(struct page * page);
void (*_flush_icache_range)(unsigned long start, unsigned long end);
void (*_flush_icache_page)(struct vm_area_struct *vma, struct page *page);

/* DMA cache operations. */
void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
void (*_dma_cache_wback)(unsigned long start, unsigned long size);
void (*_dma_cache_inv)(unsigned long start, unsigned long size);

extern void ld_mmu_r23000(void);
extern void ld_mmu_r4xx0(void);
extern void ld_mmu_r5432(void);
extern void ld_mmu_r6000(void);
extern void ld_mmu_rm7k(void);
extern void ld_mmu_tfp(void);
extern void ld_mmu_andes(void);
extern void ld_mmu_sb1(void);
extern void ld_mmu_mips32(void);

void __init loadmmu(void)
{

	if (mips_cpu.options & MIPS_CPU_4KTLB) {
#if defined(CONFIG_CPU_R4X00) || defined(CONFIG_CPU_VR41XX) || \
    defined(CONFIG_CPU_R4300) || defined(CONFIG_CPU_R5000) || \
    defined(CONFIG_CPU_NEVADA)
		printk("Loading R4000 MMU routines.\n");
		ld_mmu_r4xx0();
#endif
#if defined(CONFIG_CPU_RM7000)
               printk("Loading RM7000 MMU routines.\n");
               ld_mmu_rm7k();
#endif
#if defined(CONFIG_CPU_R5432)
		printk("Loading R5432 MMU routines.\n");
		ld_mmu_r5432();
#endif

#if defined(CONFIG_CPU_MIPS32) || defined(CONFIG_CPU_MIPS64)
		printk("Loading MIPS32 MMU routines.\n");
		ld_mmu_mips32();
#endif
	} else switch(mips_cpu.cputype) {
#ifdef CONFIG_CPU_R3000
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_TX3912:
	case CPU_TX3922:
	case CPU_TX3927:
	case CPU_R3081E:
		printk("Loading R[23]000 MMU routines.\n");
		ld_mmu_r23000();
		break;
#endif
#ifdef CONFIG_CPU_R10000
	case CPU_R10000:
		printk("Loading R10000 MMU routines.\n");
		ld_mmu_andes();
		break;
#endif
#ifdef CONFIG_CPU_SB1
	case CPU_SB1:
		printk("Loading SB1 MMU routines.\n");
		ld_mmu_sb1();
		break;
#endif
	default:
		panic("Yeee, unsupported mmu/cache architecture.");
	}
}
