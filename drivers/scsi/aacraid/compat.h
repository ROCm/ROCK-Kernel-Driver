/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*
 * This file is for backwards compatibility with older kernel versions
 */
#include <linux/version.h>
#if LINUX_VERSION_CODE  < KERNEL_VERSION(2,4,11)   
#include <linux/blk.h>
static inline unsigned int block_size(kdev_t dev)
{
	int retval = BLOCK_SIZE;
	int major = MAJOR(dev);

	if (blksize_size[major]) {
		int minor = MINOR(dev);
		if (blksize_size[major][minor])
			retval = blksize_size[major][minor];
	}
	return retval;
}
#endif

#if LINUX_VERSION_CODE  <= KERNEL_VERSION(2,4,7)   

#ifndef COMPLETION_INITIALIZER

#include <linux/wait.h>

struct completion {
	unsigned int done;
	wait_queue_head_t wait;
};
#define COMPLETION_INITIALIZER(work) \
	{ 0, __WAIT_QUEUE_HEAD_INITIALIZER((work).wait) }

#define DECLARE_COMPLETION(work) \
	struct completion work = COMPLETION_INITIALIZER(work)
#define INIT_COMPLETION(x)	((x).done = 0)

static inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_waitqueue_head(&x->wait);
}
#endif

#ifndef complete_and_exit
static inline void complete_and_exit(struct completion *comp, long code)
{
	/*
	if (comp)
		complete(comp);
	
	do_exit(code);
	*/
}
#endif

#endif

#if LINUX_VERSION_CODE  <= KERNEL_VERSION(2,4,2)   

static inline void scsi_set_pci_device(struct Scsi_Host *SHpnt,
                                       struct pci_dev *pdev)
{
//	SHpnt->pci_dev = pdev;
}

static inline void wait_for_completion(struct completion *x)
{
	spin_lock_irq(&x->wait.lock);
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			__set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&x->wait.lock);
			schedule();
			spin_lock_irq(&x->wait.lock);
		} while (!x->done);
		__remove_wait_queue(&x->wait, &wait);
	}
	x->done--;
	spin_unlock_irq(&x->wait.lock);
}

static inline int pci_set_dma_mask(struct pci_dev *dev, dma_addr_t mask)
{
    dev->dma_mask = mask;

    return 0;
}

#endif
    
