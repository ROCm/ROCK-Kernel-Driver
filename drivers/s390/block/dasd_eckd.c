/* 
 * File...........: linux/drivers/s390/block/dasd_eckd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 */

#include <linux/stddef.h>
#include <linux/kernel.h>

#ifdef MODULE
#include <linux/module.h>
#endif				/* MODULE */

#include <linux/malloc.h>
#include <linux/dasd.h>
#include <asm/io.h>

#include <asm/irq.h>

#include "dasd_types.h"
#include "dasd_ccwstuff.h"


#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(eckd):"

#define ECKD_C0(i) (i->home_bytes)
#define ECKD_F(i) (i -> formula)
#define ECKD_F1(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f1):(i->factors.f_0x02.f1))
#define ECKD_F2(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f2):(i->factors.f_0x02.f2))
#define ECKD_F3(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f3):(i->factors.f_0x02.f3))
#define ECKD_F4(i) (ECKD_F(i)==0x02?(i->factors.f_0x02.f4):0)
#define ECKD_F5(i) (ECKD_F(i)==0x02?(i->factors.f_0x02.f5):0)
#define ECKD_F6(i) (i -> factor6)
#define ECKD_F7(i) (i -> factor7)
#define ECKD_F8(i) (i -> factor8)

#define DASD_ECKD_CCW_LOCATE_RECORD 0x47

#define DASD_ECKD_CCW_READ_HOME_ADDRESS 0x0a
#define DASD_ECKD_CCW_WRITE_HOME_ADDRESS 0x09

#define DASD_ECKD_CCW_READ_RECORD_ZERO 0x16
#define DASD_ECKD_CCW_WRITE_RECORD_ZERO 0x15

#define DASD_ECKD_CCW_READ_COUNT 0x12
#define DASD_ECKD_CCW_READ 0x06
#define DASD_ECKD_CCW_READ_MT 0x86
#define DASD_ECKD_CCW_WRITE 0x05
#define DASD_ECKD_CCW_WRITE_MT 0x85
#define DASD_ECKD_CCW_READ_CKD 0x1e
#define DASD_ECKD_CCW_READ_CKD_MT 0x9e
#define DASD_ECKD_CCW_WRITE_CKD 0x1d
#define DASD_ECKD_CCW_WRITE_CKD_MT 0x9d

typedef
struct {
	__u16 cyl;
	__u16 head;
} __attribute__ ((packed))

ch_t;

typedef
struct {
	__u16 cyl;
	__u16 head;
	__u32 sector;
} __attribute__ ((packed))

chs_t;

typedef
struct {
	__u16 cyl;
	__u16 head;
	__u8 record;
} __attribute__ ((packed))

chr_t;

typedef
struct {
	__u16 cyl;
	__u16 head;
	__u32 sector;
} geom_t;

