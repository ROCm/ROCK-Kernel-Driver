
/* dvma support routines */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sun3mmu.h>
#include <asm/dvma.h>

unsigned long dvma_next_free = DVMA_START;
unsigned long dvma_region_end = DVMA_START + (DVMA_RESERVED_PMEGS * SUN3_PMEG_SIZE);


/* reserve such dma memory as we see fit */
void sun3_dvma_init(void)
{
	unsigned long dvma_phys_start;
	
	dvma_phys_start = (sun3_get_pte(DVMA_START) & 
			   SUN3_PAGE_PGNUM_MASK);
	dvma_phys_start <<= PAGE_SHIFT;
	
	reserve_bootmem(dvma_phys_start,
			(DVMA_RESERVED_PMEGS * SUN3_PMEG_SIZE));

}

/* get needed number of free dma pages, or panic if not enough */

void *sun3_dvma_malloc(int len)
{
	unsigned long vaddr;

       	if((dvma_next_free + len) > dvma_region_end) 
		panic("sun3_dvma_malloc: out of dvma pages");
	
	vaddr = dvma_next_free;
	dvma_next_free = DVMA_ALIGN(dvma_next_free + len);

	return (void *)vaddr;
}
     

