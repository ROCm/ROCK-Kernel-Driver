
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
#include <asm/ccwcache.h>	/* CCW allocations      */
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
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static struct block_device_operations tapeblock_fops = {
#else
static struct file_operations tapeblock_fops = {
#endif
    open    : tapeblock_open,      /* open */
    release : tapeblock_release,   /* release */
        };

int    tapeblock_major = 0;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static void tape_request_fn (request_queue_t * queue);
#else
static void tape_request_fn (void);
#endif

static request_queue_t* tapeblock_getqueue (kdev_t kdev);

#ifdef CONFIG_DEVFS_FS
void
tapeblock_mkdevfstree (tape_info_t* tape) {
    tape->devfs_block_dir=devfs_mk_dir (tape->devfs_dir, "block", tape);
    tape->devfs_disc=devfs_register(tape->devfs_block_dir, "disc",DEVFS_FL_DEFAULT,
				    tapeblock_major, tape->blk_minor,
				    TAPEBLOCK_DEFAULTMODE, &tapeblock_fops, tape);
}

void
tapeblock_rmdevfstree (tape_info_t* tape) {
    devfs_unregister(tape->devfs_disc);
    devfs_unregister(tape->devfs_block_dir);
}
#endif

void 
tapeblock_setup(tape_info_t* tape) {
    blk_size[tapeblock_major][tape->blk_minor]=0; // this will be detected
    blksize_size[tapeblock_major][tape->blk_minor]=2048; // blocks are 2k by default.
    hardsect_size[tapeblock_major][tape->blk_minor]=512;
    blk_init_queue (&tape->request_queue, tape_request_fn); 
    blk_queue_headactive (&tape->request_queue, 0); 
#ifdef CONFIG_DEVFS_FS
    tapeblock_mkdevfstree(tape);
#endif
}

int
tapeblock_init(void) {
    int result;
    tape_frontend_t* blkfront,*temp;
    tape_info_t* tape;

    tape_init();
    /* Register the tape major number to the kernel */
#ifdef CONFIG_DEVFS_FS
    result = devfs_register_blkdev(tapeblock_major, "tBLK", &tapeblock_fops);
#else
    result = register_blkdev(tapeblock_major, "tBLK", &tapeblock_fops);
#endif
    if (result < 0) {
        PRINT_WARN(KERN_ERR "tape: can't get major %d for block device\n", tapeblock_major);
        panic ("cannot get major number for tape block device");
    }
    if (tapeblock_major == 0) tapeblock_major = result;   /* accept dynamic major number*/
    INIT_BLK_DEV(tapeblock_major,tape_request_fn,tapeblock_getqueue,NULL);
    read_ahead[tapeblock_major]=TAPEBLOCK_READAHEAD;
    PRINT_WARN(KERN_ERR " tape gets major %d for block device\n", result);
    blk_size[tapeblock_major] = (int*) kmalloc (256*sizeof(int),GFP_ATOMIC);
    memset(blk_size[tapeblock_major],0,256*sizeof(int));
    blksize_size[tapeblock_major] = (int*) kmalloc (256*sizeof(int),GFP_ATOMIC);
    memset(blksize_size[tapeblock_major],0,256*sizeof(int));
    hardsect_size[tapeblock_major] = (int*) kmalloc (256*sizeof(int),GFP_ATOMIC);
    memset(hardsect_size[tapeblock_major],0,256*sizeof(int));
    max_sectors[tapeblock_major] = (int*) kmalloc (256*sizeof(int),GFP_ATOMIC);
    memset(max_sectors[tapeblock_major],0,256*sizeof(int));
    blkfront = kmalloc(sizeof(tape_frontend_t),GFP_KERNEL);
    if (blkfront==NULL) panic ("no mem for tape block device structure");
    blkfront->device_setup=tapeblock_setup;
#ifdef CONFIG_DEVFS_FS
    blkfront->mkdevfstree = tapeblock_mkdevfstree;
    blkfront->rmdevfstree = tapeblock_rmdevfstree;
#endif
    blkfront->next=NULL;
    if (first_frontend==NULL) {
	first_frontend=blkfront;
    } else {
	temp=first_frontend;
	while (temp->next!=NULL)
	temp=temp->next;
	temp->next=blkfront;
    }
    tape=first_tape_info;
    while (tape!=NULL) {
	tapeblock_setup(tape);
	tape=tape->next;
    }
    return 0;
}


void 
tapeblock_uninit(void) {
    unregister_blkdev(tapeblock_major, "tBLK");
}

int
tapeblock_open(struct inode *inode, struct file *filp) {
    tape_info_t *ti;
    kdev_t dev;
    int rc;
    long lockflags;

    inode = filp->f_dentry->d_inode;
    ti = first_tape_info;
    while ((ti != NULL) && (ti->blk_minor != MINOR (inode->i_rdev)))
		ti = (tape_info_t *) ti->next;
	if (ti == NULL)
		return -ENODEV;
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"b:open:");
	debug_int_event (tape_debug_area,6,ti->blk_minor);
#endif
	s390irq_spin_lock_irqsave (ti->devinfo.irq, lockflags);
	if (tapestate_get (ti) != TS_UNUSED) {
		s390irq_spin_unlock_irqrestore (ti->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
		debug_text_event (tape_debug_area,6,"b:dbusy");
#endif
		return -EBUSY;
	}
	tapestate_set (ti, TS_IDLE);
        ti->position=-1;
        
	s390irq_spin_unlock_irqrestore (ti->devinfo.irq, lockflags);
        rc=tapeblock_mediumdetect(ti);
        if (rc) return rc; // in case of errors, we don't have a size of the medium
	dev = MKDEV (tapeblock_major, MINOR (inode->i_rdev));	/* Get the device */
	s390irq_spin_lock_irqsave (ti->devinfo.irq, lockflags);
	ti->blk_filp = filp;
	filp->private_data = ti;	/* save the dev.info for later reference */
        ti->cqr=NULL;
	s390irq_spin_unlock_irqrestore (ti->devinfo.irq, lockflags);
    
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif				/* MODULE */
	return 0;
}

int
tapeblock_release(struct inode *inode, struct file *filp) {
	long lockflags;
	tape_info_t *ti,*lastti;
	ti = first_tape_info;
	while ((ti != NULL) && (ti->blk_minor != MINOR (inode->i_rdev)))
		ti = (tape_info_t *) ti->next;
	if ((ti != NULL) && (tapestate_get (ti) == TS_NOT_OPER)) {
	    if (ti==first_tape_info) {
		first_tape_info=ti->next;
	    } else {
		lastti=first_tape_info;
		while (lastti->next!=ti) lastti=lastti->next;
		lastti->next=ti->next;
	    }
	    kfree(ti);    
	    return 0;
	}
	if ((ti == NULL) || (tapestate_get (ti) != TS_IDLE)) {
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,3,"b:notidle!");
#endif
		return -ENXIO;	/* error in tape_release */
	}
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"b:release:");
	debug_int_event (tape_debug_area,6,ti->blk_minor);
