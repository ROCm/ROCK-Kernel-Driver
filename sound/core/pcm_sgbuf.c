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

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_sgbuf.h>


/* table entries are align to 32 */
#define SGBUF_TBL_ALIGN		32
#define sgbuf_align_table(tbl)	((((tbl) + SGBUF_TBL_ALIGN - 1) / SGBUF_TBL_ALIGN) * SGBUF_TBL_ALIGN)

/*
 * shrink to the given pages.
 * free the unused pages
 */
static void sgbuf_shrink(struct snd_sg_buf *sgbuf, int pages)
{
	snd_assert(sgbuf, return);
	if (! sgbuf->table)
		return;
	while (sgbuf->pages > pages) {
		sgbuf->pages--;
		snd_free_pci_pages(sgbuf->pci, PAGE_SIZE,
				   sgbuf->table[sgbuf->pages].buf,
				   sgbuf->table[sgbuf->pages].addr);
	}
}

/*
 * initialize the sg buffer
 * assigned to substream->dma_private.
 * initialize the table with the given size.
 */
int snd_pcm_sgbuf_init(snd_pcm_substream_t *substream, struct pci_dev *pci, int tblsize)
{
	struct snd_sg_buf *sgbuf;

	tblsize = sgbuf_align_table(tblsize);
	sgbuf = snd_magic_kcalloc(snd_pcm_sgbuf_t, 0, GFP_KERNEL);
	if (! sgbuf)
		return -ENOMEM;
	substream->dma_private = sgbuf;
	sgbuf->pci = pci;
	sgbuf->pages = 0;
	sgbuf->tblsize = tblsize;
	sgbuf->table = kmalloc(sizeof(struct snd_sg_page) * tblsize, GFP_KERNEL);
	if (! sgbuf->table) {
		snd_pcm_sgbuf_free(substream);
		return -ENOMEM;
	}
	memset(sgbuf->table, 0, sizeof(struct snd_sg_page) * tblsize);
	return 0;
}

/*
 * release all pages and free the sgbuf instance
 */
int snd_pcm_sgbuf_delete(snd_pcm_substream_t *substream)
{
	struct snd_sg_buf *sgbuf;

	sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return -EINVAL);
	sgbuf_shrink(sgbuf, 0);
	if (sgbuf->table)
		kfree(sgbuf->table);
	snd_magic_kfree(sgbuf);
	substream->dma_private = NULL;
	return 0;
}

/*
 * allocate sg buffer table with the given byte size.
 * if the buffer table already exists, try to resize it.
 * call this from hw_params callback.
 */
int snd_pcm_sgbuf_alloc(snd_pcm_substream_t *substream, size_t size)
{
	struct snd_sg_buf *sgbuf;
	unsigned int pages;
	unsigned int tblsize;
	int changed = 0;

	sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return -EINVAL);
	pages = snd_pcm_sgbuf_pages(size);
	tblsize = sgbuf_align_table(pages);
	if (pages < sgbuf->pages) {
		/* release unsed pages */
		sgbuf_shrink(sgbuf, pages);
		substream->runtime->dma_bytes = size;
		return 1; /* changed */
	} else if (pages > sgbuf->tblsize) {
		/* bigger than existing one.  reallocate the table. */
		struct snd_sg_page *table;
		table = kmalloc(sizeof(*table) * tblsize, GFP_KERNEL);
		if (! table)
			return -ENOMEM;
		memcpy(table, sgbuf->table, sizeof(*table) * sgbuf->tblsize);
		kfree(sgbuf->table);
		sgbuf->table = table;
		sgbuf->tblsize = tblsize;
	}
	/* allocate each page */
	while (sgbuf->pages < pages) {
		void *ptr;
		dma_addr_t addr;
		ptr = snd_malloc_pci_pages(sgbuf->pci, PAGE_SIZE, &addr);
		if (! ptr)
			return -ENOMEM;
		sgbuf->table[sgbuf->pages].buf = ptr;
		sgbuf->table[sgbuf->pages].addr = addr;
		sgbuf->pages++;
		changed = 1;
	}
	sgbuf->size = size;
	substream->runtime->dma_bytes = size;
	return changed;
}

/*
 * free the sg buffer
 * the table is kept.
 * call this from hw_free callback.
 */
int snd_pcm_sgbuf_free(snd_pcm_substream_t *substream)
{
	struct snd_sg_buf *sgbuf;

	sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return -EINVAL);
	sgbuf_shrink(sgbuf, 0);
	return 0;
}

/*
 * get the page pointer on the given offset
 * used as the page callback of pcm ops
 */
void *snd_pcm_sgbuf_ops_page(snd_pcm_substream_t *substream, unsigned long offset)
{
	struct snd_sg_buf *sgbuf;
	unsigned int idx;

	sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return NULL);
	idx = offset >> PAGE_SHIFT;
	if (idx >= sgbuf->pages)
		return 0;
	return sgbuf->table[idx].buf;
}

/*
 * do copy_from_user to the sg buffer
 */
