/*
 * Scatter-Gather PCM access
 *
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_sgbuf.h>


/* table entries are align to 32 */
#define SGBUF_TBL_ALIGN		32
#define sgbuf_align_table(tbl)	((((tbl) + SGBUF_TBL_ALIGN - 1) / SGBUF_TBL_ALIGN) * SGBUF_TBL_ALIGN)


/**
 * snd_pcm_sgbuf_alloc_pages - allocate the pages for the SG buffer
 * @pci: the pci device pointer
 * @size: the requested buffer size in bytes
 * @dmab: the dma-buffer record to store
 *
 * Initializes the SG-buffer table and allocates the buffer pages
 * for the given size.
 * The pages are mapped to the virtually continuous memory.
 *
 * This function is usually called from snd_pcm_lib_malloc_pages().
 *
 * Returns the mapped virtual address of the buffer if allocation was
 * successful, or NULL at error.
 */
void *snd_pcm_sgbuf_alloc_pages(struct pci_dev *pci, size_t size, struct snd_pcm_dma_buffer *dmab)
{
	struct snd_sg_buf *sgbuf;
	unsigned int i, pages;

	dmab->area = NULL;
	dmab->addr = 0;
	dmab->private_data = sgbuf = snd_magic_kcalloc(snd_pcm_sgbuf_t, 0, GFP_KERNEL);
	if (! sgbuf)
		return NULL;
	sgbuf->pci = pci;
	pages = snd_pcm_sgbuf_pages(size);
	sgbuf->tblsize = sgbuf_align_table(pages);
	sgbuf->table = snd_kcalloc(sizeof(*sgbuf->table) * sgbuf->tblsize, GFP_KERNEL);
	if (! sgbuf->table)
		goto _failed;
	sgbuf->page_table = snd_kcalloc(sizeof(*sgbuf->page_table) * sgbuf->tblsize, GFP_KERNEL);
	if (! sgbuf->page_table)
		goto _failed;

	/* allocate each page */
	for (i = 0; i < pages; i++) {
		void *ptr;
		dma_addr_t addr;
		ptr = snd_malloc_pci_page(sgbuf->pci, &addr);
		if (! ptr)
			goto _failed;
		sgbuf->table[i].buf = ptr;
		sgbuf->table[i].addr = addr;
		sgbuf->page_table[i] = virt_to_page(ptr);
		sgbuf->pages++;
	}

	sgbuf->size = size;
	dmab->area = vmap(sgbuf->page_table, sgbuf->pages);
	if (! dmab->area)
		goto _failed;
	return dmab->area;

 _failed:
	snd_pcm_sgbuf_free_pages(dmab); /* free the table */
	return NULL;
}

/**
 * snd_pcm_sgbuf_free_pages - free the sg buffer
 * @dmab: dma buffer record
 *
 * Releases the pages and the SG-buffer table.
 *
 * This function is called usually from snd_pcm_lib_free_pages().
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_sgbuf_free_pages(struct snd_pcm_dma_buffer *dmab)
{
	struct snd_sg_buf *sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, dmab->private_data, return -EINVAL);
	int i;

	for (i = 0; i < sgbuf->pages; i++)
		snd_free_pci_page(sgbuf->pci, sgbuf->table[i].buf, sgbuf->table[i].addr);
	if (dmab->area)
		vunmap(dmab->area);
	dmab->area = NULL;

	if (sgbuf->table)
		kfree(sgbuf->table);
	if (sgbuf->page_table)
		kfree(sgbuf->page_table);
	snd_magic_kfree(sgbuf);
	dmab->private_data = NULL;
	
	return 0;
}

/**
 * snd_pcm_sgbuf_ops_page - get the page struct at the given offset
 * @substream: the pcm substream instance
 * @offset: the buffer offset
 *
 * Returns the page struct at the given buffer offset.
 * Used as the page callback of PCM ops.
 */
struct page *snd_pcm_sgbuf_ops_page(snd_pcm_substream_t *substream, unsigned long offset)
{
	struct snd_sg_buf *sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, _snd_pcm_substream_sgbuf(substream), return NULL);

	unsigned int idx = offset >> PAGE_SHIFT;
	if (idx >= (unsigned int)sgbuf->pages)
		return NULL;
	return sgbuf->page_table[idx];
}


/*
 *  Exported symbols
 */
EXPORT_SYMBOL(snd_pcm_lib_preallocate_sg_pages);
EXPORT_SYMBOL(snd_pcm_lib_preallocate_sg_pages_for_all);
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_page);