#endif
	s390irq_spin_lock_irqsave (ti->devinfo.irq, lockflags);
	tapestate_set (ti, TS_UNUSED);
	s390irq_spin_unlock_irqrestore (ti->devinfo.irq, lockflags);
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif				/* MODULE */
	invalidate_buffers(inode->i_rdev);
	return 0;
}

static void
tapeblock_end_request(tape_info_t* tape) {
    struct buffer_head *bh;
    int uptodate;
    if ((tapestate_get(tape)!=TS_FAILED) &&
	(tapestate_get(tape)!=TS_DONE))
       BUG(); // A request has to be completed to end it
    uptodate=(tapestate_get(tape)==TS_DONE); // is the buffer up to date?
#ifdef TAPE_DEBUG
    if (uptodate) {
	debug_text_event (tape_debug_area,6,"b:done:");
	debug_int_event (tape_debug_area,6,(long)tape->cqr);
    } else {
	debug_text_event (tape_debug_area,3,"b:failed:");
	debug_int_event (tape_debug_area,3,(long)tape->cqr);
    }
#endif
    // now inform ll_rw_block about a request status
    while ((bh = tape->current_request->bh) != NULL) {
	tape->current_request->bh = bh->b_reqnext;
	bh->b_reqnext = NULL;
	bh->b_end_io (bh, uptodate);
    }
    if (!end_that_request_first (tape->current_request, uptodate, "tBLK")) {
#ifndef DEVICE_NO_RANDOM
	add_blkdev_randomness (MAJOR (tape->current_request->rq_dev));
#endif
	end_that_request_last (tape->current_request);
    }
    tape->discipline->free_bread(tape->cqr,tape);
    tape->cqr=NULL;
    tape->current_request=NULL;
    if (tapestate_get(tape)!=TS_NOT_OPER) tapestate_set(tape,TS_IDLE);
    return;
}

