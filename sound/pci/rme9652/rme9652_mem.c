/* 
    ALSA memory allocation module for the RME Digi9652
  
 	Copyright(c) 1999 IEM - Winfried Ritsch
        Copyright (C) 1999 Paul Barton-Davis 

    This module is only needed if you compiled the rme9652 driver with
    the PREALLOCATE_MEMORY option. It allocates the memory need to
    run the board and holds it until the module is unloaded. Because
    we need 2 contiguous 1.6MB regions for the board, it can be
    a problem getting them once the system memory has become fairly
    fragmented. 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

    $Id: rme9652_mem.c,v 1.6 2002/02/04 10:21:33 tiwai Exp $


    Tue Oct 17 2000  Jaroslav Kysela <perex@suse.cz>
    	* space is allocated only for physical devices
        * added support for 2.4 kernels (pci_alloc_consistent)
    
*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <sound/initval.h>

#define RME9652_CARDS			8
#define RME9652_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define RME9652_CHANNEL_BUFFER_BYTES    (4*RME9652_CHANNEL_BUFFER_SAMPLES)

/* export */

static int snd_enable[8] = {1,1,1,1,1,1,1,1};
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(RME9652_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable cards to allocate buffers for.");

MODULE_AUTHOR("Winfried Ritsch, Paul Barton-Davis <pbd@op.net>");
MODULE_DESCRIPTION("Memory allocator for RME Hammerfall");
MODULE_CLASSES("{sound}");
MODULE_LICENSE("GPL");

/* Since we don't know at this point if we're allocating memory for a
   Hammerfall or a Hammerfall/Light, assume the worst and allocate
   space for the maximum number of channels.
		   
   See note in rme9652.h about why we allocate for an extra channel.  
*/

#define TOTAL_SIZE (26+1)*(RME9652_CHANNEL_BUFFER_BYTES)
#define NBUFS   2*RME9652_CARDS

#define RME9652_BUF_ALLOCATED 0x1
#define RME9652_BUF_USED      0x2

typedef struct rme9652_buf_stru rme9652_buf_t;

struct rme9652_buf_stru {
	struct pci_dev *pci;
	void *buf;
	dma_addr_t addr;
	char flags;
};

static rme9652_buf_t rme9652_buffers[NBUFS];

/* These are here so that we have absolutely no dependencies on any
   other modules. Dependencies can (1) cause us to lose in the rush
   for 2x 1.6MB chunks of contiguous memory and (2) make driver
   debugging difficult because unloading and reloading the snd module
   causes us to have to do the same for this one. Since on 2.2
   kernels, and before, we can rarely if ever allocate memory after
   starting things running, this would be bad.
*/

/* remove hack for pci_alloc_consistent to avoid dependecy on snd module */
#ifdef HACK_PCI_ALLOC_CONSISTENT
#undef pci_alloc_consistent
#endif

static void *rme9652_malloc_pages(struct pci_dev *pci,
				  unsigned long size,
				  dma_addr_t *dmaaddr)
{
	void *res;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 3, 0)
	res = (void *) pci_alloc_consistent(pci, size, dmaaddr);
#else
	int pg;
	for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
	res = (void *)__get_free_pages(GFP_KERNEL, pg);
	if (res != NULL)
		*dmaaddr = virt_to_bus(res);
#endif
	if (res != NULL) {
		struct page *page = virt_to_page(res);
		struct page *last_page = page + (size + PAGE_SIZE - 1) / PAGE_SIZE;
		while (page < last_page)
			set_bit(PG_reserved, &(page++)->flags);
	}
	return res;
}

static void rme9652_free_pages(struct pci_dev *pci, unsigned long size,
			       void *ptr, dma_addr_t dmaaddr)
{
	struct page *page, *last_page;

	if (ptr == NULL)
		return;
	page = virt_to_page(ptr);
	last_page = virt_to_page(ptr) + (size + PAGE_SIZE - 1) / PAGE_SIZE;
	while (page < last_page)
		clear_bit(PG_reserved, &(page++)->flags);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 3, 0)
	pci_free_consistent(pci, size, ptr, dmaaddr);
