/* 
 * File...........: linux/drivers/s390/block/dasd_diag.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Based on.......: linux/drivers/s390/block/mdisk.c
 * ...............: by Hartmunt Penner <hpenner@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * $Revision: 1.28 $
 *
 * History of changes
 * 07/13/00 Added fixup sections for diagnoses ans saved some registers
 * 07/14/00 fixed constraints in newly generated inline asm
 * 10/05/00 adapted to 'new' DASD driver
 *	    fixed return codes of dia250()
 *	    fixed partition handling and HDIO_GETGEO
 * 2002/01/04 Created 2.4-2.5 compatibility mode
 * 05/04/02 code restructuring.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO			    */
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/dasd.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/s390_ext.h>
#include <asm/todclk.h>

#include "dasd_int.h"
#include "dasd_diag.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(diag):"

MODULE_LICENSE("GPL");

typedef struct dasd_diag_private_t {
	dasd_diag_characteristics_t rdc_data;
	diag_rw_io_t iob;
	diag_init_io_t iib;
	unsigned int pt_block;
} dasd_diag_private_t;

typedef struct dasd_diag_req_t {
	int block_count;
	diag_bio_t bio[0];
} dasd_diag_req_t;

static __inline__ int
dia250(void *iob, int cmd)
{
	int rc;

	__asm__ __volatile__("    lhi   %0,3\n"
			     "	  lr	0,%2\n"
			     "	  diag	0,%1,0x250\n"
			     "0:  ipm	%0\n"
			     "	  srl	%0,28\n"
			     "	  or	%0,1\n"
			     "1:\n"
#ifndef CONFIG_ARCH_S390X
			     ".section __ex_table,\"a\"\n"
			     "	  .align 4\n"
			     "	  .long 0b,1b\n"
			     ".previous\n"
#else
			     ".section __ex_table,\"a\"\n"
			     "	  .align 8\n"
			     "	  .quad  0b,1b\n"
			     ".previous\n"
#endif
			     : "=&d" (rc)
			     : "d" (cmd), "d" ((void *) __pa(iob))
			     : "0", "1", "cc");
	return rc;
}

static __inline__ int
mdsk_init_io(dasd_device_t * device, int blocksize, int offset, int size)
{
	dasd_diag_private_t *private;
	diag_init_io_t *iib;
	int rc;

	private = (dasd_diag_private_t *) device->private;
	iib = &private->iib;
	memset(iib, 0, sizeof (diag_init_io_t));

	iib->dev_nr = _ccw_device_get_device_number(device->cdev);
	iib->block_size = blocksize;
	iib->offset = offset;
	iib->start_block = 0;
	iib->end_block = size;

	rc = dia250(iib, INIT_BIO);

	return rc & 3;
}

static __inline__ int
mdsk_term_io(dasd_device_t * device)
{
	dasd_diag_private_t *private;
	diag_init_io_t *iib;
	int rc;

	private = (dasd_diag_private_t *) device->private;
	iib = &private->iib;
	memset(iib, 0, sizeof (diag_init_io_t));
	iib->dev_nr = _ccw_device_get_device_number(device->cdev);
	rc = dia250(iib, TERM_BIO);
	return rc & 3;
}

static int
dasd_start_diag(dasd_ccw_req_t * cqr)
{
	dasd_device_t *device;
	dasd_diag_private_t *private;
	dasd_diag_req_t *dreq;
	int rc;

	device = cqr->device;
	private = (dasd_diag_private_t *) device->private;
	dreq = (dasd_diag_req_t *) cqr->data;

	private->iob.dev_nr = _ccw_device_get_device_number(device->cdev);
	private->iob.key = 0;
	private->iob.flags = 2;	/* do asynchronous io */
	private->iob.block_count = dreq->block_count;
	private->iob.interrupt_params = (u32)(addr_t) cqr;
	private->iob.bio_list = __pa(dreq->bio);

	cqr->startclk = get_clock();

	rc = dia250(&private->iob, RW_BIO);
	if (rc > 8) {
		MESSAGE(KERN_WARNING, "dia250 returned CC %d", rc);
		cqr->status = DASD_CQR_ERROR;
	} else if (rc == 0) {
		cqr->status = DASD_CQR_DONE;
		dasd_schedule_bh(device);
	} else {
		cqr->status = DASD_CQR_IN_IO;
		rc = 0;
	}
	return rc;
}

