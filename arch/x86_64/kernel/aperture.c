/* 
 * Firmware replacement code.
 * 
 * Work around broken BIOSes that don't set an aperture. 
 * The IOMMU code needs an aperture even who no AGP is present in the system.
 * Map the aperture over some low memory.  This is cheaper than doing bounce 
 * buffering. The memory is lost. This is done at early boot because only
 * the bootmem allocator can allocate 32+MB. 
 * 
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * $Id: aperture.c,v 1.2 2002/09/19 19:25:32 ak Exp $
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/pci_ids.h>
#include <asm/e820.h>
#include <asm/io.h>
#include <asm/proto.h>
#include <asm/pci-direct.h>

int fallback_aper_order __initdata = 1; /* 64MB */
int fallback_aper_force __initdata = 0; 

extern int no_iommu, force_mmu;

/* This code runs before the PCI subsystem is initialized, so just 
   access the northbridge directly. */

#define NB_ID_3 (PCI_VENDOR_ID_AMD | (0x1103<<16))

static u32 __init allocate_aperture(void) 
{
#ifdef CONFIG_DISCONTIGMEM
	pg_data_t *nd0 = NODE_DATA(0);
#else
	pg_data_t *nd0 = &contig_page_data;
#endif	
	u32 aper_size;
	void *p; 

	if (fallback_aper_order > 7) 
		fallback_aper_order = 7; 
	aper_size = (32 * 1024 * 1024) << fallback_aper_order; 

	/* 
         * Aperture has to be naturally aligned it seems. This means an
	 * 2GB aperture won't have much changes to succeed in the lower 4GB of 
	 * memory. Unfortunately we cannot move it up because that would make
	 * the IOMMU useless.
	 */
	p = __alloc_bootmem_node(nd0, aper_size, aper_size, 0); 
	if (!p || __pa(p)+aper_size > 0xffffffff) {
		printk("Cannot allocate aperture memory hole (%p,%uK)\n",
		       p, aper_size>>10);
		if (p)
			free_bootmem_node(nd0, (unsigned long)p, aper_size); 
		return 0;
	}
	printk("Mapping aperture over %d KB of RAM @ %lx\n",  
	       aper_size >> 10, __pa(p)); 
	return (u32)__pa(p); 
}

void __init iommu_hole_init(void) 
{ 
	int fix, num; 
	u32 aper_size, aper_alloc, aper_order;
	u64 aper_base; 

	if (no_iommu)
		return;
	if (end_pfn < (0xffffffff>>PAGE_SHIFT) && !force_mmu) 
		return;

	printk("Checking aperture...\n"); 

	fix = 0;
	for (num = 24; num < 32; num++) {		
		if (read_pci_config(0, num, 3, 0x00) != NB_ID_3) 
			continue;	

		aper_order = (read_pci_config(0, num, 3, 0x90) >> 1) & 7; 
		aper_size = (32 * 1024 * 1024) << aper_order; 
		aper_base = read_pci_config(0, num, 3, 0x94) & 0x7fff;
		aper_base <<= 25; 

		printk("CPU %d: aperture @ %Lx size %u KB\n", num-24, 
		       aper_base, aper_size>>10);
		if (!aper_base || aper_base + aper_size >= 0xffffffff) {
			fix = 1; 
			break; 
		} 
		
		if (e820_mapped(aper_base, aper_base + aper_size, E820_RAM)) {  
			printk("Aperture pointing to e820 RAM. Ignoring.\n");
			fix = 1; 
			break; 
		} 
	} 

	if (!fix && !fallback_aper_force) 
		return; 

	printk("Your BIOS doesn't leave a aperture memory hole\n");
	printk("Please enable the IOMMU option in the BIOS setup\n"); 
	aper_alloc = allocate_aperture(); 
	if (!aper_alloc) 
		return; 

	for (num = 24; num < 32; num++) { 		
		if (read_pci_config(0, num, 3, 0x00) != NB_ID_3) 
			continue;	

		/* Don't enable translation yet. That is done later. 
		   Assume this BIOS didn't initialise the GART so 
		   just overwrite all previous bits */ 
		write_pci_config(0, num, 3, 0x90, fallback_aper_order<<1); 
		write_pci_config(0, num, 3, 0x94, aper_alloc>>25); 
	} 
} 
