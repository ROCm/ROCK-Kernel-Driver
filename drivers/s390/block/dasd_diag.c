/* 
 * File...........: linux/drivers/s390/block/dasd_diag.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Based on.......: linux/drivers/s390/block/mdisk.c
 * ...............: by Hartmunt Penner <hpenner@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000

 * History of changes
 * 07/13/00 Added fixup sections for diagnoses ans saved some registers
 * 07/14/00 fixed constraints in newly generated inline asm
 * 10/05/00 adapted to 'new' DASD driver
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <asm/debug.h>

#include <linux/malloc.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO                      */
#include <linux/blk.h>
#include <asm/ccwcache.h>
#include <asm/dasd.h>

#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/s390dyn.h>

#include "dasd_diag.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER DASD_NAME"(diag):"

dasd_discipline_t dasd_diag_discipline;

typedef struct
dasd_diag_private_t {
	dasd_diag_characteristics_t rdc_data;
	diag_rw_io_t iob;
	diag_init_io_t iib;
	unsigned long *label;
} dasd_diag_private_t;

static __inline__ int
dia210 (void *devchar)
{
	int rc;

	__asm__ __volatile__ ("    diag  %1,0,0x210\n"
			      "0:  ipm   %0\n"
			      "    srl   %0,28\n"
			      "1:\n"
			      ".section .fixup,\"ax\"\n"
			      "2:  lhi   %0,3\n"
			      "    bras  1,3f\n"
			      "    .long 1b\n"
			      "3:  l     1,0(1)\n"
			      "    br    1\n"
			      ".previous\n"
			      ".section __ex_table,\"a\"\n"
			      "    .align 4\n"
			      "    .long 0b,2b\n"
			      ".previous\n"
			      :"=d"   (rc)
			      :"d"    ((void *) __pa (devchar))
			      :"1");
	return rc;
}

static __inline__ int
dia250 (void *iob, int cmd)
{
	int rc;

	__asm__ __volatile__ ("    lr    1,%1\n"
			      "    diag  1,%2,0x250\n"
			      "1:\n"
			      ".section .fixup,\"ax\"\n"
			      "2:  lhi   %0,3\n"
			      "    bras  1,3f\n"
			      "    .long 1b\n"
			      "3:  l     1,0(1)\n"
			      "    br    1\n"
			      ".previous\n"
			      ".section __ex_table,\"a\"\n"
			      "    .align 4\n"
			      "    .long 1b,2b\n"
			      ".previous\n"
			      :"=d"   (rc)
			      :"d"    ((void *) __pa (iob)), "0" (cmd)
			      :"1");
	return rc;
}

static __inline__ int
mdsk_init_io (dasd_device_t * device, int blocksize, int offset, int size)
{
	dasd_diag_private_t *private = (dasd_diag_private_t *) device->private;
	diag_init_io_t *iib = &private->iib;
	int rc;

	memset (iib, 0, sizeof (diag_init_io_t));

	iib->dev_nr = device->devinfo.devno;
	iib->block_size = blocksize;
	iib->offset = offset;
	iib->start_block = 0;
	iib->end_block = size;

	rc = dia250 (iib, INIT_BIO);

	return rc;
}

static __inline__ int
mdsk_term_io (dasd_device_t * device)
{
	dasd_diag_private_t *private = (dasd_diag_private_t *) device->private;
	diag_init_io_t *iib = &private->iib;
	int rc;

	memset (iib, 0, sizeof (diag_init_io_t));
	iib->dev_nr = device->devinfo.devno;
	rc = dia250 (iib, TERM_BIO);
	return rc;
}

int
dasd_start_diag (ccw_req_t * cqr)
{
	int rc;
	dasd_device_t *device = cqr->device;
	dasd_diag_private_t *private;
	diag_rw_io_t *iob;

	private = (dasd_diag_private_t *) device->private;
	iob = &private->iob;

	iob->dev_nr = device->devinfo.devno;
	iob->key = 0;
	iob->flags = 2;
	iob->block_count = cqr->cplength >> 1;
	iob->interrupt_params = (u32) cqr;
	iob->bio_list = __pa (cqr->cpaddr);

	asm volatile ("STCK %0":"=m" (cqr->startclk));
	rc = dia250 (iob, RW_BIO);
	if (rc > 8) {
		PRINT_WARN ("dia250 returned CC %d\n", rc);
		check_then_set (&cqr->status,
					       CQR_STATUS_QUEUED,
					       CQR_STATUS_ERROR);
	} else {
		if (cqr->expires) {
			cqr->expires += cqr->startclk;
		}
		check_then_set (&cqr->status,
					       CQR_STATUS_QUEUED,
					       CQR_STATUS_IN_IO);
		rc = 0;
	}
	return rc;
}

