/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Takashi Iwai <tiwai@suse.de>
 * 
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/config.h>
#include <linux/pci.h>
#include <sound/memalloc.h>


#ifdef HACK_PCI_ALLOC_CONSISTENT
/*
 * A dirty hack... when the kernel code is fixed this should be removed.
 *
 * since pci_alloc_consistent always tries GFP_DMA when the requested
 * pci memory region is below 32bit, it happens quite often that even
 * 2 order of pages cannot be allocated.
 *
 * so in the following, we allocate at first without dma_mask, so that
 * allocation will be done without GFP_DMA.  if the area doesn't match
 * with the requested region, then realloate with the original dma_mask
 * again.
 */

void *snd_pci_hack_alloc_consistent(struct pci_dev *hwdev, size_t size,
				    dma_addr_t *dma_handle)
{
	void *ret;
	u64 dma_mask;
	unsigned long rmask;

	if (hwdev == NULL)
		return pci_alloc_consistent(hwdev, size, dma_handle);
	dma_mask = hwdev->dma_mask;
	rmask = ~((unsigned long)dma_mask);
	hwdev->dma_mask = 0xffffffff; /* do without masking */
	ret = pci_alloc_consistent(hwdev, size, dma_handle);
	hwdev->dma_mask = dma_mask; /* restore */
	if (ret) {
		/* obtained address is out of range? */
		if (((unsigned long)*dma_handle + size - 1) & rmask) {
			/* reallocate with the proper mask */
			pci_free_consistent(hwdev, size, ret, *dma_handle);
			ret = pci_alloc_consistent(hwdev, size, dma_handle);
		}
	} else {
		/* wish to success now with the proper mask... */
		if (dma_mask != 0xffffffff)
			ret = pci_alloc_consistent(hwdev, size, dma_handle);
	}
	return ret;
}

#endif /* HACK_PCI_ALLOC_CONSISTENT */
