#ifndef _BLK_H
#define _BLK_H

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/locks.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>

/*
 * get rid of this next...
 */
extern int ide_init(void);

extern void set_device_ro(kdev_t dev,int flag);
extern void add_blkdev_randomness(int major);

#ifdef CONFIG_BLK_DEV_INITRD

#define INITRD_MINOR 250 /* shouldn't collide with /dev/ram* too soon ... */

extern unsigned long initrd_start,initrd_end;
extern int initrd_below_start_ok; /* 1 if it is not an error if initrd_start < memory_start */
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
void initrd_init(void);

#endif
/*
 * end_request() and friends. Must be called with the request queue spinlock
 * acquired. All functions called within end_request() _must_be_ atomic.
 *
 * Several drivers define their own end_request and call
 * end_that_request_first() and end_that_request_last()
 * for parts of the original function. This prevents
 * code duplication in drivers.
 */

extern int end_that_request_first(struct request *, int, int);
extern void end_that_request_last(struct request *);

static inline void blkdev_dequeue_request(struct request *req)
{
	list_del(&req->queuelist);

	if (req->q)
		elv_remove_request(req->q, req);
}

extern inline struct request *elv_next_request(request_queue_t *q)
{
	struct request *rq;

	while ((rq = __elv_next_request(q))) {
		rq->flags |= REQ_STARTED;

		if (&rq->queuelist == q->last_merge)
			q->last_merge = NULL;

		if ((rq->flags & REQ_DONTPREP) || !q->prep_rq_fn)
			break;

		/*
		 * all ok, break and return it
		 */
		if (!q->prep_rq_fn(q, rq))
			break;

		/*
		 * prep said no-go, kill it
		 */
		blkdev_dequeue_request(rq);
		if (end_that_request_first(rq, 0, rq->nr_sectors))
			BUG();

		end_that_request_last(rq);
	}

	return rq;
}

#define _elv_add_request_core(q, rq, where, plug)			\
	do {								\
		if ((plug))						\
			blk_plug_device((q));				\
		(q)->elevator.elevator_add_req_fn((q), (rq), (where));	\
	} while (0)

#define _elv_add_request(q, rq, back, p) do {				      \
	if ((back))							      \
		_elv_add_request_core((q), (rq), (q)->queue_head.prev, (p)); \
	else								      \
		_elv_add_request_core((q), (rq), &(q)->queue_head, 0);	      \
} while (0)

#define elv_add_request(q, rq, back) _elv_add_request((q), (rq), (back), 1)
	
#if defined(MAJOR_NR) || defined(IDE_DRIVER)

#undef DEVICE_ON
#undef DEVICE_OFF

/*
 * Add entries as needed.
 */

#ifdef IDE_DRIVER

#define DEVICE_NR(device)	(minor(device) >> PARTN_BITS)
#define DEVICE_NAME "ide"

#elif (MAJOR_NR == RAMDISK_MAJOR)

/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_NR(device) (minor(device))
#define DEVICE_NO_RANDOM

#elif (MAJOR_NR == Z2RAM_MAJOR)

/* Zorro II Ram */
#define DEVICE_NAME "Z2RAM"
#define DEVICE_REQUEST do_z2_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == FLOPPY_MAJOR)

static void floppy_off(unsigned int nr);

#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ( (minor(device) & 3) | ((minor(device) & 0x80 ) >> 5 ))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == HD_MAJOR)

/* Hard disk:  timeout is 6 seconds. */
#define DEVICE_NAME "hard disk"
#define DEVICE_INTR do_hd
#define TIMEOUT_VALUE (6*HZ)
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (minor(device)>>6)

#elif (SCSI_DISK_MAJOR(MAJOR_NR))

#define DEVICE_NAME "scsidisk"
#define TIMEOUT_VALUE (2*HZ)
#define DEVICE_NR(device) (((major(device) & SD_MAJOR_MASK) << (8 - 4)) + (minor(device) >> 4))

/* Kludge to use the same number for both char and block major numbers */
#elif  (MAJOR_NR == MD_MAJOR) && defined(MD_DRIVER)