static void
tapeblock_exec_IO (tape_info_t* tape) {
    int rc;
    struct request* req;
    if (tape->cqr) { // process done/failed request
	while ((tapestate_get(tape)==TS_FAILED) &&
	    tape->blk_retries>0) {
	    tape->blk_retries--;
	    tape->position=-1;
	    tapestate_set(tape,TS_BLOCK_INIT);
#ifdef TAPE_DEBUG
	    debug_text_event (tape_debug_area,3,"b:retryreq:");
	    debug_int_event (tape_debug_area,3,(long)tape->cqr);
#endif
	    rc = do_IO (tape->devinfo.irq, tape->cqr->cpaddr, (unsigned long) tape->cqr, 
			0x00, tape->cqr->options);
	    if (rc) {
#ifdef TAPE_DEBUG
		debug_text_event (tape_debug_area,3,"b:doIOfail:");
		debug_int_event (tape_debug_area,3,(long)tape->cqr);
#endif 
		continue; // one retry lost 'cause doIO failed
	    }
	    return;
	}
	tapeblock_end_request (tape); // check state, inform user, free mem, dev=idl
    }
    if (tape->cqr!=NULL) BUG(); // tape should be idle now, request should be freed!
    if (tapestate_get (tape) == TS_NOT_OPER) {
	tape->blk_minor=tape->rew_minor=tape->nor_minor=-1;
	tape->devinfo.irq=-1;
	return;
    }
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	if (list_empty (&tape->request_queue.queue_head)) {
#else
	if (tape->request_queue==NULL) {
#endif
	// nothing more to do or device has dissapeared;)
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"b:Qempty");
#endif
	tapestate_set(tape,TS_IDLE);
	return;
    }
    // queue is not empty, fetch a request and start IO!
    req=tape->current_request=tape_next_request(&tape->request_queue);
    if (req==NULL) {
	BUG(); // Yo. The queue was not reported empy, but no request found. This is _bad_.
    }
    if (req->cmd!=READ) { // we only support reading
	tapestate_set(tape,TS_FAILED);
	tapeblock_end_request (tape); // check state, inform user, free mem, dev=idl
	tapestate_set(tape,TS_BLOCK_INIT);
	schedule_tapeblock_exec_IO(tape);
	return;
    }
    tape->cqr=tape->discipline->bread(req,tape,tapeblock_major); //build channel program from request
    if (!tape->cqr) {
	// ccw generation failed. we try again later.
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,3,"b:cqrNULL");
#endif
	schedule_tapeblock_exec_IO(tape);
	tape->current_request=NULL;
	return;
    }
    tape->blk_retries = TAPEBLOCK_RETRIES;
    rc= do_IO (tape->devinfo.irq, tape->cqr->cpaddr, 
	       (unsigned long) tape->cqr, 0x00, tape->cqr->options);
    if (rc) {
	// okay. ssch failed. we try later.
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,3,"b:doIOfail");
#endif
	tape->discipline->free_bread(tape->cqr,tape);
	tape->cqr=NULL;
	tape->current_request=NULL;
	schedule_tapeblock_exec_IO(tape);
	return;
    }
    // our request is in IO. we remove it from the queue and exit
    tape_dequeue_request (&tape->request_queue,req);
}

static void 
do_tape_request (request_queue_t * queue) {
    tape_info_t* tape;
    long lockflags;
    for (tape=first_tape_info;
	 ((tape!=NULL) && ((&tape->request_queue)!=queue));
	 tape=tape->next);
    if (tape==NULL) BUG();
    s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
    if (tapestate_get(tape)!=TS_IDLE) {
	s390irq_spin_unlock_irqrestore(tape->devinfo.irq,lockflags);
	return;
    }
    if (tapestate_get(tape)!=TS_IDLE) BUG();
    tapestate_set(tape,TS_BLOCK_INIT);
    tapeblock_exec_IO(tape);
    s390irq_spin_unlock_irqrestore(tape->devinfo.irq,lockflags);
}