typedef struct {
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 dev_class;
		__u8 reserved;
		unsigned char dev_type[6];
		unsigned char dev_model[3];
		unsigned char HDA_manufacturer[3];
		unsigned char HDA_location[2];
		unsigned char HDA_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned1;
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 reserved[2];
		unsigned char dev_type[6];
		unsigned char dev_model[3];
		unsigned char DASD_manufacturer[3];
		unsigned char DASD_location[2];
		unsigned char DASD_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned2;
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 reserved[2];
		unsigned char cont_type[6];
		unsigned char cont_model[3];
		unsigned char cont_manufacturer[3];
		unsigned char cont_location[2];
		unsigned char cont_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned3;
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char token_id:1;
			unsigned char sno_valid:1;
			unsigned char subst_sno:1;
			unsigned char recNED:1;
			unsigned char emuNED:1;
			unsigned char reserved:1;
		} __attribute__ ((packed)) flags;
		__u8 descriptor;
		__u8 reserved[2];
		unsigned char cont_type[6];
		unsigned char empty[3];
		unsigned char cont_manufacturer[3];
		unsigned char cont_location[2];
		unsigned char cont_seqno[12];
		__u16 ID;
	} __attribute__ ((packed)) ned4;
	unsigned char ned5[32];
	unsigned char ned6[32];
	unsigned char ned7[32];
	struct {
		struct {
			unsigned char identifier:2;
			unsigned char reserved:6;
		} __attribute__ ((packed)) flags;
		__u8 selector;
		__u16 interfaceID;
		__u32 reserved;
		__u16 subsystemID;
		struct {
			unsigned char sp0:1;
			unsigned char sp1:1;
			unsigned char reserved:5;
			unsigned char scluster:1;
		} __attribute__ ((packed)) spathID;
		__u8 unit_address;
		__u8 dev_ID;
		__u8 dev_address;
		__u8 adapterID;
		__u16 link_address;
		struct {
			unsigned char parallel:1;
			unsigned char escon:1;
			unsigned char reserved:1;
			unsigned char ficon:1;
			unsigned char reserved2:4;
		} __attribute__ ((packed)) protocol_type;
		struct {
			unsigned char PID_in_236:1;
			unsigned char reserved:7;
		} __attribute__ ((packed)) format_flags;
		__u8 log_dev_address;
		unsigned char reserved2[12];
	} __attribute__ ((packed)) neq;

} __attribute__ ((packed))

eckd_confdata_t;

typedef
struct {
	struct {
		unsigned char perm:2;	/* Permissions on this extent */
		unsigned char reserved:1;
		unsigned char seek:2;	/* Seek control */
		unsigned char auth:2;	/* Access authorization */
		unsigned char pci:1;	/* PCI Fetch mode */
	} __attribute__ ((packed)) mask;
	struct {
		unsigned char mode:2;	/* Architecture mode */
		unsigned char ckd:1;	/* CKD Conversion */
		unsigned char operation:3;	/* Operation mode */
		unsigned char cfw:1;	/* Cache fast write */
		unsigned char dfw:1;	/* DASD fast write */
	} __attribute__ ((packed)) attributes;
	__u16 short blk_size;	/* Blocksize */
	__u16 fast_write_id;
	__u8 unused;
	__u8 reserved;
	ch_t beg_ext;
	ch_t end_ext;
} __attribute__ ((packed, aligned (32)))

DE_eckd_data_t;

typedef
struct {
	struct {
		unsigned char orientation:2;
		unsigned char operation:6;
	} __attribute__ ((packed)) operation;
	struct {
		unsigned char last_bytes_used:1;
		unsigned char reserved:6;
		unsigned char read_count_suffix:1;
	} __attribute__ ((packed)) auxiliary;
	__u8 unused;
	__u8 count;
	ch_t seek_addr;
	chr_t search_arg;
	__u8 sector;
	__u16 length;
} __attribute__ ((packed, aligned (32)))

LO_eckd_data_t;

/* Stuff for handling home addresses */
typedef struct {
	__u8 skip_control[14];
	__u16 cell_number;
	__u8 physical_addr[3];
	__u8 flag;
	ch_t track_addr;
	__u8 reserved;
	__u8 key_length;
	__u8 reserved2[2];
} __attribute__ ((packed, aligned (32)))

eckd_home_t;


static unsigned int
round_up_multiple (unsigned int no, unsigned int mult)
{
	int rem = no % mult;
	return (rem ? no - rem + mult : no);
/*      return (no % mult ? no - (no % mult) + mult : no); */
}

static unsigned int
ceil_quot (unsigned int d1, unsigned int d2)
{
	return (d1 + (d2 - 1)) / d2;
}

