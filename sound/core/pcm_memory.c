/*
 *  Digital Audio (PCM) abstract layer
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

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/initval.h>

static int snd_preallocate_dma = 1;
MODULE_PARM(snd_preallocate_dma, "i");
MODULE_PARM_DESC(snd_preallocate_dma, "Preallocate DMA memory when the PCM devices are initialized.");
MODULE_PARM_SYNTAX(snd_preallocate_dma, SNDRV_BOOLEAN_TRUE_DESC);

static int snd_maximum_substreams = 4;
MODULE_PARM(snd_maximum_substreams, "i");
MODULE_PARM_DESC(snd_maximum_substreams, "Maximum substreams with preallocated DMA memory.");
MODULE_PARM_SYNTAX(snd_maximum_substreams, SNDRV_BOOLEAN_TRUE_DESC);

static int snd_minimum_buffer = 16384;


static void snd_pcm_lib_preallocate_dma_free(snd_pcm_substream_t *substream)
{
	if (substream->dma_area == NULL)
		return;
	switch (substream->dma_type) {
	case SNDRV_PCM_DMA_TYPE_CONTINUOUS:
		snd_free_pages(substream->dma_area, substream->dma_bytes);
		break;
#ifdef CONFIG_ISA
	case SNDRV_PCM_DMA_TYPE_ISA:
		snd_free_isa_pages(substream->dma_bytes, substream->dma_area, substream->dma_addr);
		break;
#endif
#ifdef CONFIG_PCI
	case SNDRV_PCM_DMA_TYPE_PCI:
		snd_free_pci_pages((struct pci_dev *)substream->dma_private, substream->dma_bytes, substream->dma_area, substream->dma_addr);
		break;
#endif
#ifdef CONFIG_SBUS
	case SNDRV_PCM_DMA_TYPE_SBUS:
		snd_free_sbus_pages((struct sbus_dev *)substream->dma_private, substream->dma_bytes, substream->dma_area, substream->dma_addr);
		break;
#endif
	}
	substream->dma_area = NULL;
}

int snd_pcm_lib_preallocate_free(snd_pcm_substream_t *substream)
{
	snd_pcm_lib_preallocate_dma_free(substream);
	if (substream->proc_prealloc_entry) {
		snd_info_unregister(substream->proc_prealloc_entry);
		substream->proc_prealloc_entry = NULL;
	}
	return 0;
}

int snd_pcm_lib_preallocate_free_for_all(snd_pcm_t *pcm)
{
	snd_pcm_substream_t *substream;
	int stream;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			snd_pcm_lib_preallocate_free(substream);
	return 0;
}

static void snd_pcm_lib_preallocate_proc_read(snd_info_entry_t *entry,
					      snd_info_buffer_t *buffer)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)entry->private_data;
	snd_iprintf(buffer, "%lu\n", (unsigned long) substream->dma_bytes / 1024);
}

static void snd_pcm_lib_preallocate_proc_write(snd_info_entry_t *entry,
					       snd_info_buffer_t *buffer)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)entry->private_data;
	char line[64], str[64];
	size_t size;
	void *dma_area;
	dma_addr_t dma_addr;

	if (substream->runtime) {
		buffer->error = -EBUSY;
		return;
	}
	if (!snd_info_get_line(buffer, line, sizeof(line))) {
		snd_info_get_str(str, line, sizeof(str));
		size = simple_strtoul(str, NULL, 10) * 1024;
		if ((size != 0 && size < 8192) || size > substream->dma_max) {
			buffer->error = -EINVAL;
			return;
		}
		if (substream->dma_bytes == size)
			return;
		if (size > 0) {
			switch (substream->dma_type) {
			case SNDRV_PCM_DMA_TYPE_CONTINUOUS:
				dma_area = snd_malloc_pages(size, (unsigned int)((unsigned long)substream->dma_private & 0xffffffff));
				dma_addr = 0UL;		/* not valid */
				break;
#ifdef CONFIG_ISA
			case SNDRV_PCM_DMA_TYPE_ISA:
				dma_area = snd_malloc_isa_pages(size, &dma_addr);
				break;
#endif
#ifdef CONFIG_PCI
			case SNDRV_PCM_DMA_TYPE_PCI:
				dma_area = snd_malloc_pci_pages((struct pci_dev *)substream->dma_private, size, &dma_addr);
				break;
#endif
#ifdef CONFIG_SBUS
			case SNDRV_PCM_DMA_TYPE_SBUS:
				dma_area = snd_malloc_sbus_pages((struct sbus_dev *)substream->dma_private, size, &dma_addr);
				break;
