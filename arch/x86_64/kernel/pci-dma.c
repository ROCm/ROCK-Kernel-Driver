/*
 * Dynamic DMA mapping support. Common code
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <asm/io.h>

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
int pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg,
			     int nents, int direction)
{
	int i;

	BUG_ON(direction == PCI_DMA_NONE);
 	for (i = 0; i < nents; i++ ) {
		struct scatterlist *s = &sg[i];
		BUG_ON(!s->page); 
			s->dma_address = pci_map_page(hwdev, s->page, s->offset, 
						      s->length, direction); 
		s->dma_length = s->length;
	}
	return nents;
}

EXPORT_SYMBOL(pci_map_sg);

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void pci_unmap_sg(struct pci_dev *dev, struct scatterlist *sg, 
				  int nents, int dir)
{
	int i;
	for (i = 0; i < nents; i++) { 
		struct scatterlist *s = &sg[i];
		BUG_ON(s->page == NULL); 
		BUG_ON(s->dma_address == 0); 
		pci_unmap_single(dev, s->dma_address, s->dma_length, dir); 
	} 
}

EXPORT_SYMBOL(pci_unmap_sg);