static int
bytes_per_record (dasd_eckd_characteristics_t * rdc,
		  int kl,	/* key length */
		  int dl /* data length */ )
{
	int bpr = 0;
	switch (rdc->formula) {
	case 0x01:{
			unsigned int fl1, fl2;
			fl1 = round_up_multiple (ECKD_F2 (rdc) + dl,
						 ECKD_F1 (rdc));
			fl2 = round_up_multiple (kl ? ECKD_F2 (rdc) + kl : 0,
						 ECKD_F1 (rdc));
			bpr = fl1 + fl2;
			break;
		}
	case 0x02:{
			unsigned int fl1, fl2, int1, int2;
			int1 = ceil_quot (dl + ECKD_F6 (rdc),
					  ECKD_F5 (rdc) << 1);
			int2 = ceil_quot (kl + ECKD_F6 (rdc),
					  ECKD_F5 (rdc) << 1);
			fl1 = round_up_multiple (ECKD_F1 (rdc) *
						 ECKD_F2 (rdc) +
						 (dl + ECKD_F6 (rdc) +
						  ECKD_F4 (rdc) * int1),
						 ECKD_F1 (rdc));
			fl2 = round_up_multiple (ECKD_F1 (rdc) *
						 ECKD_F3 (rdc) +
						 (kl + ECKD_F6 (rdc) +
						  ECKD_F4 (rdc) * int2),
						 ECKD_F1 (rdc));
			bpr = fl1 + fl2;
			break;
		}
	default:
		INTERNAL_ERROR ("unknown formula%d\n", rdc->formula);
	}
	return bpr;
}

static inline unsigned int
bytes_per_track (dasd_eckd_characteristics_t * rdc)
{
	return *(unsigned int *) (rdc->byte_per_track) >> 8;
}

static unsigned int
recs_per_track (dasd_eckd_characteristics_t * rdc,
		unsigned int kl, unsigned int dl)
{
	int rpt = 0;
	int dn;
        switch ( rdc -> dev_type ) {
	case 0x3380: 
		if (kl)
			return 1499 / (15 +
				       7 + ceil_quot (kl + 12, 32) +
				       ceil_quot (dl + 12, 32));
		else
			return 1499 / (15 + ceil_quot (dl + 12, 32));
	case 0x3390: 
		dn = ceil_quot (dl + 6, 232) + 1;
		if (kl) {
			int kn = ceil_quot (kl + 6, 232) + 1;
			return 1729 / (10 +
				       9 + ceil_quot (kl + 6 * kn, 34) +
				       9 + ceil_quot (dl + 6 * dn, 34));
		} else
			return 1729 / (10 +
				       9 + ceil_quot (dl + 6 * dn, 34));
	case 0x9345: 
	        dn = ceil_quot (dl + 6, 232) + 1;
                if (kl) {
                        int kn = ceil_quot (kl + 6, 232) + 1;
                        return 1420 / (18 +
                                       7 + ceil_quot (kl + 6 * kn, 34) +
                                       ceil_quot (dl + 6 * dn, 34));
                } else
                        return 1420 / (18 +
                                       7 + ceil_quot (dl + 6 * dn, 34));
	}
	return rpt;
}

static
void
define_extent (ccw1_t * de_ccw,
	       DE_eckd_data_t * data,
	       int trk,
	       int totrk,
	       int cmd,
	       dasd_information_t * info)
{
	ch_t geo, beg, end;

	geo.cyl = info->rdc_data->eckd.no_cyl;
	geo.head = info->rdc_data->eckd.trk_per_cyl;
	beg.cyl = trk / geo.head;
	beg.head = trk % geo.head;
	end.cyl = totrk / geo.head;
	end.head = totrk % geo.head;

	memset (de_ccw, 0, sizeof (ccw1_t));
	de_ccw->cmd_code = CCW_DEFINE_EXTENT;
	de_ccw->count = 16;
	de_ccw->cda = (void *) virt_to_phys (data);

	memset (data, 0, sizeof (DE_eckd_data_t));
	switch (cmd) {
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
	case DASD_ECKD_CCW_READ_CKD:	/* Fallthrough */
	case DASD_ECKD_CCW_READ_CKD_MT:
	case DASD_ECKD_CCW_READ_COUNT:
		data->mask.perm = 0x1;
                data->attributes.operation = 0x3; /* enable seq. caching */
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
                data->attributes.operation = 0x3; /* enable seq. caching */
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->attributes.operation = 0x1;	/* format through cache */
		break;
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->mask.perm = 0x3;
		data->mask.auth = 0x1;
		data->attributes.operation = 0x1;	/* format through cache */
		break;
	default:
		INTERNAL_ERROR ("unknown opcode 0x%x\n", cmd);
		break;
	}
	data->attributes.mode = 0x3;
	data->beg_ext.cyl = beg.cyl;
	data->beg_ext.head = beg.head;
	data->end_ext.cyl = end.cyl;
	data->end_ext.head = end.head;
}

