#include_next <asm/swiotlb.h>

#ifdef CONFIG_SWIOTLB
#define pci_swiotlb_detect() 1
#else
#define swiotlb_init(verbose) ((void)(verbose))
#endif

dma_addr_t swiotlb_map_single_phys(struct device *, phys_addr_t, size_t size,
				   int dir);