static void
dasd_ext_handler(struct pt_regs *regs, __u16 code)
{
	dasd_ccw_req_t *cqr, *next;
	dasd_device_t *device;
	unsigned long long expires;
	unsigned long flags;
	char status;
	int ip, cpu;

	/*
	 * Get the external interruption subcode. VM stores
	 * this in the 'cpu address' field associated with
	 * the external interrupt. For diag 250 the subcode
	 * needs to be 3.
	 */
	if ((S390_lowcore.cpu_addr & 0xff00) != 0x0300)
		return;
	status = *((char *) &S390_lowcore.ext_params + 5);
	ip = S390_lowcore.ext_params;

	cpu = smp_processor_id();
	irq_enter();

	if (!ip) {		/* no intparm: unsolicited interrupt */
		MESSAGE(KERN_DEBUG, "%s", "caught unsolicited interrupt");
		irq_exit();
		return;
	}
	cqr = (dasd_ccw_req_t *)(addr_t) ip;
	device = (dasd_device_t *) cqr->device;
	if (strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		DEV_MESSAGE(KERN_WARNING, device,
			    " magic number of dasd_ccw_req_t 0x%08X doesn't"
			    " match discipline 0x%08X",
			    cqr->magic, *(int *) (&device->discipline->name));
		irq_exit();
		return;
	}

	/* get irq lock to modify request queue */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);

	cqr->stopclk = get_clock();

	expires = 0;
	if (status == 0) {
		cqr->status = DASD_CQR_DONE;
		/* Start first request on queue if possible -> fast_io. */
		if (!list_empty(&device->ccw_queue)) {
			next = list_entry(device->ccw_queue.next,
					  dasd_ccw_req_t, list);
			if (next->status == DASD_CQR_QUEUED) {
				if (dasd_start_diag(next) == 0)
					expires = next->expires;
				else
					MESSAGE(KERN_WARNING, "%s",
						"Interrupt fastpath failed!");
			}
		}
	} else 
		cqr->status = DASD_CQR_FAILED;

	if (expires != 0)
		dasd_set_timer(device, expires);
	else
		dasd_clear_timer(device);
	dasd_schedule_bh(device);

	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	irq_exit();
}

static int
dasd_diag_check_device(dasd_device_t *device)
{
	dasd_diag_private_t *private;
	dasd_diag_characteristics_t *rdc_data;
	diag_bio_t bio;
	long *label;
	int sb, bsize;
	int rc;

	private = (dasd_diag_private_t *) device->private;
	if (private == NULL) {
		private = kmalloc(sizeof(dasd_diag_private_t), GFP_KERNEL);
		if (private == NULL) {
			MESSAGE(KERN_WARNING, "%s",
				"memory allocation failed for private data");
			return -ENOMEM;
		}
		device->private = (void *) private;
	}
	/* Read Device Characteristics */
	rdc_data = (void *) &(private->rdc_data);
	rdc_data->dev_nr = _ccw_device_get_device_number(device->cdev);
	rdc_data->rdc_len = sizeof (dasd_diag_characteristics_t);

	rc = diag210((struct diag210 *) rdc_data);
	if (rc)
		return -ENOTSUPP;

	/* Figure out position of label block */
	switch (private->rdc_data.vdev_class) {
	case DEV_CLASS_FBA:
		private->pt_block = 1;
		break;
	case DEV_CLASS_ECKD:
		private->pt_block = 2;
		break;
	default:
		return -ENOTSUPP;
	}

	DBF_EVENT(DBF_INFO,
		  "%04X: %04X on real %04X/%02X",
		  rdc_data->dev_nr,
		  rdc_data->vdev_type,
		  rdc_data->rdev_type, rdc_data->rdev_model);

	/* terminate all outstanding operations */
	mdsk_term_io(device);

	/* figure out blocksize of device */
	label = (long *) get_zeroed_page(GFP_KERNEL);
	if (label == NULL)  {
		MESSAGE(KERN_WARNING, "%s",
			"No memory to allocate initialization request");
		return -ENOMEM;
	}
	for (bsize = 512; bsize <= PAGE_SIZE; bsize <<= 1) {
		mdsk_init_io(device, bsize, 0, 64);
		memset(&bio, 0, sizeof (diag_bio_t));
		bio.type = MDSK_READ_REQ;
		bio.block_number = private->pt_block + 1;
		bio.buffer = __pa(label);
		memset(&private->iob, 0, sizeof (diag_rw_io_t));
		private->iob.dev_nr = rdc_data->dev_nr;
		private->iob.key = 0;
		private->iob.flags = 0;	/* do synchronous io */
		private->iob.block_count = 1;
		private->iob.interrupt_params = 0;
		private->iob.bio_list = __pa(&bio);
		if (dia250(&private->iob, RW_BIO) == 0)
			break;
		mdsk_term_io(device);
	}
	if (bsize <= PAGE_SIZE && label[3] == bsize &&
	    label[0] == 0xc3d4e2f1 && label[13] != 0) {
		device->blocks = label[7];
		device->bp_block = bsize;
		device->s2b_shift = 0;	/* bits to shift 512 to get a block */
		for (sb = 512; sb < bsize; sb = sb << 1)
			device->s2b_shift++;
		
		DEV_MESSAGE(KERN_INFO, device,
			    "capacity (%dkB blks): %ldkB",
			    (device->bp_block >> 10),
			    (device->blocks << device->s2b_shift) >> 1);
		rc = 0;
	} else
		rc = -EMEDIUMTYPE;
	free_page((long) label);
	return rc;
}