static inline void
locate_record (ccw1_t * lo_ccw,
	       LO_eckd_data_t * data,
	       int trk,
	       int rec_on_trk,
	       int no_rec,
	       int cmd,
	       dasd_information_t * info)
{
	ch_t geo =
	{info->rdc_data->eckd.no_cyl,
	 info->rdc_data->eckd.trk_per_cyl};
	ch_t seek =
	{trk / (geo.head), trk % (geo.head)};
	int reclen = info->sizes.bp_block;
	memset (lo_ccw, 0, sizeof (ccw1_t));
	lo_ccw->cmd_code = DASD_ECKD_CCW_LOCATE_RECORD;
	lo_ccw->count = 16;
	lo_ccw->cda = (void *) virt_to_phys (data);

	memset (data, 0, sizeof (LO_eckd_data_t));
	switch (cmd) {
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x03;
		data->count++;
		break;
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		data->count++;
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x01;
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x06;
		break;
	case DASD_ECKD_CCW_READ_CKD:
	case DASD_ECKD_CCW_READ_CKD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_READ_COUNT:
		data->operation.operation = 0x06;
                break;
	default:
		INTERNAL_ERROR ("unknown opcode 0x%x\n", cmd);
	}
	memcpy (&(data->seek_addr), &seek, sizeof (ch_t));
	memcpy (&(data->search_arg), &seek, sizeof (ch_t));
	data->search_arg.record = rec_on_trk;
	data->count += no_rec;
}

void
dasd_eckd_print_error (devstat_t * stat)
{
	int sct, sl;
	char *sense = stat->ii.sense.data;
	PRINT_WARN ("IRQ on devno %x: with intparm:%x DS:0x%02x CS:0x%02x\n",
		    stat->devno, stat->intparm, stat->dstat, stat->cstat);
	PRINT_WARN ("Failing CCW: %p\n", (ccw1_t *) stat->cpa);
	for (sl = 0; sl < 4; sl++) {
		PRINT_DEBUG ("Sense:");
		for (sct = 0; sct < 8; sct++) {
			printk (" %2d:0x%02x",
				8 * sl + sct, sense[8 * sl + sct]);
		}
		printk ("\n");
	}
	if (sense[27] & 0x80) {	/* 32 Byte Sense Data */
		PRINT_INFO ("Sense Data is 32 Byte information\n");
		PRINT_INFO ("Format: %x Exception class %x\n",
			    sense[6] & 0x0f, sense[22] >> 4);
	} else {		/* 24 Byte Sense Data */
		PRINT_INFO ("Sense Data is 24 Byte information\n");
		PRINT_INFO ("FMT: %x MSG %x, %s MSGb to SYSOP\n",
			    sense[7] >> 4, sense[7] & 0x0f,
			    sense[1] & 0x10 ? "" : "no");
	}
}