static void
run_tapeblock_exec_IO (tape_info_t* tape) {
    long flags_390irq,flags_ior;
    spin_lock_irqsave (&io_request_lock, flags_ior);
    s390irq_spin_lock_irqsave(tape->devinfo.irq,flags_390irq);
    atomic_set(&tape->bh_scheduled,0);
    tapeblock_exec_IO(tape);
    s390irq_spin_unlock_irqrestore(tape->devinfo.irq,flags_390irq);
    spin_unlock_irqrestore (&io_request_lock, flags_ior);
}

void
schedule_tapeblock_exec_IO (tape_info_t *tape)
{
	/* Protect against rescheduling, when already running */
        if (atomic_compare_and_swap(0,1,&tape->bh_scheduled)) {
                return;
        }
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	INIT_LIST_HEAD(&tape->bh_tq.list);
#endif
	tape->bh_tq.sync = 0;
	tape->bh_tq.routine = (void *) (void *) run_tapeblock_exec_IO;
	tape->bh_tq.data = tape;

	queue_task (&tape->bh_tq, &tq_immediate);
	mark_bh (IMMEDIATE_BH);
	return;
}

/* wrappers around do_tape_request for different kernel versions */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,98))
static void tape_request_fn (void) {
    tape_info_t* tape=first_tape_info;
    while (tape!=NULL) {
	do_tape_request(&tape->request_queue);
	tape=tape->next;
    }
}
#else
static void  tape_request_fn (request_queue_t* queue) {
    do_tape_request(queue);
}
#endif

static request_queue_t* tapeblock_getqueue (kdev_t kdev) {
    tape_info_t* tape=first_tape_info;
    while ((tape!=NULL) && (MINOR(kdev)!=tape->blk_minor)) 
        tape=tape->next;
    if (tape!=NULL) return &tape->request_queue;
    return NULL;
}

