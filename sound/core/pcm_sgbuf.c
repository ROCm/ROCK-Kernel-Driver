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


/*
 * snd_pcm_sgbuf_new - constructor of the sgbuf instance
 * @pci: pci device pointer
 *
 * Initializes the SG-buffer instance to be assigned to
 * substream->dma_private.
 * 
 * Returns the pointer of the instance, or NULL at error.
 */
struct snd_sg_buf *snd_pcm_sgbuf_new(struct pci_dev *pci)
{
	struct snd_sg_buf *sgbuf;

	sgbuf = snd_magic_kcalloc(snd_pcm_sgbuf_t, 0, GFP_KERNEL);
	if (! sgbuf)
		return NULL;
	sgbuf->pci = pci;
	sgbuf->pages = 0;
	sgbuf->tblsize = 0;

	return sgbuf;
}

/*
 * snd_pcm_sgbuf_delete - destructor of sgbuf instance
 * @sgbuf: the SG-buffer instance
 *
 * Destructor Releaes all pages and free the sgbuf instance.
 */
void snd_pcm_sgbuf_delete(struct snd_sg_buf *sgbuf)
{
	snd_pcm_sgbuf_free_pages(sgbuf, NULL);
	snd_magic_kfree(sgbuf);
}

/**
 * snd_pcm_sgbuf_alloc_pages - allocate the pages for the SG buffer
 * @sgbuf: the sgbuf instance
 * @size: the requested buffer size in bytes
 *
 * Allocates the buffer pages for the given size and updates the
 * sg buffer table.  The pages are mapped to the virtually continuous
 * memory.
 *
 * This function is usually called from snd_pcm_lib_malloc_pages().
 *
 * Returns the mapped virtual address of the buffer if allocation was
 * successful, or NULL at error.
 */
void *snd_pcm_sgbuf_alloc_pages(struct snd_sg_buf *sgbuf, size_t size)
{
	unsigned int i, pages;
	void *vmaddr;

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
	vmaddr = vmap(sgbuf->page_table, sgbuf->pages);
	if (! vmaddr)
		goto _failed;
	return vmaddr;

 _failed:
	snd_pcm_sgbuf_free_pages(sgbuf, NULL); /* free the table */
	return NULL;
}

/**
 * snd_pcm_sgbuf_free_pages - free the sg buffer
 * @sgbuf: the sgbuf instance
 * @vmaddr: the mapped virtual address
 *
 * Releases the pages and the mapped tables.
 *
 * This function is called usually from snd_pcm_lib_free_pages().
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_sgbuf_free_pages(struct snd_sg_buf *sgbuf, void *vmaddr)
{
	if (vmaddr)
		vunmap(vmaddr);

	while (sgbuf->pages > 0) {
		sgbuf->pages--;
		snd_free_pci_page(sgbuf->pci, sgbuf->table[sgbuf->pages].buf,
				   sgbuf->table[sgbuf->pages].addr);
	}
	if (sgbuf->table)
		kfree(sgbuf->table);
	sgbuf->table = NULL;
	if (sgbuf->page_table)
		kfree(sgbuf->page_table);
	sgbuf->page_table = NULL;
	sgbuf->tblsize = 0;
	sgbuf->pages = 0;
	sgbuf->size = 0;
	
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
	struct snd_sg_buf *sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return NULL);

	unsigned int idx = offset >> PAGE_SHIFT;
	if (idx >= (unsigned int)sgbuf->pages)
		return NULL;
	return sgbuf->page_table[idx];
}


/**
 * snd_pcm_lib_preallocate_sg_pages - initialize SG-buffer for the PCI bus
 *
 * @pci: pci device
 * @substream: substream to assign the buffer
 *
 * Initializes SG-buffer for the PCI bus.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_lib_preallocate_sg_pages(struct pci_dev *pci,
				     snd_pcm_substream_t *substream)
{
	if ((substream->dma_private = snd_pcm_sgbuf_new(pci)) == NULL)
		return -ENOMEM;
	substream->dma_type = SNDRV_PCM_DMA_TYPE_PCI_SG;
	substream->dma_area = 0;
	substream->dma_addr = 0;
	substream->dma_bytes = 0;
	substream->buffer_bytes_max = UINT_MAX;
	substream->dma_max = 0;
	return 0;
}

/*
 * FIXME: the function name is too long for docbook!
 *
 * snd_pcm_lib_preallocate_sg_pages_for_all - initialize SG-buffer for the PCI bus (all substreams)
 * @pci: pci device
 * @pcm: pcm to assign the buffer
 *
 * Initialize the SG-buffer to all substreams of the given pcm for the
 * PCI bus.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_lib_preallocate_sg_pages_for_all(struct pci_dev *pci,
					     snd_pcm_t *pcm)
{
	snd_pcm_substream_t *substream;
	int stream, err;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			if ((err = snd_pcm_lib_preallocate_sg_pages(pci, substream)) < 0)
				return err;
	return 0;
}


/*
 *  Exported symbols
 */
EXPORT_SYMBOL(snd_pcm_lib_preallocate_sg_pages);
EXPORT_SYMBOL(snd_pcm_lib_preallocate_sg_pages_for_all);
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_page);