#else
	{
		int pg;
		for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);
		if (bus_to_virt(dmaaddr) != ptr) {
			printk(KERN_ERR "rme9652_free_pages: dmaaddr != ptr\n");
			return;
		}
		free_pages((unsigned long)ptr, pg);
	}
#endif
}

void *snd_rme9652_get_buffer (int card, dma_addr_t *dmaaddr)

{
	int i;
	rme9652_buf_t *rbuf;

	if (card < 0 || card >= RME9652_CARDS) {
		printk(KERN_ERR "snd_rme9652_get_buffer: card %d is out of range", card);
		return NULL;
	}
	for (i = card * 2; i < card * 2 + 2; i++) {
		rbuf = &rme9652_buffers[i];
		if (rbuf->flags == RME9652_BUF_ALLOCATED) {
			rbuf->flags |= RME9652_BUF_USED;
			MOD_INC_USE_COUNT;
			*dmaaddr = rbuf->addr;
			return rbuf->buf;
		}
	}

	return NULL;
}

void snd_rme9652_free_buffer (int card, void *addr)

{
	int i;
	rme9652_buf_t *rbuf;

	if (card < 0 || card >= RME9652_CARDS) {
		printk(KERN_ERR "snd_rme9652_get_buffer: card %d is out of range", card);
		return;
	}
	for (i = card * 2; i < card * 2 + 2; i++) {
		rbuf = &rme9652_buffers[i];
		if (rbuf->buf == addr) {
			MOD_DEC_USE_COUNT;
			rbuf->flags &= ~RME9652_BUF_USED;
			return;
		}
	}

	printk ("RME9652 memory allocator: unknown buffer address passed to free buffer");
}

static void __exit rme9652_free_buffers (void)

{
	int i;
	rme9652_buf_t *rbuf;

	for (i = 0; i < NBUFS; i++) {

		/* We rely on general module code to prevent
		   us from being unloaded with buffers in use.

		   However, not quite. Do not release memory
		   if it is still marked as in use. This might
		   be unnecessary.
		*/

		rbuf = &rme9652_buffers[i];

		if (rbuf->flags == RME9652_BUF_ALLOCATED) {
			rme9652_free_pages (rbuf->pci, TOTAL_SIZE, rbuf->buf, rbuf->addr);
			rbuf->buf = NULL;
			rbuf->flags = 0;
		}
	}
}				 

static int __init alsa_rme9652_mem_init(void)
{
	int i;
	struct pci_dev *pci;
	rme9652_buf_t *rbuf;

	/* make sure our buffer records are clean */

	for (i = 0; i < NBUFS; i++) {
		rbuf = &rme9652_buffers[i];
		rbuf->pci = NULL;
		rbuf->buf = NULL;
		rbuf->flags = 0;
	}

	/* ensure sane values for the number of buffers */

	/* Remember: 2 buffers per card, one for capture, one for
	   playback.
	*/
	
	i = 0;	/* card number */
	rbuf = rme9652_buffers;
	pci_for_each_dev(pci) {
		int k;
		if (pci->vendor != 0x10ee || pci->device != 0x3fc4)
			continue;

		if (!snd_enable[i])
			continue;

		for (k = 0; k < 2; ++k) {
			rbuf->buf = rme9652_malloc_pages(pci, TOTAL_SIZE, &rbuf->addr);
			if (rbuf->buf == NULL) {
				rme9652_free_buffers();
				printk(KERN_ERR "RME9652 memory allocator: no memory available for card %d buffer %d\n", i, k + 1);
				return -ENOMEM;
			}
			rbuf->flags = RME9652_BUF_ALLOCATED;
			rbuf++;
		}
		i++;
	}

	if (i == 0)
		printk(KERN_ERR "RME9652 memory allocator: no RME9652 card found...\n");
	
	return 0;
}

static void __exit alsa_rme9652_mem_exit(void)
{
	rme9652_free_buffers();
}

module_init(alsa_rme9652_mem_init)
module_exit(alsa_rme9652_mem_exit)

EXPORT_SYMBOL(snd_rme9652_get_buffer);
EXPORT_SYMBOL(snd_rme9652_free_buffer);
