/*
 *  Various wrappers
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
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

#ifdef ALSA_BUILD
#include "config.h"
#endif

#include <linux/version.h>
#include <linux/config.h>
#ifdef ALSA_BUILD
#if defined(CONFIG_MODVERSIONS) && !defined(__GENKSYMS__) && !defined(__DEPEND__)
#define MODVERSIONS
#include <linux/modversions.h>
#include "sndversions.h"
#endif
#endif
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>

#ifdef CONFIG_SND_DEBUG_MEMORY
void *snd_wrapper_kmalloc(size_t size, int flags)
{
	return kmalloc(size, flags);
}

void snd_wrapper_kfree(const void *obj)
{
	kfree(obj);
}

void *snd_wrapper_vmalloc(unsigned long size)
{
	return vmalloc(size);
}

void snd_wrapper_vfree(void *obj)
{
	vfree(obj);
}
#endif


/* check the condition in <sound/core.h> !! */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0)
#if defined(__i386__) || defined(__ppc__) || defined(__x86_64__)

#include <linux/pci.h>

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
	if (ret && ((*dma_handle + size - 1) & rmask)) {
		pci_free_consistent(hwdev, size, ret, *dma_handle);
		ret = 0;
	}
	hwdev->dma_mask = dma_mask; /* restore */
	if (! ret)
		ret = pci_alloc_consistent(hwdev, size, dma_handle);
	return ret;
}

#endif
#endif