#endif
			default:
				dma_area = NULL;
				dma_addr = 0UL;
			}
			if (dma_area == NULL) {
				buffer->error = -ENOMEM;
				return;
			}
			substream->buffer_bytes_max = size;
		} else {
			dma_area = NULL;
			substream->buffer_bytes_max = UINT_MAX;
		}
		snd_pcm_lib_preallocate_dma_free(substream);
		substream->dma_area = dma_area;
		substream->dma_addr = dma_addr;
		substream->dma_bytes = size;
	} else {
		buffer->error = -EINVAL;
	}
}

static int snd_pcm_lib_preallocate_pages1(snd_pcm_substream_t *substream,
					  size_t size, size_t max)
{
	unsigned long rsize = 0;
	void *dma_area = NULL;
	dma_addr_t dma_addr = 0UL;
	snd_info_entry_t *entry;

	if (!size || !snd_preallocate_dma || substream->number >= snd_maximum_substreams) {
		size = 0;
	} else {
		switch (substream->dma_type) {
		case SNDRV_PCM_DMA_TYPE_CONTINUOUS:
			dma_area = snd_malloc_pages_fallback(size, (unsigned int)((unsigned long)substream->dma_private & 0xffffffff), &rsize);
			dma_addr = 0UL;		/* not valid */
			break;
#ifdef CONFIG_ISA
		case SNDRV_PCM_DMA_TYPE_ISA:
			dma_area = snd_malloc_isa_pages_fallback(size, &dma_addr, &rsize);
			break;
#endif
#ifdef CONFIG_PCI
		case SNDRV_PCM_DMA_TYPE_PCI:
			dma_area = snd_malloc_pci_pages_fallback((struct pci_dev *)substream->dma_private, size, &dma_addr, &rsize);
			break;
#endif
#ifdef CONFIG_SBUS
		case SNDRV_PCM_DMA_TYPE_SBUS:
			dma_area = snd_malloc_sbus_pages_fallback((struct sbus_dev *)substream->dma_private, size, &dma_addr, &rsize);
			break;
#endif
		default:
			size = 0;
		}
		if (rsize < snd_minimum_buffer) {
			snd_pcm_lib_preallocate_dma_free(substream);
			size = 0;
		}
	}
	substream->dma_area = dma_area;
	substream->dma_addr = dma_addr;
	substream->dma_bytes = rsize;
	if (rsize > 0)
		substream->buffer_bytes_max = rsize;
	substream->dma_max = max;
	if ((entry = snd_info_create_card_entry(substream->pcm->card, "prealloc", substream->proc_root)) != NULL) {
		entry->c.text.read_size = 64;
		entry->c.text.read = snd_pcm_lib_preallocate_proc_read;
		entry->c.text.write_size = 64;
		entry->c.text.write = snd_pcm_lib_preallocate_proc_write;
		entry->private_data = substream;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_prealloc_entry = entry;
	return 0;
}

int snd_pcm_lib_preallocate_pages(snd_pcm_substream_t *substream,
				      size_t size, size_t max,
				      unsigned int flags)
{
	substream->dma_type = SNDRV_PCM_DMA_TYPE_CONTINUOUS;
	substream->dma_private = (void *)(unsigned long)flags;
	return snd_pcm_lib_preallocate_pages1(substream, size, max);
}

int snd_pcm_lib_preallocate_pages_for_all(snd_pcm_t *pcm,
					  size_t size, size_t max,
					  unsigned int flags)
{
	snd_pcm_substream_t *substream;
	int stream, err;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			if ((err = snd_pcm_lib_preallocate_pages(substream, size, max, flags)) < 0)
				return err;
	return 0;
}

#ifdef CONFIG_ISA
int snd_pcm_lib_preallocate_isa_pages(snd_pcm_substream_t *substream,
				      size_t size, size_t max)
{
	substream->dma_type = SNDRV_PCM_DMA_TYPE_ISA;
	substream->dma_private = NULL;
	return snd_pcm_lib_preallocate_pages1(substream, size, max);
}

int snd_pcm_lib_preallocate_isa_pages_for_all(snd_pcm_t *pcm,
					      size_t size, size_t max)
{
	snd_pcm_substream_t *substream;
	int stream, err;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			if ((err = snd_pcm_lib_preallocate_isa_pages(substream, size, max)) < 0)
				return err;
	return 0;
}
#endif /* CONFIG_ISA */

int snd_pcm_lib_malloc_pages(snd_pcm_substream_t *substream, size_t size)
{
	snd_pcm_runtime_t *runtime;
	void *dma_area = NULL;
	dma_addr_t dma_addr = 0UL;

	snd_assert(substream != NULL, return -EINVAL);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EINVAL);	
	if (runtime->dma_area != NULL) {
		/* perphaps, we might free the large DMA memory region
		   to save some space here, but the actual solution
		   costs us less time */
		if (runtime->dma_bytes >= size)
			return 0;	/* ok, do not change */
      		snd_pcm_lib_free_pages(substream);
	}
	if (substream->dma_area != NULL && substream->dma_bytes >= size) {
		dma_area = substream->dma_area;
		dma_addr = substream->dma_addr;
	} else {
		switch (substream->dma_type) {
		case SNDRV_PCM_DMA_TYPE_CONTINUOUS:
			dma_area = snd_malloc_pages(size, (unsigned int)((unsigned long)substream->dma_private & 0xffffffff));
			dma_addr = 0UL;		/* not valid */
			break;
#ifdef CONFIG_ISA
		case SNDRV_PCM_DMA_TYPE_ISA:
			dma_area = snd_malloc_isa_pages(size, &dma_addr); 
			break;
#endif
#ifdef CONFIG_PCI
		case SNDRV_PCM_DMA_TYPE_PCI:
			dma_area = snd_malloc_pci_pages((struct pci_dev *)substream->dma_private, size, &dma_addr);
			break;
#endif
#ifdef CONFIG_SBUS
		case SNDRV_PCM_DMA_TYPE_SBUS:
			dma_area = snd_malloc_sbus_pages((struct sbus_dev *)substream->dma_private, size, &dma_addr);
			break;
#endif
		default:
			return -ENXIO;
		}
	}
	if (! dma_area)
		return -ENOMEM;
	runtime->dma_area = dma_area;
	runtime->dma_addr = dma_addr;
	runtime->dma_bytes = size;
	return 1;			/* area was changed */
}