void
dasd_ext_handler (struct pt_regs *regs, __u16 code)
{
	ccw_req_t *cqr;
	int ip = S390_lowcore.ext_params;
	char status = *((char *) S390_lowcore.ext_params + 5);
	dasd_device_t *device;
	int done_fast_io = 0;
	int devno;

	if (!ip) {		/* no intparm: unsolicited interrupt */
		printk (KERN_WARNING PRINTK_HEADER
			"caught unsolicited interrupt\n");
		return;
	}
	if (ip & 0x80000001) {
		printk (KERN_WARNING PRINTK_HEADER
			"caught spurious interrupt with parm %08x\n",
			ip);
		return;
	}
	cqr = (ccw_req_t *) ip;
	device = (dasd_device_t *) cqr->device;
	devno = device->devinfo.devno;
	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			" INT on devno 0x%04X  = /dev/%s (%d:%d)"
			" belongs to NULL device\n",
			devno, device->name, MAJOR (device->kdev), MINOR (device->kdev));
	}
	if (strncmp (device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		printk (KERN_WARNING PRINTK_HEADER
			"0x%04X : /dev/%s (%d:%d)"
			" magic number of ccw_req_t 0x%08X doesn't match"
			" discipline 0x%08X\n",
			devno, device->name,
			MAJOR (device->kdev), MINOR (device->kdev),
			cqr->magic, *(int *) (&device->discipline->name));
		return;
	}
	asm volatile ("STCK %0":"=m" (cqr->stopclk));
	switch (status) {
	case 0x00:
		check_then_set (&cqr->status,
					       CQR_STATUS_IN_IO,
					       CQR_STATUS_DONE);
		if ( device->level == DASD_DEVICE_LEVEL_ANALYSIS_PENDING)
					device->level = DASD_DEVICE_LEVEL_ANALYSIS_PREPARED;
		if (cqr->next &&
		    (cqr->next->status == CQR_STATUS_QUEUED)) {
			if (dasd_start_diag (cqr->next) == 0) {
				done_fast_io = 1;
			}
		}
		break;
	case 0x01:
	case 0x02:
	case 0x03:
	default:
		check_then_set (&cqr->status,
					       CQR_STATUS_IN_IO,
					       CQR_STATUS_FAILED);
		break;
	}
	wake_up (&device->wait_q);
	dasd_schedule_bh (device);
}

static int
dasd_diag_check_characteristics (struct dasd_device_t *device)
{
	int rc = 0;
	int bsize;
	int label_block;
	dasd_diag_private_t *private;
	dasd_diag_characteristics_t *rdc_data;
	ccw_req_t *cqr;
	long *label;
	int sb;

	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
		   "Null device pointer passed to characteristics checker\n");
		return -ENODEV;
	}
	if (device->private == NULL) {
                device->private = kmalloc (sizeof (dasd_diag_private_t), GFP_KERNEL);
                if (device->private == NULL) {
                        printk (KERN_WARNING PRINTK_HEADER
                                "memory allocation failed for private data\n");
                        return -ENOMEM;
                }
	}
	private = (dasd_diag_private_t *) device->private;
	rdc_data = (void *) &(private->rdc_data);
        
	rdc_data->dev_nr = device->devinfo.devno;
	rdc_data->rdc_len = sizeof (dasd_diag_characteristics_t);
        
	if (dia210 (rdc_data) != 0) {
                goto nodiag;
	}
	if (rdc_data->vdev_class != DEV_CLASS_FBA &&
	    rdc_data->vdev_class != DEV_CLASS_ECKD &&
	    rdc_data->vdev_class != DEV_CLASS_CKD) {
                goto nodiag;
	}
#if 0
	printk (KERN_INFO PRINTK_HEADER
		"%04X: %04X on real %04X/%02X\n",
		rdc_data->dev_nr,
		rdc_data->vdev_type,
		rdc_data->rdev_type, rdc_data->rdev_model);
