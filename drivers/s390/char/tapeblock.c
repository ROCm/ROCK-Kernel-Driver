/***************************************************************************
 *
 *  drivers/s390/char/tapeblock.c
 *    block device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 *
 ****************************************************************************
 */

#include "tapedefs.h"
#include <linux/config.h>
#include <linux/blkdev.h>
#include <linux/blk.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <asm/debug.h>
#include <asm/s390dyn.h>
#include <linux/compatmac.h>
#ifdef MODULE
#define __NO_VERSION__
#include <linux/module.h>
#endif
#include "tape.h"
#include "tapeblock.h"

#define PRINTK_HEADER "TBLOCK:"

/*
 * file operation structure for tape devices
 */
static struct block_device_operations tapeblock_fops = {
	.owner		= THIS_MODULE,
	.open		= tapeblock_open,
	.release	= tapeblock_release,
};

int    tapeblock_major = 0;

static void tape_request_fn (request_queue_t * queue);
static request_queue_t* tapeblock_getqueue (kdev_t kdev);

#ifdef CONFIG_DEVFS_FS
devfs_handle_t
tapeblock_mkdevfstree (tape_dev_t* td) {
    devfs_handle_t rc=NULL;
    rc=td->blk_data.devfs_block_dir=devfs_mk_dir (td->devfs_dir, "block", td);
    if (rc==NULL) goto out_undo;
    rc=td->blk_data.devfs_disc=devfs_register(td->blk_data.devfs_block_dir, "disc",DEVFS_FL_DEFAULT,
				    tapeblock_major, td->first_minor,
				    TAPEBLOCK_DEVFSMODE, &tapeblock_fops, td);
    if (rc==NULL) goto out_undo;
    goto out;
 out_undo:
    tapeblock_rmdevfstree(td);
 out:
    return rc;
}

void
tapeblock_rmdevfstree (tape_dev_t* td) {
    if (td->blk_data.devfs_disc)
            devfs_unregister(td->blk_data.devfs_disc);
    if (td->blk_data.devfs_block_dir)
            devfs_unregister(td->blk_data.devfs_block_dir);
}
#endif

void 
tapeblock_setup(tape_dev_t* td) {
    blk_queue_hardsect_size(&ti->request_queue, 2048);
    blk_init_queue (&td->blk_data.request_queue, tape_request_fn); 
#ifdef CONFIG_DEVFS_FS
    tapeblock_mkdevfstree(td);
#endif
    set_device_ro (MKDEV(tapeblock_major, td->first_minor), 1);
}

int
tapeblock_init(void) {
	int result;
	tape_frontend_t* blkfront,*temp;
	tape_dev_t* td;
	tape_init();
	/* Register the tape major number to the kernel */
	result = register_blkdev(tapeblock_major, "tBLK", &tapeblock_fops);
	if (result < 0) {
		PRINT_WARN(KERN_ERR "tape: can't get major %d for block device\n", tapeblock_major);
		result=-ENODEV;
		goto out;
	}
	if (tapeblock_major == 0) tapeblock_major = result;   /* accept dynamic major number*/
	INIT_BLK_DEV(tapeblock_major,tape_request_fn,tapeblock_getqueue,NULL);
	PRINT_WARN(KERN_ERR " tape gets major %d for block device\n", tapeblock_major);
	max_sectors[tapeblock_major] = (int*) kmalloc (256*sizeof(int),GFP_KERNEL);
	if (max_sectors[tapeblock_major]==NULL) goto out_undo_hardsect_size;
	memset(max_sectors[tapeblock_major],0,256*sizeof(int));
	blkfront = kmalloc(sizeof(tape_frontend_t),GFP_KERNEL);
	if (blkfront==NULL) goto out_undo_max_sectors;
	blkfront->device_setup=(tape_setup_device_t)tapeblock_setup;
#ifdef CONFIG_DEVFS_FS
	blkfront->mkdevfstree = tapeblock_mkdevfstree;
	blkfront->rmdevfstree = tapeblock_rmdevfstree;
#endif
	blkfront->next=NULL;
	if (tape_first_front==NULL) {
		tape_first_front=blkfront;
	} else {
		temp=tape_first_front;
		while (temp->next!=NULL) 
			temp=temp->next;
		temp->next=blkfront;
	}
	td=tape_first_dev;
	while (td!=NULL) {
		tapeblock_setup(td);
		td=td->next;
	}
	result=0;
	goto out;
out_undo_max_sectors:
	kfree(max_sectors[tapeblock_major]);
out_undo_hardsect_size:
out_undo_blk_size:
out_undo_bdev:
	unregister_blkdev(tapeblock_major, "tBLK");
	result=-ENOMEM;
	max_sectors[tapeblock_major]=NULL;
	tapeblock_major=-1;    
out:
	return result;
}