#define DEVICE_NAME "Multiple devices driver"
#define DEVICE_REQUEST do_md_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == SCSI_TAPE_MAJOR)

#define DEVICE_NAME "scsitape"
#define DEVICE_INTR do_st  
#define DEVICE_NR(device) (minor(device) & 0x7f)

#elif (MAJOR_NR == OSST_MAJOR)

#define DEVICE_NAME "onstream" 
#define DEVICE_INTR do_osst
#define DEVICE_NR(device) (minor(device) & 0x7f) 
#define DEVICE_ON(device) 
#define DEVICE_OFF(device) 

#elif (MAJOR_NR == SCSI_CDROM_MAJOR)

#define DEVICE_NAME "CD-ROM"
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == XT_DISK_MAJOR)

#define DEVICE_NAME "xt disk"
#define DEVICE_REQUEST do_xd_request
#define DEVICE_NR(device) (minor(device) >> 6)

#elif (MAJOR_NR == PS2ESDI_MAJOR)

#define DEVICE_NAME "PS/2 ESDI"
#define DEVICE_REQUEST do_ps2esdi_request
#define DEVICE_NR(device) (minor(device) >> 6)

#elif (MAJOR_NR == CDU31A_CDROM_MAJOR)

#define DEVICE_NAME "CDU31A"
#define DEVICE_REQUEST do_cdu31a_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == ACSI_MAJOR) && (defined(CONFIG_ATARI_ACSI) || defined(CONFIG_ATARI_ACSI_MODULE))

#define DEVICE_NAME "ACSI"
#define DEVICE_INTR do_acsi
#define DEVICE_REQUEST do_acsi_request
#define DEVICE_NR(device) (minor(device) >> 4)

#elif (MAJOR_NR == MITSUMI_CDROM_MAJOR)

#define DEVICE_NAME "Mitsumi CD-ROM"
/* #define DEVICE_INTR do_mcd */
#define DEVICE_REQUEST do_mcd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == MITSUMI_X_CDROM_MAJOR)

#define DEVICE_NAME "Mitsumi CD-ROM"
/* #define DEVICE_INTR do_mcdx */
#define DEVICE_REQUEST do_mcdx_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == MATSUSHITA_CDROM_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #1"
#define DEVICE_REQUEST do_sbpcd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == MATSUSHITA_CDROM2_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #2"
#define DEVICE_REQUEST do_sbpcd2_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == MATSUSHITA_CDROM3_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #3"
#define DEVICE_REQUEST do_sbpcd3_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == MATSUSHITA_CDROM4_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #4"
#define DEVICE_REQUEST do_sbpcd4_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == AZTECH_CDROM_MAJOR)

#define DEVICE_NAME "Aztech CD-ROM"
#define DEVICE_REQUEST do_aztcd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == CDU535_CDROM_MAJOR)

#define DEVICE_NAME "SONY-CDU535"
#define DEVICE_INTR do_cdu535
#define DEVICE_REQUEST do_cdu535_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == GOLDSTAR_CDROM_MAJOR)

#define DEVICE_NAME "Goldstar R420"
#define DEVICE_REQUEST do_gscd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == CM206_CDROM_MAJOR)
#define DEVICE_NAME "Philips/LMS CD-ROM cm206"
#define DEVICE_REQUEST do_cm206_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == OPTICS_CDROM_MAJOR)

#define DEVICE_NAME "DOLPHIN 8000AT CD-ROM"
#define DEVICE_REQUEST do_optcd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == SANYO_CDROM_MAJOR)

#define DEVICE_NAME "Sanyo H94A CD-ROM"
#define DEVICE_REQUEST do_sjcd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == APBLOCK_MAJOR)

#define DEVICE_NAME "apblock"
#define DEVICE_REQUEST ap_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == DDV_MAJOR)

#define DEVICE_NAME "ddv"
#define DEVICE_REQUEST ddv_request
#define DEVICE_NR(device) (minor(device)>>PARTN_BITS)

#elif (MAJOR_NR == MFM_ACORN_MAJOR)

#define DEVICE_NAME "mfm disk"
#define DEVICE_INTR do_mfm
#define DEVICE_REQUEST do_mfm_request
#define DEVICE_NR(device) (minor(device) >> 6)