int
dasd_eckd_format_track (int di, int trk, int bs)
{
	int rc = 0;
	int i;
	int flags = 0x00;	/* FORMAT_R0 = 0x01, FORMAT_HA = 0x03 */
        dasd_information_t * info=dasd_info[di];
	cqr_t *fcp;
	DE_eckd_data_t *DE_data;
	LO_eckd_data_t *LO_data;
	eckd_count_t *ct_data;
	eckd_count_t *r0_data;
	ccw1_t *last_ccw;
        int retries = 5;

	int rpt = recs_per_track (&(info->rdc_data->eckd), 0, bs);
	int cyl = trk / info->rdc_data->eckd.trk_per_cyl;
	int head = trk % info->rdc_data->eckd.trk_per_cyl;

	fcp = request_cqr (2 + 1 + rpt,
			   sizeof (DE_eckd_data_t) +
			   sizeof (LO_eckd_data_t) +
			   (rpt + 1) * sizeof (eckd_count_t));
        fcp -> devindex=di;
	DE_data = (DE_eckd_data_t *) fcp->data;
	LO_data = (LO_eckd_data_t *) (((long) DE_data) +
				      sizeof (DE_eckd_data_t));
	r0_data = (eckd_count_t *) (((long) LO_data) +
				    sizeof (LO_eckd_data_t));
	ct_data = (eckd_count_t *) (((long) r0_data) +
				    sizeof (eckd_count_t));
	last_ccw = fcp->cpaddr;
	switch (flags) {
	case 0x03:
		define_extent (last_ccw, DE_data, trk, trk,
			       DASD_ECKD_CCW_WRITE_HOME_ADDRESS, info);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		locate_record (last_ccw, LO_data, trk, 0, rpt,
			       DASD_ECKD_CCW_WRITE_HOME_ADDRESS, info);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		break;
	case 0x01:
		define_extent (last_ccw, DE_data, trk, trk,
			       DASD_ECKD_CCW_WRITE_RECORD_ZERO, info);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		locate_record (last_ccw, LO_data, trk, 0, rpt,
			       DASD_ECKD_CCW_WRITE_RECORD_ZERO, info);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		break;
	case 0x00:
		define_extent (last_ccw, DE_data, trk, trk,
			       DASD_ECKD_CCW_WRITE_CKD, info);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		locate_record (last_ccw, LO_data, trk, 0, rpt,
			       DASD_ECKD_CCW_WRITE_CKD, info);
                LO_data->length = bs;
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		break;
	default:
		PRINT_WARN ("Unknown format flags...%d\n", flags);
		return -EINVAL;
	}
	if (flags & 0x02) {
		PRINT_WARN ("Unsupported format flag...%d\n", flags);
		return -EINVAL;
	}
	if (flags & 0x01) {	/* write record zero */
		memset (r0_data, 0, sizeof (eckd_count_t));
		r0_data->cyl = cyl;
		r0_data->head = head;
		r0_data->record = 0;
		r0_data->kl = 0;
		r0_data->dl = 8;
		last_ccw->cmd_code = 0x03;
		last_ccw->count = 8;
		last_ccw->flags = CCW_FLAG_CC | CCW_FLAG_SLI;
		last_ccw->cda = (void *) virt_to_phys (r0_data);
		last_ccw++;
	}
	/* write remaining records */
	for (i = 0; i < rpt; i++, last_ccw++) {
		memset (ct_data + i, 0, sizeof (eckd_count_t));
		(ct_data + i)->cyl = cyl;
		(ct_data + i)->head = head;
		(ct_data + i)->record = i + 1;
		(ct_data + i)->kl = 0;
		(ct_data + i)->dl = bs;
		last_ccw->cmd_code = DASD_ECKD_CCW_WRITE_CKD;
		last_ccw->flags = CCW_FLAG_CC | CCW_FLAG_SLI;
		last_ccw->count = 8;
		last_ccw->cda = (void *)
                        virt_to_phys (ct_data + i);
	}
	(last_ccw - 1)->flags &= ~(CCW_FLAG_CC | CCW_FLAG_DC);
        fcp -> devindex = di;
        fcp -> flags = DASD_DO_IO_SLEEP;
        do {
                DECLARE_WAITQUEUE(wait, current);
                unsigned long flags;
                int irq;
                int cs;

                irq = dasd_info[fcp->devindex]->info.irq;
                s390irq_spin_lock_irqsave (irq, flags);
                atomic_set(&fcp->status,CQR_STATUS_QUEUED);
                rc = dasd_start_IO ( fcp );
                add_wait_queue (&dasd_waitq, &wait);
                do {
                        current->state = TASK_UNINTERRUPTIBLE;
                        s390irq_spin_unlock_irqrestore (irq, flags);
                        schedule ();
                        s390irq_spin_lock_irqsave (irq, flags);
                } while (((cs = atomic_read (&fcp->status)) !=
                          CQR_STATUS_DONE) &&
                         (cs != CQR_STATUS_ERROR));
                remove_wait_queue (&dasd_waitq, &wait);
		s390irq_spin_unlock_irqrestore (irq, flags);

                retries --;
	} while ( (rc || (atomic_read(&fcp->status) != CQR_STATUS_DONE)) &&
                  retries);
        if ((rc || (atomic_read(&fcp->status) != CQR_STATUS_DONE)))
                rc = -EIO;
	release_cqr (fcp);
	return rc;
}