#endif
	/* Figure out position of label block */
	if (private->rdc_data.vdev_class == DEV_CLASS_FBA) {
		label_block = 1;
	} else if (private->rdc_data.vdev_class == DEV_CLASS_ECKD ||
		   private->rdc_data.vdev_class == DEV_CLASS_CKD) {
		label_block = 2;
	} else {
                goto nodiag;
	}
	private->label = (long *) get_free_page (GFP_KERNEL);
	label = private->label;
	mdsk_term_io (device);	/* first terminate all outstanding operations */
	/* figure out blocksize of device */
	for (bsize = 512; bsize <= PAGE_SIZE; bsize <<= 1) {
		diag_bio_t *bio;
		diag_rw_io_t *iob = &private->iob;
                
		rc = mdsk_init_io (device, bsize, 0, 64);
		if (rc > 4) {
			continue;
		}
		cqr = ccw_alloc_request (dasd_diag_discipline.name, sizeof(diag_bio_t)/sizeof(ccw1_t), 0);
		if (cqr == NULL) {
			printk (KERN_WARNING PRINTK_HEADER
                                "No memory to allocate initialization request\n");
                        free_page(private->label);
                        kfree(private);
                        device->private = NULL;
                        return -ENOMEM;
		}
		bio = (diag_bio_t *) (cqr->cpaddr);
		memset (bio, 0, sizeof (diag_bio_t));
		bio->type = MDSK_READ_REQ;
		bio->block_number = label_block + 1;
		bio->buffer = __pa (private->label);
		cqr->device = device;
		cqr->status= CQR_STATUS_FILLED;
		memset (iob, 0, sizeof (diag_rw_io_t));
		iob->dev_nr = rdc_data->dev_nr;
		iob->block_count = 1;
		iob->interrupt_params = cqr;
		iob->bio_list = __pa (bio);
		rc = dia250 (iob, RW_BIO);
		if (rc == 0) {
			if (label[3] != bsize) {
                                goto nodiag;
			}
			if (label[0] != 0xc3d4e2f1) {	/* != CMS1 */
                                goto nodiag;
			}
			if (label[13] == 0) {
                                goto nodiag;
			}
			break;
		}
		mdsk_term_io (device);
	}
        if ( bsize > PAGE_SIZE )
                goto nodiag;
	device->sizes.bp_block = bsize;
	device->sizes.s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < bsize; sb = sb << 1)
		device->sizes.s2b_shift++;
        goto good;
 nodiag:
        kfree(private);
        device -> private = NULL;
        rc = -ENODEV;
 good:
	return rc;
}

static int
dasd_diag_do_analysis (struct dasd_device_t *device)
{
	int sb;
	dasd_diag_private_t *private = (dasd_diag_private_t *) device->private;

	long *label = private->label;

	/* real size of the volume */
	device->sizes.blocks = label[7];
	printk (KERN_INFO PRINTK_HEADER
		"/dev/%s (%04X): capacity (%dkB blks): %ldkB\n",
		device->name, device->devinfo.devno,
		(device->sizes.bp_block >> 10),
		(device->sizes.blocks << device->sizes.s2b_shift) >> 1);
        free_page(private->label);
	return 0;
}

static int
dasd_diag_fill_geometry (struct dasd_device_t *device, struct hd_geometry *geo)
{
	int rc = 0;
	dasd_diag_private_t *private = (dasd_diag_private_t *) device->private;
	unsigned long sectors = device->sizes.blocks << device->sizes.s2b_shift;
	unsigned long tracks = sectors >> 6;
	unsigned long trk_rem = sectors & ((1 << 6) - 1);	/* 64k per track */
	unsigned long cyls = tracks >> 4;
	unsigned long cyls_rem = tracks & ((1 << 4) - 1);	/* 16 head per cyl */

	switch (device->sizes.bp_block) {
	case 512:
	case 1024:
	case 2048:
	case 4096:
		break;
	default:
		return -EINVAL;
	}
	geo->cylinders = cyls;
	geo->heads = 16;
	geo->sectors = 128 >> device->sizes.s2b_shift;
	if (private->rdc_data.vdev_class == DEV_CLASS_FBA) {
		geo->start = 1;
	} else if (private->rdc_data.vdev_class == DEV_CLASS_ECKD ||
		   private->rdc_data.vdev_class == DEV_CLASS_CKD) {
		geo->start = 2;
	} else {
		return -EINVAL;
	}
	return rc;
}

static dasd_era_t
dasd_diag_examine_error (ccw_req_t * cqr, devstat_t * stat)
{
	return dasd_era_fatal;
}

static dasd_erp_action_fn_t
dasd_diag_erp_action (ccw_req_t * cqr)
{
	return default_erp_action;
}