void 
tapeblock_uninit(void) {
	if (tapeblock_major==-1)
	        goto out; /* init failed so there is nothing to clean up */
	if (max_sectors[tapeblock_major]!=NULL) {
		kfree (max_sectors[tapeblock_major]);
		max_sectors[tapeblock_major]=NULL;
	}

	unregister_blkdev(tapeblock_major, "tBLK");

out:
	return;
}

int
tapeblock_open(struct inode *inode, struct file *filp) {
	tape_dev_t *td = NULL;
	int rc = 0;
	long lockflags;


	tape_sprintf_event (tape_dbf_area,6,"b:open:  %x\n",td->first_minor);

	inode = filp->f_dentry->d_inode;

	td = tape_get_device_by_minor(MINOR (inode->i_rdev));
	if (td == NULL){
		rc = -ENODEV;
		goto error;
	}
	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	if (tape_state_get(td) == TS_NOT_OPER) {
		tape_sprintf_event (tape_dbf_area,6,"c:nodev\n");
		rc = -ENODEV;
		goto out_rel_lock;
	}
	if (tape_state_get (td) != TS_UNUSED) {
		tape_sprintf_event (tape_dbf_area,6,"b:dbusy\n");
		rc = -EBUSY;
		goto out_rel_lock;
	}
	tape_state_set (td, TS_IN_USE);
        td->blk_data.position=-1;
	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);
        rc=tapeblock_mediumdetect(td);
        if (rc) {
	    s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	    tape_state_set (td, TS_UNUSED);
	    goto out_rel_lock; // in case of errors, we don't have a size of the medium
	}
	if ( td->discipline->owner )
		__MOD_INC_USE_COUNT(td->discipline->owner);
	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
	td->filp = filp;
	filp->private_data = td;/* save the dev.info for later reference */
out_rel_lock:
	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);
error:
	if(rc != 0){
		if (td != NULL)
			tape_put_device(td);
	}
	return rc;
}

int
tapeblock_release(struct inode *inode, struct file *filp) {
	long lockflags;
	tape_dev_t *td = NULL;
	int rc = 0;
	if((!inode) || !(inode->i_rdev)) {
		rc = -EINVAL;
		goto out;
	}
	td = tape_get_device_by_minor(MINOR (inode->i_rdev));
	if (td==NULL) {
		rc = -ENODEV;
		goto out;
	}
	s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);

	tape_sprintf_event (tape_dbf_area,6,"b:release: %x\n",td->first_minor);
	if(tape_state_get(td) == TS_IN_USE)
		tape_state_set (td, TS_UNUSED);
	else if (tape_state_get(td) != TS_NOT_OPER) 
		BUG();
	s390irq_spin_unlock_irqrestore (td->devinfo.irq, lockflags);
	tape_put_device(td);
	tape_put_device(td); /* 2x ! */
	if ( td->discipline->owner )
		__MOD_DEC_USE_COUNT(td->discipline->owner);
out:
	return rc;
}