int
dasd_eckd_ck_devinfo (dev_info_t * info)
{
	return 0;
}

cqr_t *
dasd_eckd_build_req (int devindex,
		     struct request * req)
{
	cqr_t *rw_cp = NULL;
	ccw1_t *ccw;

	DE_eckd_data_t *DE_data;
	LO_eckd_data_t *LO_data;
	struct buffer_head *bh;
	int rw_cmd;
	dasd_information_t *info = dasd_info[devindex];
	int blk_per_trk = recs_per_track (&(info->rdc_data->eckd),
					  0, info->sizes.bp_block);
	int byt_per_blk = info->sizes.bp_block;
	int noblk = req-> nr_sectors >> info->sizes.s2b_shift;
	int btrk = (req->sector >> info->sizes.s2b_shift) / blk_per_trk;
	int etrk = ((req->sector + req->nr_sectors - 1) >>
		    info->sizes.s2b_shift) / blk_per_trk;

        if ( ! noblk ) {
                PRINT_ERR("No blocks to write...returning\n");
                return NULL;
        }

	if (req->cmd == READ) {
		rw_cmd = DASD_ECKD_CCW_READ_MT;
	} else
#if DASD_PARANOIA > 2
	if (req->cmd == WRITE)
#endif				/* DASD_PARANOIA */
	{
		rw_cmd = DASD_ECKD_CCW_WRITE_MT;
	}
#if DASD_PARANOIA > 2
	else {
		PRINT_ERR ("Unknown command %d\n", req->cmd);
		return NULL;
	}
#endif				/* DASD_PARANOIA */
	/* Build the request */
	rw_cp = request_cqr (2 + noblk,
			     sizeof (DE_eckd_data_t) +
			     sizeof (LO_eckd_data_t));
        if ( ! rw_cp ) {
          return NULL;
        }
	DE_data = rw_cp->data;
	LO_data = rw_cp->data + sizeof (DE_eckd_data_t);
	ccw = rw_cp->cpaddr;

	define_extent (ccw, DE_data, btrk, etrk, rw_cmd, info);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, LO_data, btrk,
		       (req->sector >> info->sizes.s2b_shift) %
		       blk_per_trk + 1,
		       req->nr_sectors >> info->sizes.s2b_shift,
		       rw_cmd, info);
	ccw->flags = CCW_FLAG_CC;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
                long size;
		for (size = 0; size < bh->b_size; size += byt_per_blk) {
                        ccw++;
                        ccw->flags = CCW_FLAG_CC;
                        ccw->cmd_code = rw_cmd;
                        ccw->count = byt_per_blk;
                        ccw->cda = (void *) virt_to_phys (bh->b_data + size);
		}
	}
	ccw->flags &= ~(CCW_FLAG_DC | CCW_FLAG_CC);
	return rw_cp;
}