static int
dasd_diag_fill_geometry(dasd_device_t *device, struct hd_geometry *geo)
{
	if (dasd_check_blocksize(device->bp_block) != 0)
		return -EINVAL;
	geo->cylinders = (device->blocks << device->s2b_shift) >> 10;
	geo->heads = 16;
	geo->sectors = 128 >> device->s2b_shift;
	return 0;
}

static dasd_era_t
dasd_diag_examine_error(dasd_ccw_req_t * cqr, struct irb * stat)
{
	return dasd_era_fatal;
}

static dasd_erp_fn_t
dasd_diag_erp_action(dasd_ccw_req_t * cqr)
{
	return dasd_default_erp_action;
}

static dasd_erp_fn_t
dasd_diag_erp_postaction(dasd_ccw_req_t * cqr)
{
	return dasd_default_erp_postaction;
}

static dasd_ccw_req_t *
dasd_diag_build_cp(dasd_device_t * device, struct request *req)
{
	dasd_ccw_req_t *cqr;
	dasd_diag_req_t *dreq;
	diag_bio_t *dbio;
	struct bio *bio;
	struct bio_vec *bv;
	char *dst;
	int count, datasize;
	sector_t recid, first_rec, last_rec;
	unsigned blksize, off;
	unsigned char rw_cmd;
	int i;

	if (rq_data_dir(req) == READ)
		rw_cmd = MDSK_READ_REQ;
	else if (rq_data_dir(req) == WRITE)
		rw_cmd = MDSK_WRITE_REQ;
	else
		return ERR_PTR(-EINVAL);
	blksize = device->bp_block;
	/* Calculate record id of first and last block. */
	first_rec = req->sector >> device->s2b_shift;
	last_rec = (req->sector + req->nr_sectors - 1) >> device->s2b_shift;
	/* Check struct bio and count the number of blocks for the request. */
	count = 0;
	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bv, bio, i) {
			if (bv->bv_len & (blksize - 1))
				/* Fba can only do full blocks. */
				return ERR_PTR(-EINVAL);
			count += bv->bv_len >> (device->s2b_shift + 9);
		}
	}
	/* Paranoia. */
	if (count != last_rec - first_rec + 1)
		return ERR_PTR(-EINVAL);
	/* Build the request */
	datasize = sizeof(dasd_diag_req_t) + count*sizeof(diag_bio_t);
	cqr = dasd_smalloc_request(dasd_diag_discipline.name, 0,
				   datasize, device);
	if (IS_ERR(cqr))
		return cqr;
	
	dreq = (dasd_diag_req_t *) cqr->data;
	dreq->block_count = count;
	dbio = dreq->bio;
	recid = first_rec;
	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bv, bio, i) {
			dst = kmap(bv->bv_page) + bv->bv_offset;
			for (off = 0; off < bv->bv_len; off += blksize) {
				memset(dbio, 0, sizeof (diag_bio_t));
				dbio->type = rw_cmd;
				dbio->block_number = recid + 1;
				dbio->buffer = __pa(dst);
				dbio++;
				dst += blksize;
				recid++;
			}
		}
	}
	cqr->buildclk = get_clock();
	cqr->device = device;
	cqr->expires = 50 * HZ;	/* 50 seconds */
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