static dasd_erp_postaction_fn_t
dasd_diag_erp_postaction (ccw_req_t * cqr)
{
	if (cqr->function == default_erp_action)
		return default_erp_postaction;
	printk (KERN_WARNING PRINTK_HEADER
		"unknown ERP action %p\n",
		cqr->function);
	return NULL;
}

static ccw_req_t *
dasd_diag_build_cp_from_req (dasd_device_t * device, struct request *req)
{
	ccw_req_t *rw_cp = NULL;
	struct buffer_head *bh;
	int rw_cmd;
	int noblk = req->nr_sectors >> device->sizes.s2b_shift;
	int byt_per_blk = device->sizes.bp_block;
	int block;
	diag_bio_t *bio;
	int bhct;
	long size;

	if (!noblk) {
		PRINT_ERR ("No blocks to read/write...returning\n");
		return NULL;
	}
	if (req->cmd == READ) {
		rw_cmd = MDSK_READ_REQ;
	} else if (req->cmd == WRITE) {
		rw_cmd = MDSK_WRITE_REQ;
	}
	bhct = 0;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size > byt_per_blk)
			for (size = 0; size < bh->b_size; size += byt_per_blk)
				bhct++;
		else
			bhct++;
	}
	/* Build the request */
	rw_cp = dasd_alloc_request (dasd_diag_discipline.name, bhct << 1, 0);
	if (!rw_cp) {
		return NULL;
	}
	bio = (diag_bio_t *) (rw_cp->cpaddr);

	block = req->sector >> device->sizes.s2b_shift;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size >= byt_per_blk) {
			memset (bio, 0, sizeof (diag_bio_t));
			for (size = 0; size < bh->b_size; size += byt_per_blk) {
				bio->type = rw_cmd;
				bio->block_number = block + 1;
				bio->buffer = __pa (bh->b_data + size);
				bio++;
				block++;
			}
		} else {
			PRINT_WARN ("Cannot fulfill request smaller than block\n");
			ccw_free_request (rw_cp);
			return NULL;
		}
	}
	rw_cp->device = device;
	rw_cp->expires = 5 * 0xf424000;		/* 5 seconds */
	rw_cp->req = req;
	check_then_set (&rw_cp->status, CQR_STATUS_EMPTY, CQR_STATUS_FILLED);
	return rw_cp;
}

static char *
dasd_diag_dump_sense (struct dasd_device_t *device, ccw_req_t * req)
{
	char *page = (char *) get_free_page (GFP_KERNEL);
	int len;
	if (page == NULL) {
		return NULL;
	}
	len = sprintf (page, KERN_WARNING PRINTK_HEADER
		       "device %04X on irq %d: I/O status report:\n",
		       device->devinfo.devno, device->devinfo.irq);

	return page;
}

dasd_discipline_t dasd_diag_discipline =
{
	name:"DIAG",
	ebcname:"DIAG",
	check_characteristics:dasd_diag_check_characteristics,
	do_analysis:dasd_diag_do_analysis,
	fill_geometry:dasd_diag_fill_geometry,
	start_IO:dasd_start_diag,
	examine_error:dasd_diag_examine_error,
	erp_action:dasd_diag_erp_action,
	erp_postaction:dasd_diag_erp_postaction,
	build_cp_from_req:dasd_diag_build_cp_from_req,
	dump_sense:dasd_diag_dump_sense,
	int_handler:dasd_ext_handler
};

int
dasd_diag_init (void)
{
	int rc = 0;
	if (MACHINE_IS_VM) {
		printk (KERN_INFO PRINTK_HEADER
		   "%s discipline initializing\n", dasd_diag_discipline.name);
		ASCEBC (dasd_diag_discipline.ebcname, 4);
		ctl_set_bit (0, 9);
		register_external_interrupt (0x2603, dasd_ext_handler);
		dasd_discipline_enq (&dasd_diag_discipline);
	} else {
		printk (KERN_INFO PRINTK_HEADER
			"Machine is not VM: %s discipline not initializing\n", dasd_diag_discipline.name);
		rc = -EINVAL;
	}
	return rc;
}

void
dasd_diag_cleanup( void ) {
        if ( MACHINE_IS_VM ) {
          printk ( KERN_INFO PRINTK_HEADER
                   "%s discipline cleaning up\n", dasd_diag_discipline.name);
          dasd_discipline_deq(&dasd_diag_discipline);
          unregister_external_interrupt (0x2603, dasd_ext_handler);
          ctl_clear_bit (0, 9);
        } else {
          printk ( KERN_INFO PRINTK_HEADER
                   "Machine is not VM: %s discipline not initializing\n", dasd_diag_discipline.name);
        }
}
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