int tapeblock_mediumdetect(tape_info_t* tape) {
        ccw_req_t* cqr;
    int losize=1,hisize=1,rc;
    long lockflags;
#ifdef TAPE_DEBUG
    debug_text_event (tape_debug_area,3,"b:medDet");
#endif
    PRINT_WARN("Detecting media size. This will take _long_, so get yourself a coffee...\n");
    while (1) { //is interruped by break
	hisize=hisize << 1; // try twice the size tested before 
	cqr=tape->discipline->mtseek (tape, hisize);
	if (cqr == NULL) {
#ifdef TAPE_DEBUG
	    debug_text_event (tape_debug_area,6,"b:ccwg fail");
#endif
	    return -ENOSPC;
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->cqr = cqr;
	tape->wanna_wakeup=0;
	rc = do_IO (tape->devinfo.irq, cqr->cpaddr, (unsigned long) cqr, 0x00, cqr->options);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	if (rc) return -EIO;
	wait_event_interruptible (tape->wq,tape->wanna_wakeup);
	tape->cqr = NULL;
	tape_free_request (cqr);
	if (tape->kernbuf) {
	    kfree (tape->kernbuf);
	    tape->kernbuf=NULL;
	}
	if (signal_pending (current)) {
		tapestate_set (tape, TS_IDLE);
		return -ERESTARTSYS;
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	if (tapestate_get (tape) == TS_FAILED) {
		tapestate_set (tape, TS_IDLE);
		s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		break;
	}
	if (tapestate_get (tape) == TS_NOT_OPER) {
	    tape->blk_minor=tape->rew_minor=tape->nor_minor=-1;
	    tape->devinfo.irq=-1;
	    s390irq_spin_unlock_irqrestore (tape->devinfo.irq,lockflags);
	    return -ENODEV;
	}
	if (tapestate_get (tape) != TS_DONE) {
		tapestate_set (tape, TS_IDLE);
		s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		return -EIO;
	}
	tapestate_set (tape, TS_IDLE);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	losize=hisize;
    }
    cqr = tape->discipline->mtrew (tape, 1);
    if (cqr == NULL) {
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"b:ccwg fail");
#endif
	return -ENOSPC;
    }
    s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
    tape->cqr = cqr;
    tape->wanna_wakeup=0;
    rc = do_IO (tape->devinfo.irq, cqr->cpaddr, (unsigned long) cqr, 0x00, cqr->options);
    s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
    wait_event_interruptible (tape->wq,tape->wanna_wakeup);
    tape->cqr = NULL;
    tape_free_request (cqr);
    if (signal_pending (current)) {
	tapestate_set (tape, TS_IDLE);
	return -ERESTARTSYS;
    }
    s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
    if (tapestate_get (tape) == TS_FAILED) {
	tapestate_set (tape, TS_IDLE);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	return -EIO;
    }
    if (tapestate_get (tape) == TS_NOT_OPER) {
	tape->blk_minor=tape->rew_minor=tape->nor_minor=-1;
	tape->devinfo.irq=-1;
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq,lockflags);
	return -ENODEV;
    }
    if (tapestate_get (tape) != TS_DONE) {
	tapestate_set (tape, TS_IDLE);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	return -EIO;
    }
    tapestate_set (tape, TS_IDLE);
    s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
    while (losize!=hisize) {
	cqr=tape->discipline->mtseek (tape, (hisize+losize)/2+1);
	if (cqr == NULL) {
#ifdef TAPE_DEBUG
	    debug_text_event (tape_debug_area,6,"b:ccwg fail");
#endif
	    return -ENOSPC;
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->cqr = cqr;
	tape->wanna_wakeup=0;
	rc = do_IO (tape->devinfo.irq, cqr->cpaddr, (unsigned long) cqr, 0x00, cqr->options);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	if (rc) return -EIO;
	wait_event_interruptible (tape->wq,tape->wanna_wakeup);
	tape->cqr = NULL;
	tape_free_request (cqr);
	if (tape->kernbuf) {
	    kfree (tape->kernbuf);
	    tape->kernbuf=NULL;
	}
	if (signal_pending (current)) {
		tapestate_set (tape, TS_IDLE);
		return -ERESTARTSYS;
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	if (tapestate_get (tape) == TS_NOT_OPER) {
	    tape->blk_minor=tape->rew_minor=tape->nor_minor=-1;
	    tape->devinfo.irq=-1;
	    s390irq_spin_unlock_irqrestore (tape->devinfo.irq,lockflags);
	    return -ENODEV;
	}
	if (tapestate_get (tape) == TS_FAILED) {
		tapestate_set (tape, TS_IDLE);
		s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		hisize=(hisize+losize)/2;
		cqr = tape->discipline->mtrew (tape, 1);
		if (cqr == NULL) {
#ifdef TAPE_DEBUG
		    debug_text_event (tape_debug_area,6,"b:ccwg fail");
#endif
		    return -ENOSPC;
		}
		s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
		tape->cqr = cqr;
		tape->wanna_wakeup=0;
		rc = do_IO (tape->devinfo.irq, cqr->cpaddr, (unsigned long) cqr, 0x00, cqr->options);
		s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		wait_event_interruptible (tape->wq,tape->wanna_wakeup);
		tape->cqr = NULL;
		tape_free_request (cqr);
		if (signal_pending (current)) {
		    tapestate_set (tape, TS_IDLE);
		    return -ERESTARTSYS;
		}
		s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
		if (tapestate_get (tape) == TS_FAILED) {
		    tapestate_set (tape, TS_IDLE);
		    s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		    return -EIO;
		}
		if (tapestate_get (tape) != TS_DONE) {
		    tapestate_set (tape, TS_IDLE);
		    s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		    return -EIO;
		}
		tapestate_set (tape, TS_IDLE);
		s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		continue;
	}
	if (tapestate_get (tape) != TS_DONE) {
		tapestate_set (tape, TS_IDLE);
		s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
		return -EIO;
	}
	tapestate_set (tape, TS_IDLE);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	losize=(hisize+losize)/2+1;
    }
    blk_size[tapeblock_major][tape->blk_minor]=(losize)*(blksize_size[tapeblock_major][tape->blk_minor]/1024);
    return 0;
}