static int
dasd_diag_fill_info(dasd_device_t * device, dasd_information2_t * info)
{
	dasd_diag_private_t *private;

	private = (dasd_diag_private_t *) device->private;
	info->label_block = private->pt_block;
	info->FBA_layout = 1;
	info->format = DASD_FORMAT_LDL;
	info->characteristics_size = sizeof (dasd_diag_characteristics_t);
	memcpy(info->characteristics,
	       &((dasd_diag_private_t *) device->private)->rdc_data,
	       sizeof (dasd_diag_characteristics_t));
	info->confdata_size = 0;
	return 0;
}

static void
dasd_diag_dump_sense(dasd_device_t *device, dasd_ccw_req_t * req,
		     struct irb *stat)
{
	char *page;

	page = (char *) get_zeroed_page(GFP_KERNEL);
	if (page == NULL) {
		MESSAGE(KERN_ERR, "%s", "No memory to dump sense data");
		return;
	}
	sprintf(page, KERN_WARNING PRINTK_HEADER
		"device %s: I/O status report:\n",
		device->cdev->dev.bus_id);

	MESSAGE(KERN_ERR, "Sense data:\n%s", page);

	free_page((unsigned long) page);
}

/*
 * max_blocks is dependent on the amount of storage that is available
 * in the static io buffer for each device. Currently each device has
 * 8192 bytes (=2 pages). dasd diag is only relevant for 31 bit.
 * The dasd_ccw_req_t has 96 bytes, the dasd_diag_req_t has 8 bytes and
 * the diag_bio_t for each block has 16 bytes. 
 * That makes:
 * (8192 - 96 - 8) / 16 = 505.5 blocks at maximum.
 * We want to fit two into the available memory so that we can immediately
 * start the next request if one finishes off. That makes 252.75 blocks
 * for one request. Give a little safety and the result is 240.
 */
dasd_discipline_t dasd_diag_discipline = {
	.owner = THIS_MODULE,
	.name = "DIAG",
	.ebcname = "DIAG",
	.max_blocks = 240,
	.check_device = dasd_diag_check_device,
	.fill_geometry = dasd_diag_fill_geometry,
	.start_IO = dasd_start_diag,
	.examine_error = dasd_diag_examine_error,
	.erp_action = dasd_diag_erp_action,
	.erp_postaction = dasd_diag_erp_postaction,
	.build_cp = dasd_diag_build_cp,
	.dump_sense = dasd_diag_dump_sense,
	.fill_info = dasd_diag_fill_info,
};

static int __init
dasd_diag_init(void)
{
	if (!MACHINE_IS_VM) {
		MESSAGE(KERN_INFO,
			"Machine is not VM: %s discipline not initializing",
			dasd_diag_discipline.name);
		return -EINVAL;
	}
	ASCEBC(dasd_diag_discipline.ebcname, 4);

	ctl_set_bit(0, 9);
	register_external_interrupt(0x2603, dasd_ext_handler);
	return 0;
}

static void __exit
dasd_diag_cleanup(void)
{
	if (!MACHINE_IS_VM) {
		MESSAGE(KERN_INFO,
			"Machine is not VM: %s discipline not initializing",
			dasd_diag_discipline.name);
		return;
	}
	unregister_external_interrupt(0x2603, dasd_ext_handler);
	ctl_clear_bit(0, 9);
}

module_init(dasd_diag_init);
module_exit(dasd_diag_cleanup);

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
 * indent-tabs-mode: 1
 * tab-width: 8
 * End:
 */