int snd_pcm_lib_free_pages(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime;

	snd_assert(substream != NULL, return -EINVAL);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EINVAL);
	if (runtime->dma_area == NULL)
		return 0;
	if (runtime->dma_area != substream->dma_area) {
		switch (substream->dma_type) {
#ifdef CONFIG_ISA
		case SNDRV_PCM_DMA_TYPE_ISA:
			snd_free_isa_pages(runtime->dma_bytes, runtime->dma_area, runtime->dma_addr);
			break;
#endif
#ifdef CONFIG_PCI
		case SNDRV_PCM_DMA_TYPE_PCI:
			snd_free_pci_pages((struct pci_dev *)substream->dma_private, runtime->dma_bytes, runtime->dma_area, runtime->dma_addr);
			break;
#endif
#ifdef CONFIG_SBUS
		case SNDRV_PCM_DMA_TYPE_SBUS:
			snd_free_sbus_pages((struct sbus_dev *)substream->dma_private, runtime->dma_bytes, runtime->dma_area, runtime->dma_addr);
			break;
#endif
		}
	}
	runtime->dma_area = NULL;
	runtime->dma_addr = 0UL;
	runtime->dma_bytes = 0;
	return 0;
}

#ifdef CONFIG_PCI

int snd_pcm_lib_preallocate_pci_pages(struct pci_dev *pci,
				      snd_pcm_substream_t *substream,
				      size_t size, size_t max)
{
	substream->dma_type = SNDRV_PCM_DMA_TYPE_PCI;
	substream->dma_private = pci;
	return snd_pcm_lib_preallocate_pages1(substream, size, max);
}

int snd_pcm_lib_preallocate_pci_pages_for_all(struct pci_dev *pci,
					      snd_pcm_t *pcm,
					      size_t size, size_t max)
{
	snd_pcm_substream_t *substream;
	int stream, err;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			if ((err = snd_pcm_lib_preallocate_pci_pages(pci, substream, size, max)) < 0)
				return err;
	return 0;
}

#endif /* CONFIG_PCI */

#ifdef CONFIG_SBUS

int snd_pcm_lib_preallocate_sbus_pages(struct sbus_dev *sdev,
				       snd_pcm_substream_t *substream,
				       size_t size, size_t max)
{
	substream->dma_type = SNDRV_PCM_DMA_TYPE_SBUS;
	substream->dma_private = sdev;
	return snd_pcm_lib_preallocate_pages1(substream, size, max);
}

int snd_pcm_lib_preallocate_sbus_pages_for_all(struct sbus_dev *sdev,
					       snd_pcm_t *pcm,
					       size_t size, size_t max)
{
	snd_pcm_substream_t *substream;
	int stream, err;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			if ((err = snd_pcm_lib_preallocate_sbus_pages(sdev, substream, size, max)) < 0)
				return err;
	return 0;
}

#endif /* CONFIG_SBUS */