static void
tapeblock_end_request(tape_dev_t* td) {
    struct buffer_head *bh;
    int uptodate;
    tape_ccw_req_t *treq = tape_get_active_ccw_req(td);
    if(treq == NULL){
	uptodate = 0;
    }
    else
    	uptodate=(treq->rc == 0); // is the buffer up to date?
    if (uptodate) {
	tape_sprintf_event (tape_dbf_area,6,"b:done: %x\n",(unsigned long)treq);
    } else {
	tape_sprintf_event (tape_dbf_area,3,"b:failed: %x\n",(unsigned long)treq);
    }
    // now inform ll_rw_block about a request status
    while ((bh = td->blk_data.current_request->bh) != NULL) {
	td->blk_data.current_request->bh = bh->b_reqnext;
	bh->b_reqnext = NULL;
	bh->b_end_io (bh, uptodate);
    }
    if (!end_that_request_first (td->blk_data.current_request, uptodate, "tBLK")) {
#ifndef DEVICE_NO_RANDOM
	add_blkdev_randomness (MAJOR (td->blk_data.current_request->rq_dev));
#endif
	end_that_request_last (td->blk_data.current_request);
    }
    if (treq!=NULL) {
	    tape_remove_ccw_req(td,treq);
	    td->discipline->free_bread(treq);
    }
    td->blk_data.current_request=NULL;
    return;
}

static void
tapeblock_exec_IO (tape_dev_t* td) {
    int rc;
    struct request* req;
    tape_ccw_req_t *treq = tape_get_active_ccw_req(td);

    if (treq) { // process done/failed request
	while (treq->rc != 0 && td->blk_data.blk_retries>0) {
	    td->blk_data.blk_retries--;
	    td->blk_data.position=-1;
	    td->discipline->bread_enable_locate(treq);
	    tape_sprintf_event (tape_dbf_area,3,"b:retryreq: %x\n",(unsigned long)treq);
	    rc = tape_do_io_irq(td,treq,TAPE_SCHED_BLOCK);
	    if (rc != 0) {
		tape_sprintf_event (tape_dbf_area,3,"b:doIOfail: %x\n",(unsigned long)treq);
		continue; // one retry lost 'cause doIO failed
	    }
	    return;
	}
	tapeblock_end_request (td); // check state, inform user, free mem, dev=idl
    }
    if(TAPE_BUSY(td)) BUG(); // tape should be idle now, request should be freed!
    if (tape_state_get (td) == TS_NOT_OPER) {
	return;
    }
	if (list_empty (&td->blk_data.request_queue.queue_head)) {
	// nothing more to do or device has dissapeared;)
	tape_sprintf_event (tape_dbf_area,6,"b:Qempty\n");
	return;
    }
    // queue is not empty, fetch a request and start IO!
    req=td->blk_data.current_request=tape_next_request(&td->blk_data.request_queue);
    if (req==NULL) {
	BUG(); // Yo. The queue was not reported empy, but no request found. This is _bad_.
    }
    if (req->cmd!=READ) { // we only support reading
	tapeblock_end_request (td); // check state, inform user, free mem, dev=idl
	tapeblock_schedule_exec_io(td);
	return;
    }
    treq=td->discipline->bread(req,td,tapeblock_major); //build channel program from request
    if (!treq) {
	// ccw generation failed. we try again later.
	tape_sprintf_event (tape_dbf_area,3,"b:cqrNULL\n");
	tapeblock_schedule_exec_io(td);
	td->blk_data.current_request=NULL;
	return;
    }
    td->blk_data.blk_retries = TAPEBLOCK_RETRIES;
    rc = tape_do_io_irq(td,treq,TAPE_SCHED_BLOCK);
    if (rc != 0) {
	// okay. ssch failed. we try later.
	tape_sprintf_event (tape_dbf_area,3,"b:doIOfail\n");
	tape_remove_ccw_req(td,treq);
	td->discipline->free_bread(treq);
	td->blk_data.current_request=NULL;
	tapeblock_schedule_exec_io(td);
	return;
    }
    // our request is in IO. we remove it from the queue and exit
    tape_dequeue_request (&td->blk_data.request_queue,req);
}

