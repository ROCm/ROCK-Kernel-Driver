#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <asm/proto.h>

/* 
 * Dummy IO MMU functions
 */

void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;
	
	if (hwdev == NULL ||
	    end_pfn > (hwdev->dma_mask>>PAGE_SHIFT) ||  /* XXX */
	    (u32)hwdev->dma_mask < 0xffffffff)
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}

void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

int pci_dma_supported(struct pci_dev *hwdev, u64 mask)
{
        /*
         * we fall back to GFP_DMA when the mask isn't all 1s,
         * so we can't guarantee allocations that must be
         * within a tighter range than GFP_DMA..
		 * RED-PEN this won't work for pci_map_single. Caller has to
		 * use GFP_DMA in the first place.
         */
        if (mask < 0x00ffffff)
                return 0;

	return 1;
} 

EXPORT_SYMBOL(pci_dma_supported);

static int __init check_ram(void) 
{ 
	if (end_pfn >= 0xffffffff>>PAGE_SHIFT) { 
		printk(KERN_ERR "WARNING more than 4GB of memory but no IOMMU.\n"
		       KERN_ERR "WARNING 32bit PCI may malfunction.\n"); 
	} 
	return 0;
} 
__initcall(check_ram);