cqr_t *
dasd_eckd_rw_label (int devindex, int rw, char *buffer)
{
	int cmd_code = 0x03;
	dasd_information_t *info = dasd_info[devindex];
	cqr_t *cqr;
	ccw1_t *ccw;

	switch (rw) {
	case READ:
		cmd_code = DASD_ECKD_CCW_READ;
		break;
	case WRITE:
		cmd_code = DASD_ECKD_CCW_WRITE;
		break;
#if DASD_PARANOIA > 2
	default:
		INTERNAL_ERROR ("unknown cmd %d", rw);
		return NULL;
#endif				/* DASD_PARANOIA */
	}
	cqr = request_cqr (3, sizeof (DE_eckd_data_t) +
			   sizeof (LO_eckd_data_t));
	ccw = cqr->cpaddr;
	define_extent (ccw, cqr->data, 0, 0, cmd_code, info);
	ccw->flags |= CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, cqr->data + 1, 0, 2, 1, cmd_code, info);
	ccw->flags |= CCW_FLAG_CC;
	ccw++;
	ccw->cmd_code = cmd_code;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = sizeof (dasd_volume_label_t);
	ccw->cda = (void *) virt_to_phys ((void *) buffer);
	return cqr;

}

void
dasd_eckd_print_char (dasd_characteristics_t * i)
{
        dasd_eckd_characteristics_t * c = 
                (dasd_eckd_characteristics_t *)i;
	PRINT_INFO ("%x/%x (%x/%x) Cyl: %d Head: %d Sec: %d \n",
		    c->dev_type, c->dev_model,
		    c->cu_type, c->cu_model.model,
		    c->no_cyl, c->trk_per_cyl,
		    c->sec_per_trk);
	PRINT_INFO ("Estimate: %d Byte/trk %d byte/kByte %d kByte/trk \n",
		    bytes_per_track (c),
		    bytes_per_record (c, 0, 1024),
		    recs_per_track (c, 0, 1024));
};

int
dasd_eckd_ck_char (dasd_characteristics_t * i)
{
	int rc = 0;
	dasd_eckd_print_char (i);
	return rc;
}

int
dasd_eckd_format (int devindex, format_data_t * fdata)
{
	int rc = 0;
	int i;
	dasd_information_t *info = dasd_info[devindex];
	format_data_t fd;

	if (!fdata) {
		fd.start_unit = 0;
		fd.stop_unit = info->rdc_data->eckd.no_cyl *
                        info->rdc_data->eckd.trk_per_cyl - 1;
		fd.blksize = 4096;
	} else {
		memcpy (&fd, fdata, sizeof (format_data_t));
		if ( fd.stop_unit == -1 ) {
                        fd.stop_unit = info->rdc_data->eckd.no_cyl *
                                info->rdc_data->eckd.trk_per_cyl - 1;
		}
                if ( fd.blksize == 0 ) {
                        fd.blksize = 4096;
                }
	}
        PRINT_INFO("Formatting device %d from %d to %d with bs %d\n",
                   devindex,fd.start_unit,fd.stop_unit,fd.blksize);
        if ( fd.start_unit > fd.stop_unit ) {
                PRINT_WARN ("start unit .gt. stop unit\n");
                return -EINVAL;
        }
        if ( (fd.start_unit > info->rdc_data->eckd.no_cyl *
              info->rdc_data->eckd.trk_per_cyl - 1) ) {
                PRINT_WARN ("start unit beyond end of disk\n");
                return -EINVAL;
        }
        if ( (fd.stop_unit > info->rdc_data->eckd.no_cyl *
              info->rdc_data->eckd.trk_per_cyl - 1) ) {
                PRINT_WARN ("stop unit beyond end of disk\n");
                return -EINVAL;
        }
        switch (fd.blksize) {
        case 512:
        case 1024:
        case 2048:
        case 4096:
                break;
        default:
                PRINT_WARN ("invalid blocksize\n");
                return -EINVAL;
        }
	for (i = fd.start_unit; i <= fd.stop_unit; i++) {
                /* print 20 messages per disk at all */
                if ( ! ( i % (info->rdc_data->eckd.trk_per_cyl  *
                                (info->rdc_data->eckd.no_cyl / 20 ) )))  {
                        PRINT_INFO ("Format %d Cylinder: %d\n",devindex,
                                    i/info->rdc_data->eckd.trk_per_cyl);
                }
		rc = dasd_eckd_format_track (devindex, i, fd.blksize);
		if (rc) {
			PRINT_WARN ("Formatting of Track %d failed...exiting\n", i);
			break;
		}
	}
        PRINT_INFO("Formated device %d from %d to %d with bs %d\n",
                   devindex,fd.start_unit,fd.stop_unit,fd.blksize);
	return rc;
}

