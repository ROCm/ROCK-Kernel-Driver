/*
 * Virtual DMA allocation
 *
 * (C) 1999 Thomas Bogendoerfer (tsbogend@alpha.franken.de) 
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/mm.h>

#include <asm/sun3x.h>
#include <asm/dvma.h>
#include <asm/io.h>
#include <asm/page.h>

/* IOMMU support */

#define IOMMU_ENTRIES		   2048
#define IOMMU_ADDR_MASK            0x03ffe000
#define IOMMU_CACHE_INHIBIT        0x00000040
#define IOMMU_FULL_BLOCK           0x00000020
#define IOMMU_MODIFIED             0x00000010
#define IOMMU_USED                 0x00000008
#define IOMMU_WRITE_PROTECT        0x00000004
#define IOMMU_DT_MASK              0x00000003
#define IOMMU_DT_INVALID           0x00000000
#define IOMMU_DT_VALID             0x00000001
#define IOMMU_DT_BAD               0x00000002

#define DVMA_PAGE_SHIFT	13
#define DVMA_PAGE_SIZE	(1UL << DVMA_PAGE_SHIFT)
#define DVMA_PAGE_MASK	(~(DVMA_PAGE_SIZE-1))


static volatile unsigned long *iommu_pte = (unsigned long *)SUN3X_IOMMU;
static unsigned long iommu_use[IOMMU_ENTRIES];
static unsigned long iommu_bitmap[IOMMU_ENTRIES/32];


#define dvma_entry_paddr(index) 	(iommu_pte[index] & IOMMU_ADDR_MASK)
#define dvma_entry_vaddr(index,paddr) 	((index << DVMA_PAGE_SHIFT) |  \
					 (paddr & (DVMA_PAGE_SIZE-1)))
#define dvma_entry_set(index,addr)	(iommu_pte[index] =            \
					    (addr & IOMMU_ADDR_MASK) | \
				             IOMMU_DT_VALID)
#define dvma_entry_clr(index)		(iommu_pte[index] = IOMMU_DT_INVALID)
#define dvma_entry_use(index)		(iommu_use[index])
#define dvma_entry_inc(index)		(iommu_use[index]++)
#define dvma_entry_dec(index)		(iommu_use[index]--)
#define dvma_entry_hash(addr)		((addr >> DVMA_PAGE_SHIFT) ^ \
					 ((addr & 0x03c00000) >>     \
						(DVMA_PAGE_SHIFT+4)))
#define dvma_map			iommu_bitmap
#define dvma_map_size			(IOMMU_ENTRIES/2)
#define dvma_slow_offset		(IOMMU_ENTRIES/2)
#define dvma_is_slow(addr)		((addr) & 		      \
					 (dvma_slow_offset << DVMA_PAGE_SHIFT))

static int fixed_dvma;

void __init dvma_init(void)
{
    unsigned long tmp;

    if ((unsigned long)high_memory < (IOMMU_ENTRIES << DVMA_PAGE_SHIFT)) {
	printk ("Sun3x fixed DVMA mapping\n");
	fixed_dvma = 1;
	for (tmp = 0; tmp < (unsigned long)high_memory; tmp += DVMA_PAGE_SIZE)
	dvma_entry_set (tmp >> DVMA_PAGE_SHIFT, virt_to_phys((void *)tmp));
	fixed_dvma = 1;
    } else {
	printk ("Sun3x variable DVMA mapping\n");
	for (tmp = 0; tmp < IOMMU_ENTRIES; tmp++)
	    dvma_entry_clr (tmp);
	fixed_dvma = 0;
    }
}

unsigned long dvma_slow_alloc (unsigned long paddr, int npages)
{
    int scan, base;
    
    scan = 0;
    for (;;) {
	scan = find_next_zero_bit(dvma_map, dvma_map_size, scan);
	if ((base = scan) + npages > dvma_map_size) {
	    printk ("dvma_slow_alloc failed for %d pages\n",npages);
	    return 0;
	}
	for  (;;) {
	    if (scan >= base + npages) goto found;
	    if (test_bit(scan, dvma_map)) break;
	    scan++;
	}
    }

found:
    for (scan = base; scan < base+npages; scan++) {
	dvma_entry_set(scan+dvma_slow_offset, paddr);
	paddr += DVMA_PAGE_SIZE;
	set_bit(scan, dvma_map);
    }
    return (dvma_entry_vaddr((base+dvma_slow_offset),paddr));
}

unsigned long dvma_alloc (unsigned long paddr, unsigned long size)
{
    int index;
    int pages = ((paddr & ~DVMA_PAGE_MASK) + size + (DVMA_PAGE_SIZE-1)) >>
		DVMA_PAGE_SHIFT;

    if (fixed_dvma)
	return ((unsigned long)phys_to_virt (paddr));

    if (pages > 1) /* multi page, allocate from slow pool */
	return dvma_slow_alloc (paddr, pages);
    
    index = dvma_entry_hash (paddr);

    if (dvma_entry_use(index)) {
	if (dvma_entry_paddr(index) == (paddr & DVMA_PAGE_MASK)) {
	    dvma_entry_inc(index);
	    return dvma_entry_vaddr(index,paddr);
	}
	/* collision, allocate from slow pool */
	return dvma_slow_alloc (paddr, pages);
    }
    
    dvma_entry_set(index,paddr); 
    dvma_entry_inc(index);
    return dvma_entry_vaddr(index,paddr);
}

void dvma_free (unsigned long dvma_addr, unsigned long size)
{
    int npages;
    int index;
    
    if (fixed_dvma)
	return;

    if (!dvma_is_slow(dvma_addr)) {
	index = (dvma_addr >> DVMA_PAGE_SHIFT);
	if (dvma_entry_use(index) == 0) {
	    printk ("dvma_free: %lx entry already free\n",dvma_addr);
	    return;
	}
        dvma_entry_dec(index);
	if (dvma_entry_use(index) == 0)
	    dvma_entry_clr(index);
	return;
    }

    /* free in slow pool */
    npages = ((dvma_addr & ~DVMA_PAGE_MASK) + size + (DVMA_PAGE_SIZE-1)) >>
	    DVMA_PAGE_SHIFT;
    for (index = (dvma_addr >> DVMA_PAGE_SHIFT); npages--; index++) {
	dvma_entry_clr(index);
	clear_bit (index,dvma_map);
    }
}