static int copy_from_user_sg_buf(snd_pcm_substream_t *substream,
				 char *buf, size_t hwoff, ssize_t bytes)
{
	int len;
	char *addr;
	size_t p = (hwoff >> PAGE_SHIFT) << PAGE_SHIFT;
	hwoff -= p;
	len = PAGE_SIZE - hwoff;
	for (;;) {
		addr = snd_pcm_sgbuf_ops_page(substream, p);
		if (! addr)
			return -EFAULT;
		if (len > bytes)
			len = bytes;
		if (copy_from_user(addr + hwoff, buf, len))
			return -EFAULT;
		bytes -= len;
		if (bytes <= 0)
			break;
		buf += len;
		p += PAGE_SIZE;
		len = PAGE_SIZE;
		hwoff = 0;
	}
	return 0;
}

/*
 * do copy_to_user from the sg buffer
 */
static int copy_to_user_sg_buf(snd_pcm_substream_t *substream,
			       char *buf, size_t hwoff, ssize_t bytes)
{
	int len;
	char *addr;
	size_t p = (hwoff >> PAGE_SHIFT) << PAGE_SHIFT;
	hwoff -= p;
	len = PAGE_SIZE - hwoff;
	for (;;) {
		addr = snd_pcm_sgbuf_ops_page(substream, p);
		if (! addr)
			return -EFAULT;
		if (len > bytes)
			len = bytes;
		if (copy_to_user(buf, addr + hwoff, len))
			return -EFAULT;
		bytes -= len;
		if (bytes <= 0)
			break;
		buf += len;
		p += PAGE_SIZE;
		len = PAGE_SIZE;
		hwoff = 0;
	}
	return 0;
}

/*
 * set silence on the sg buffer
 */
static int set_silence_sg_buf(snd_pcm_substream_t *substream,
			      size_t hwoff, ssize_t samples)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int len, page_len;
	char *addr;
	size_t p = (hwoff >> PAGE_SHIFT) << PAGE_SHIFT;
	hwoff -= p;
	len = bytes_to_samples(substream->runtime, PAGE_SIZE - hwoff);
	page_len = bytes_to_samples(substream->runtime, PAGE_SIZE);
	for (;;) {
		addr = snd_pcm_sgbuf_ops_page(substream, p);
		if (! addr)
			return -EFAULT;
		if (len > samples)
			len = samples;
		snd_pcm_format_set_silence(runtime->format, addr + hwoff, len);
		samples -= len;
		if (samples <= 0)
			break;
		p += PAGE_SIZE;
		len = page_len;
		hwoff = 0;
	}
	return 0;
}

/*
 * copy callback for playback pcm ops
 */
int snd_pcm_sgbuf_ops_copy_playback(snd_pcm_substream_t *substream, int channel,
				    snd_pcm_uframes_t hwoff, void *buf, snd_pcm_uframes_t count)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (channel < 0) {
		return copy_from_user_sg_buf(substream, buf, frames_to_bytes(runtime, hwoff), frames_to_bytes(runtime, count));
	} else {
		size_t dma_csize = runtime->dma_bytes / runtime->channels;
		size_t c_ofs = (channel * dma_csize) + samples_to_bytes(runtime, hwoff);
		return copy_from_user_sg_buf(substream, buf, c_ofs, samples_to_bytes(runtime, count));
	}
}

/*
 * copy callback for capture pcm ops
 */
int snd_pcm_sgbuf_ops_copy_capture(snd_pcm_substream_t *substream, int channel,
				   snd_pcm_uframes_t hwoff, void *buf, snd_pcm_uframes_t count)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (channel < 0) {
		return copy_to_user_sg_buf(substream, buf, frames_to_bytes(runtime, hwoff), frames_to_bytes(runtime, count));
	} else {
		size_t dma_csize = runtime->dma_bytes / runtime->channels;
		size_t c_ofs = (channel * dma_csize) + samples_to_bytes(runtime, hwoff);
		return copy_to_user_sg_buf(substream, buf, c_ofs, samples_to_bytes(runtime, count));
	}
}

/*
 * silence callback for pcm ops
 */
int snd_pcm_sgbuf_ops_silence(snd_pcm_substream_t *substream, int channel,
			      snd_pcm_uframes_t hwoff, snd_pcm_uframes_t count)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (channel < 0) {
		return set_silence_sg_buf(substream, frames_to_bytes(runtime, hwoff),
					  frames_to_bytes(runtime, count));
	} else {
		size_t dma_csize = runtime->dma_bytes / runtime->channels;
		size_t c_ofs = (channel * dma_csize) + samples_to_bytes(runtime, hwoff);
		return set_silence_sg_buf(substream, c_ofs, samples_to_bytes(runtime, count));
	}
}


/*
 *  Exported symbols
 */
EXPORT_SYMBOL(snd_pcm_sgbuf_init);
EXPORT_SYMBOL(snd_pcm_sgbuf_delete);
EXPORT_SYMBOL(snd_pcm_sgbuf_alloc);
EXPORT_SYMBOL(snd_pcm_sgbuf_free);
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_copy_playback);
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_copy_capture);
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_silence);
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_page);
