/*
 *  scsi_merge.c Copyright (C) 1999 Eric Youngdale
 *
 *  SCSI queueing library.
 *      Initial versions: Eric Youngdale (eric@andante.org).
 *                        Based upon conversations with large numbers
 *                        of people at Linux Expo.
 *	Support for dynamic DMA mapping: Jakub Jelinek (jakub@redhat.com).
 *	Support for highmem I/O: Jens Axboe <axboe@suse.de>
 */

/*
 * This file contains queue management functions that are used by SCSI.
 * We need to ensure that commands do not grow so large that they cannot
 * be handled all at once by a host adapter.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>


#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/io.h>

#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>

/*
 * Function:    scsi_init_io()
 *
 * Purpose:     SCSI I/O initialize function.
 *
 * Arguments:   SCpnt   - Command descriptor we wish to initialize
 *
 * Returns:     1 on success.
 *
 * Lock status: 
 */
int scsi_init_io(Scsi_Cmnd *SCpnt)
{
	struct request     *req = SCpnt->request;
	struct scatterlist *sgpnt;
	int count, gfp_mask;

	/*
	 * First we need to know how many scatter gather segments are needed.
	 */
	count = req->nr_phys_segments;

	/*
	 * we used to not use scatter-gather for single segment request,
	 * but now we do (it makes highmem I/O easier to support without
	 * kmapping pages)
	 */
	SCpnt->use_sg = count;

	gfp_mask = GFP_NOIO;
	if (in_interrupt()) {
		gfp_mask &= ~__GFP_WAIT;
		gfp_mask |= __GFP_HIGH;
	}

	/*
	 * if sg table allocation fails, requeue request later.
	 */
	sgpnt = scsi_alloc_sgtable(SCpnt, gfp_mask);
	if (!sgpnt)
		return 0;

	SCpnt->request_buffer = (char *) sgpnt;
	SCpnt->request_bufflen = req->nr_sectors << 9;
	req->buffer = NULL;

	/* 
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	count = blk_rq_map_sg(req->q, req, SCpnt->request_buffer);

	/*
	 * mapped well, send it off
	 */
	if (count <= SCpnt->use_sg) {
		SCpnt->use_sg = count;
		return 1;
	}

	printk("Incorrect number of segments after building list\n");
	printk("counted %d, received %d\n", count, SCpnt->use_sg);
	printk("req nr_sec %lu, cur_nr_sec %u\n", req->nr_sectors, req->current_nr_sectors);

	/*
	 * kill it. there should be no leftover blocks in this request
	 */
	SCpnt = scsi_end_request(SCpnt, 0, req->nr_sectors);
	BUG_ON(SCpnt);
	return 0;
}

/*
 * Function:    scsi_initialize_merge_fn()
 *
 * Purpose:     Initialize merge function for a host
 *
 * Arguments:   SHpnt   - Host descriptor.
 *
 * Returns:     Nothing.
 *
 * Lock status: 
 *
 * Notes:
 */
void scsi_initialize_merge_fn(Scsi_Device * SDpnt)
{
	struct Scsi_Host *SHpnt = SDpnt->host;
	request_queue_t *q = &SDpnt->request_queue;
	u64 bounce_limit;

	/*
	 * The generic merging functions work just fine for us.
	 * Enable highmem I/O, if appropriate.
	 */
	bounce_limit = BLK_BOUNCE_HIGH;
	if (SHpnt->highmem_io) {
		if (!PCI_DMA_BUS_IS_PHYS)
			/* Platforms with virtual-DMA translation
 			 * hardware have no practical limit.
			 */
			bounce_limit = BLK_BOUNCE_ANY;
		else if (SHpnt->pci_dev)
			bounce_limit = SHpnt->pci_dev->dma_mask;
	} else if (SHpnt->unchecked_isa_dma)
		bounce_limit = BLK_BOUNCE_ISA;

	blk_queue_bounce_limit(q, bounce_limit);
}
