#ifndef __SOUND_PCM_SGBUF_H
#define __SOUND_PCM_SGBUF_H

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

struct snd_sg_page {
	void *buf;
	dma_addr_t addr;
};

struct snd_sg_buf {
	int size;	/* allocated byte size (= runtime->dma_bytes) */
	int pages;	/* allocated pages */
	int tblsize;	/* allocated table size */
	struct snd_sg_page *table;
	struct page **page_table;
	struct pci_dev *pci;
};

typedef struct snd_sg_buf snd_pcm_sgbuf_t; /* for magic cast */

/*
 * return the pages matching with the given byte size
 */
static inline unsigned int snd_pcm_sgbuf_pages(size_t size)
{
	return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

/*
 * return the physical address at the corresponding offset
 */
static inline dma_addr_t snd_pcm_sgbuf_get_addr(struct snd_sg_buf *sgbuf, size_t offset)
{
	return sgbuf->table[offset >> PAGE_SHIFT].addr + offset % PAGE_SIZE;
}

struct snd_sg_buf *snd_pcm_sgbuf_init(struct pci_dev *pci);
void snd_pcm_sgbuf_delete(struct snd_sg_buf *sgbuf);
void *snd_pcm_sgbuf_alloc_pages(struct snd_sg_buf *sgbuf, size_t size);
int snd_pcm_sgbuf_free_pages(struct snd_sg_buf *sgbuf, void *vmaddr);

int snd_pcm_lib_preallocate_sg_pages(struct pci_dev *pci, snd_pcm_substream_t *substream);
int snd_pcm_lib_preallocate_sg_pages_for_all(struct pci_dev *pci, snd_pcm_t *pcm);

#define _snd_pcm_substream_sgbuf(substream) ((substream)->dma_private)
#define snd_pcm_substream_sgbuf(substream) snd_magic_cast(snd_pcm_sgbuf_t, _snd_pcm_substream_sgbuf(substream), return -ENXIO)

struct page *snd_pcm_sgbuf_ops_page(snd_pcm_substream_t *substream, unsigned long offset);

#endif /* __SOUND_PCM_SGBUF_H */
