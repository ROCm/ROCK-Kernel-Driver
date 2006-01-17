/* Fallback functions when the main IOMMU code is not compiled in. This
   code is roughly equivalent to i386. */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <asm/proto.h>
#include <asm/processor.h>

int iommu_merge = 0;
EXPORT_SYMBOL(iommu_merge);

dma_addr_t bad_dma_address;
EXPORT_SYMBOL(bad_dma_address);

int iommu_bio_merge = 0;
EXPORT_SYMBOL(iommu_bio_merge);

int iommu_sac_force = 0;
EXPORT_SYMBOL(iommu_sac_force);

int dma_get_cache_alignment(void)
{
	return boot_cpu_data.x86_clflush_size;
}
EXPORT_SYMBOL(dma_get_cache_alignment);