cqr_t *
dasd_eckd_fill_sizes_first (int di)
{
	cqr_t *rw_cp = NULL;
	ccw1_t *ccw;
	DE_eckd_data_t *DE_data;
	LO_eckd_data_t *LO_data;
	dasd_information_t *info = dasd_info[di];
	eckd_count_t *count_data= &(info->private.eckd.count_data);
	rw_cp = request_cqr (3,
			     sizeof (DE_eckd_data_t) +
			     sizeof (LO_eckd_data_t));
	DE_data = rw_cp->data;
	LO_data = rw_cp->data + sizeof (DE_eckd_data_t);
	ccw = rw_cp->cpaddr;
	define_extent (ccw, DE_data, 0, 0, DASD_ECKD_CCW_READ_COUNT, info);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, LO_data, 0, 1, 1, DASD_ECKD_CCW_READ_COUNT, info);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	ccw->cmd_code = DASD_ECKD_CCW_READ_COUNT;
	ccw->count = 8;
	ccw->cda = (void *) __pa (count_data);
	rw_cp->devindex = di;
        atomic_set(&rw_cp->status,CQR_STATUS_FILLED);
	return rw_cp;
}

int dasd_eckd_fill_sizes_last (int devindex) 
{
	int sb;
	dasd_information_t *in = dasd_info[devindex];
	int bs = in->private.eckd.count_data.dl;
	if (bs <= 0) {
                PRINT_INFO("Cannot figure out blocksize. did you format the disk?\n");
                memset (&(in -> sizes), 0, sizeof(dasd_sizes_t ));
                return -EMEDIUMTYPE;
	} else {
		in->sizes.bp_block = bs;
	}
	in->sizes.bp_sector = in->sizes.bp_block;
        
	in->sizes.b2k_shift = 0; /* bits to shift a block to get 1k */
	for (sb = 1024; sb < bs; sb = sb << 1)
                in->sizes.b2k_shift++;
        
	in->sizes.s2b_shift = 0; /* bits to shift 512 to get a block */
	for (sb = 512; sb < bs; sb = sb << 1)
                in->sizes.s2b_shift++;
        
	in->sizes.blocks = in->rdc_data->eckd.no_cyl *
                in->rdc_data->eckd.trk_per_cyl *
                recs_per_track (&(in->rdc_data->eckd), 0, bs);
	in->sizes.kbytes = in->sizes.blocks << in->sizes.b2k_shift;
        
	PRINT_INFO ("Verified: %d B/trk %d B/Blk(%d B) %d Blks/trk %d kB/trk \n",
		    bytes_per_track (&(in->rdc_data->eckd)),
		    bytes_per_record (&(in->rdc_data->eckd), 0, in->sizes.bp_block),
                    in->sizes.bp_block,
		    recs_per_track (&(in->rdc_data->eckd), 0, in->sizes.bp_block),
		    (recs_per_track (&(in->rdc_data->eckd), 0, in->sizes.bp_block) <<
                     in->sizes.b2k_shift ));
                    return 0;
}

dasd_operations_t dasd_eckd_operations =
{
	ck_devinfo:	dasd_eckd_ck_devinfo,
	get_req_ccw:	dasd_eckd_build_req,
	rw_label:	dasd_eckd_rw_label,
	ck_characteristics: 	dasd_eckd_ck_char,
	fill_sizes_first:	dasd_eckd_fill_sizes_first,
	fill_sizes_last:	dasd_eckd_fill_sizes_last,
	dasd_format:	dasd_eckd_format,
};

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
