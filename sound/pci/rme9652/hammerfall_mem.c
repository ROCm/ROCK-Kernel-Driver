/* 
    ALSA memory allocation module for the RME Digi9652
  
 	Copyright(c) 1999 IEM - Winfried Ritsch
        Copyright (C) 1999 Paul Barton-Davis 

    This module is only needed if you compiled the hammerfall driver with
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

    $Id: hammerfall_mem.c,v 1.9 2003/05/31 11:33:57 perex Exp $


    Tue Oct 17 2000  Jaroslav Kysela <perex@suse.cz>
    	* space is allocated only for physical devices
        * added support for 2.4 kernels (pci_alloc_consistent)
    
*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <sound/initval.h>

#define HAMMERFALL_CARDS			8
#define HAMMERFALL_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define HAMMERFALL_CHANNEL_BUFFER_BYTES    (4*HAMMERFALL_CHANNEL_BUFFER_SAMPLES)

/* export */

static int enable[8] = {1,1,1,1,1,1,1,1};
MODULE_PARM(enable, "1-" __MODULE_STRING(HAMMERFALL_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable cards to allocate buffers for.");

MODULE_AUTHOR("Winfried Ritsch, Paul Barton-Davis <pbd@op.net>");
MODULE_DESCRIPTION("Memory allocator for RME Hammerfall");
MODULE_CLASSES("{sound}");
MODULE_LICENSE("GPL");

/* Since we don't know at this point if we're allocating memory for a
   Hammerfall or a Hammerfall/Light, assume the worst and allocate
   space for the maximum number of channels.

   The extra channel is allocated because we need a 64kB-aligned
   buffer in the actual interface driver code (see rme9652.c or hdsp.c
   for details)
*/

#define TOTAL_SIZE (26+1)*(HAMMERFALL_CHANNEL_BUFFER_BYTES)
#define NBUFS   2*HAMMERFALL_CARDS

#define HAMMERFALL_BUF_ALLOCATED 0x1
#define HAMMERFALL_BUF_USED      0x2

typedef struct hammerfall_buf_stru hammerfall_buf_t;

struct hammerfall_buf_stru {
	struct pci_dev *pci;
	void *buf;
	dma_addr_t addr;
	char flags;
};

static hammerfall_buf_t hammerfall_buffers[NBUFS];

/* These are here so that we have absolutely no dependencies
   on any other modules. Dependencies can (1) cause us to
   lose in the rush for 2x1.6MB chunks of contiguous memory
   and (2) make driver debugging difficult because unloading
   and reloading the snd module causes us to have to do the
   same for this one. Since we can rarely if ever allocate
   memory after starting things running, that would be very
   undesirable.  
*/

static void *hammerfall_malloc_pages(struct pci_dev *pci,
				  unsigned long size,
				  dma_addr_t *dmaaddr)
{
	void *res;

	res = (void *) pci_alloc_consistent(pci, size, dmaaddr);
	if (res != NULL) {
		struct page *page = virt_to_page(res);
		struct page *last_page = page + (size + PAGE_SIZE - 1) / PAGE_SIZE;
		while (page < last_page)
			set_bit(PG_reserved, &(page++)->flags);
	}
	return res;
}

static void hammerfall_free_pages(struct pci_dev *pci, unsigned long size,
			       void *ptr, dma_addr_t dmaaddr)
{
	struct page *page, *last_page;

	if (ptr == NULL)
		return;
	page = virt_to_page(ptr);
	last_page = virt_to_page(ptr) + (size + PAGE_SIZE - 1) / PAGE_SIZE;
	while (page < last_page)
		clear_bit(PG_reserved, &(page++)->flags);
	pci_free_consistent(pci, size, ptr, dmaaddr);
}

void *snd_hammerfall_get_buffer (struct pci_dev *pcidev, dma_addr_t *dmaaddr)
{
	int i;
	hammerfall_buf_t *rbuf;

	for (i = 0; i < NBUFS; i++) {
		rbuf = &hammerfall_buffers[i];
		if (rbuf->flags == HAMMERFALL_BUF_ALLOCATED) {
			rbuf->flags |= HAMMERFALL_BUF_USED;
			rbuf->pci = pcidev;
			*dmaaddr = rbuf->addr;
			return rbuf->buf;
		}
	}

	return NULL;
}

void snd_hammerfall_free_buffer (struct pci_dev *pcidev, void *addr)
{
	int i;
	hammerfall_buf_t *rbuf;

	for (i = 0; i < NBUFS; i++) {
		rbuf = &hammerfall_buffers[i];
		if (rbuf->buf == addr && rbuf->pci == pcidev) {
			rbuf->flags &= ~HAMMERFALL_BUF_USED;
			return;
		}
	}

	printk ("Hammerfall memory allocator: unknown buffer address or PCI device ID");
}

static void hammerfall_free_buffers (void)

{
	int i;
	hammerfall_buf_t *rbuf;

	for (i = 0; i < NBUFS; i++) {

		/* We rely on general module code to prevent
		   us from being unloaded with buffers in use.

		   However, not quite. Do not release memory
		   if it is still marked as in use. This might
		   be unnecessary.
		*/

		rbuf = &hammerfall_buffers[i];

		if (rbuf->flags == HAMMERFALL_BUF_ALLOCATED) {
			hammerfall_free_pages (rbuf->pci, TOTAL_SIZE, rbuf->buf, rbuf->addr);
			rbuf->buf = NULL;
			rbuf->flags = 0;
		}
	}
}				 

static int __init alsa_hammerfall_mem_init(void)
{
	int i;
	struct pci_dev *pci;
	hammerfall_buf_t *rbuf;

	/* make sure our buffer records are clean */

	for (i = 0; i < NBUFS; i++) {
		rbuf = &hammerfall_buffers[i];
		rbuf->pci = NULL;
		rbuf->buf = NULL;
		rbuf->flags = 0;
	}

	/* ensure sane values for the number of buffers */

	/* Remember: 2 buffers per card, one for capture, one for
	   playback.
	*/
	
	i = 0;	/* card number */
	rbuf = hammerfall_buffers;
	pci_for_each_dev(pci) {
		int k;
		
		/* check for Hammerfall and Hammerfall DSP cards */

		if (pci->vendor != 0x10ee || (pci->device != 0x3fc4 && pci->device != 0x3fc5)) 
			continue;

		if (!enable[i])
			continue;

		for (k = 0; k < 2; ++k) {
			rbuf->buf = hammerfall_malloc_pages(pci, TOTAL_SIZE, &rbuf->addr);
			if (rbuf->buf == NULL) {
				hammerfall_free_buffers();
				printk(KERN_ERR "Hammerfall memory allocator: no memory available for card %d buffer %d\n", i, k + 1);
				return -ENOMEM;
			}
			rbuf->flags = HAMMERFALL_BUF_ALLOCATED;
			rbuf++;
		}
		i++;
	}

	if (i == 0)
		printk(KERN_ERR "Hammerfall memory allocator: "
		       "no Hammerfall cards found...\n");
	else
		printk(KERN_ERR "Hammerfall memory allocator: "
		       "buffers allocated for %d cards\n", i);

	return 0;
}

static void __exit alsa_hammerfall_mem_exit(void)
{
	hammerfall_free_buffers();
}

module_init(alsa_hammerfall_mem_init)
module_exit(alsa_hammerfall_mem_exit)

EXPORT_SYMBOL(snd_hammerfall_get_buffer);
EXPORT_SYMBOL(snd_hammerfall_free_buffer);