static void 
do_tape_request (tape_dev_t * td) {
    long lockflags;
    if (td==NULL) BUG();
    s390irq_spin_lock_irqsave (td->devinfo.irq, lockflags);
    if (tape_state_get(td)!=TS_IN_USE) {
	s390irq_spin_unlock_irqrestore(td->devinfo.irq,lockflags);
	return;
    }
    tapeblock_exec_IO(td);
    s390irq_spin_unlock_irqrestore(td->devinfo.irq,lockflags);
}

static void
run_tapeblock_exec_IO (tape_dev_t* td) {
    long flags_390irq,flags_ior;
    request_queue_t *q = &tape->request_queue;

    spin_lock_irqsave (&q->queue_lock, flags_ior);
    s390irq_spin_lock_irqsave(td->devinfo.irq,flags_390irq);
    atomic_set(&td->blk_data.bh_scheduled,0);
    tapeblock_exec_IO(td);
    s390irq_spin_unlock_irqrestore(td->devinfo.irq,flags_390irq);
    spin_unlock_irqrestore (&q->queue_lock, flags_ior);
}

void
tapeblock_schedule_exec_io (tape_dev_t *td)
{
	/* Protect against rescheduling, when already running */
        if (atomic_compare_and_swap(0,1,&td->blk_data.bh_scheduled)) {
                return;
        }
	INIT_LIST_HEAD(&td->blk_data.bh_tq.list);
	td->blk_data.bh_tq.sync = 0;
	td->blk_data.bh_tq.routine = (void *) (void *) run_tapeblock_exec_IO;
	td->blk_data.bh_tq.data = td;

	queue_task (&td->blk_data.bh_tq, &tq_immediate);
	mark_bh (IMMEDIATE_BH);
	return;
}

static void  tape_request_fn (request_queue_t* queue) {
	tape_dev_t* td=tape_get_device_by_queue(queue);
	if (td!=NULL) {
		do_tape_request(td);
		tape_put_device(td);
	}		
}

static request_queue_t* tapeblock_getqueue (kdev_t kdev) {
	tape_dev_t* td=tape_get_device_by_minor(MINOR(kdev));
	if (td!=NULL) return &td->blk_data.request_queue;
	else return NULL;
}

int tapeblock_mediumdetect(tape_dev_t* td) {
	tape_ccw_req_t *treq;
	unsigned int nr_of_blks;
	int rc;
	PRINT_INFO("Detecting media size...\n");

	/* Rewind */

	treq = td->discipline->ioctl (td, MTREW, 1, &rc);
	if (treq == NULL)
		return rc;
	rc = tape_do_io_and_wait (td,treq,TAPE_WAIT_INTERRUPTIBLE);
	TAPE_MERGE_RC(treq,rc);
	tape_free_ccw_req (treq);
	if (rc)
		return rc;

	/* FSF */

	treq=td->discipline->ioctl (td, MTFSF,1,&rc);
	if (treq == NULL) 
		return rc;
	rc = tape_do_io_and_wait (td,treq,TAPE_WAIT_INTERRUPTIBLE);
	TAPE_MERGE_RC(treq,rc);
	tape_free_ccw_req (treq);
	if (rc)
		return rc;

	/* TELL */

	treq = td->discipline->ioctl (td, MTTELL, 1, &rc);
	if (treq == NULL) 
		return rc;
	rc = tape_do_io_and_wait(td,treq,TAPE_WAIT_INTERRUPTIBLE);
	TAPE_MERGE_RC(treq,rc);
	nr_of_blks = *((int*)(treq->kernbuf)) - 1; /* don't count FM */
	tape_free_ccw_req (treq);
	if(rc)
		return rc;

	/* Rewind */

	treq = td->discipline->ioctl (td, MTREW, 1, &rc);
        if (treq == NULL)
		return rc;
        rc = tape_do_io_and_wait(td,treq,TAPE_WAIT_INTERRUPTIBLE);
	TAPE_MERGE_RC(treq,rc);
        tape_free_ccw_req (treq);
	if(rc)	
		return rc;
	return 0;
}