#elif (MAJOR_NR == NBD_MAJOR)

#define DEVICE_NAME "nbd"
#define DEVICE_REQUEST do_nbd_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == MDISK_MAJOR)

#define DEVICE_NAME "mdisk"
#define DEVICE_REQUEST mdisk_request
#define DEVICE_NR(device) (minor(device))

#elif (MAJOR_NR == DASD_MAJOR)

#define DEVICE_NAME "dasd"
#define DEVICE_REQUEST do_dasd_request
#define DEVICE_NR(device) (minor(device) >> PARTN_BITS)

#elif (MAJOR_NR == I2O_MAJOR)

#define DEVICE_NAME "I2O block"
#define DEVICE_REQUEST i2ob_request
#define DEVICE_NR(device) (minor(device)>>4)

#elif (MAJOR_NR == COMPAQ_SMART2_MAJOR)

#define DEVICE_NAME "ida"
#define TIMEOUT_VALUE (25*HZ)
#define DEVICE_REQUEST do_ida_request
#define DEVICE_NR(device) (minor(device) >> 4)

#endif /* MAJOR_NR == whatever */

/* provide DEVICE_xxx defaults, if not explicitly defined
 * above in the MAJOR_NR==xxx if-elif tree */
#ifndef DEVICE_ON
#define DEVICE_ON(device) do {} while (0)
#endif
#ifndef DEVICE_OFF
#define DEVICE_OFF(device) do {} while (0)
#endif

#if (MAJOR_NR != SCSI_TAPE_MAJOR) && (MAJOR_NR != OSST_MAJOR)
#if !defined(IDE_DRIVER)

#ifndef CURRENT
#define CURRENT elv_next_request(&blk_dev[MAJOR_NR].request_queue)
#endif
#ifndef QUEUE
#define QUEUE (&blk_dev[MAJOR_NR].request_queue)
#endif
#ifndef QUEUE_EMPTY
#define QUEUE_EMPTY blk_queue_empty(QUEUE)
#endif


#ifndef DEVICE_NAME
#define DEVICE_NAME "unknown"
#endif

#define CURRENT_DEV DEVICE_NR(CURRENT->rq_dev)

#ifdef DEVICE_INTR
static void (*DEVICE_INTR)(void) = NULL;
#endif

#define SET_INTR(x) (DEVICE_INTR = (x))

#ifdef DEVICE_REQUEST
static void (DEVICE_REQUEST)(request_queue_t *);
#endif 
  
#ifdef DEVICE_INTR
#define CLEAR_INTR SET_INTR(NULL)
#else
#define CLEAR_INTR
#endif

#define INIT_REQUEST						\
	if (QUEUE_EMPTY) {					\
		CLEAR_INTR;					\
		return;						\
	}							\
	if (major(CURRENT->rq_dev) != MAJOR_NR) 			\
		panic(DEVICE_NAME ": request list destroyed");	\
	if (!CURRENT->bio)					\
		panic(DEVICE_NAME ": no bio");			\

#endif /* !defined(IDE_DRIVER) */


#ifndef LOCAL_END_REQUEST	/* If we have our own end_request, we do not want to include this mess */

#if ! SCSI_BLK_MAJOR(MAJOR_NR) && (MAJOR_NR != COMPAQ_SMART2_MAJOR)

static inline void end_request(int uptodate)
{
	struct request *req = CURRENT;

	if (end_that_request_first(req, uptodate, CURRENT->hard_cur_sectors))
		return;

#ifndef DEVICE_NO_RANDOM
	add_blkdev_randomness(major(req->rq_dev));
#endif
	DEVICE_OFF(req->rq_dev);
	blkdev_dequeue_request(req);
	end_that_request_last(req);
}

#endif /* ! SCSI_BLK_MAJOR(MAJOR_NR) */
#endif /* LOCAL_END_REQUEST */

#endif /* (MAJOR_NR != SCSI_TAPE_MAJOR) */
#endif /* defined(MAJOR_NR) || defined(IDE_DRIVER) */

#endif /* _BLK_H */
