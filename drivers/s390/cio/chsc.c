/*
 *  drivers/s390/cio/chsc.c
 *   S/390 common I/O routines -- channel subsystem call
 *   $Revision: 1.78 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/cio.h>
#include <asm/ccwdev.h> // FIXME: layering violation, remove this

#include "css.h"
#include "cio.h"
#include "cio_debug.h"
#include "device.h" // FIXME: layering violation, remove this
#include "ioasm.h"
#include "chsc.h"

#define CHPID_LONGS (256 / (8 * sizeof(long))) /* 256 chpids */
static unsigned long chpids[CHPID_LONGS];
static unsigned long chpids_logical[CHPID_LONGS] = {[0 ... CHPID_LONGS-1] -1};
static unsigned long chpids_known[CHPID_LONGS];

static struct channel_path *chps[NR_CHPIDS];

static int cio_chsc_desc_avail;

static int new_channel_path(int chpid, int status);

static int
set_chp_status(int chp, int status)
{
	if (chps[chp] == NULL)
		return -EINVAL;

	chps[chp]->state = status;
	return 0;
}

static inline int
chsc_chpid_logical (struct subchannel *sch, int chp)
{
	return test_bit (sch->schib.pmcw.chpid[chp], chpids_logical);
}

void
chsc_validate_chpids(struct subchannel *sch)
{
	int mask, chp;

	if (sch->lpm == 0)
		return;

	for (chp = 0; chp <= 7; chp++) {
		mask = 0x80 >> chp;
		if (sch->lpm & mask)
			if (!chsc_chpid_logical(sch, chp))
				/* disable using this path */
				sch->lpm &= ~mask;
	}
}

/* FIXME: this is _always_ called for every subchannel. shouldn't we
 *	  process more than one at a time? */
static int
chsc_get_sch_desc_irq(int irq, void *page)
{
	int ccode, chpid, j;

	struct {
		struct chsc_header request;
		u16 reserved1;
		u16 f_sch;	  /* first subchannel */
		u16 reserved2;
		u16 l_sch;	  /* last subchannel */
		u32 reserved3;
		struct chsc_header response;
		u32 reserved4;
		u8 sch_valid : 1;
		u8 dev_valid : 1;
		u8 st	     : 3; /* subchannel type */
		u8 zeroes    : 3;
		u8  unit_addr;	  /* unit address */
		u16 devno;	  /* device number */
		u8 path_mask;
		u8 fla_valid_mask;
		u16 sch;	  /* subchannel */
		u8 chpid[8];	  /* chpids 0-7 */
		u16 fla[8];	  /* full link addresses 0-7 */
	} *ssd_area;

	ssd_area = page;

	ssd_area->request = (struct chsc_header) {
		.length = 0x0010,
		.code   = 0x0004,
	};

	ssd_area->f_sch = irq;
	ssd_area->l_sch = irq;

	ccode = chsc(ssd_area);
	if (ccode > 0) {
		pr_debug("chsc returned with ccode = %d\n", ccode);
		return (ccode == 3) ? -ENODEV : -EBUSY;
	}

	switch (ssd_area->response.code) {
	case 0x0001: /* everything ok */
		break;
	case 0x0002:
		CIO_CRW_EVENT(2, "Invalid command!\n");
	case 0x0003:
		CIO_CRW_EVENT(2, "Error in chsc request block!\n");
		return -EINVAL;
		break;
	case 0x0004:
		CIO_CRW_EVENT(2, "Model does not provide ssd\n");
		return -EOPNOTSUPP;
		break;
	default:
		CIO_CRW_EVENT(2, "Unknown CHSC response %d\n",
			      ssd_area->response.code);
		return -EIO;
		break;
	}

	/*
	 * ssd_area->st stores the type of the detected
	 * subchannel, with the following definitions:
	 *
	 * 0: I/O subchannel:	  All fields have meaning
	 * 1: CHSC subchannel:	  Only sch_val, st and sch
	 *			  have meaning
	 * 2: Message subchannel: All fields except unit_addr
	 *			  have meaning
	 * 3: ADM subchannel:	  Only sch_val, st and sch
	 *			  have meaning
	 *
	 * Other types are currently undefined.
	 */
	if (ssd_area->st > 3) { /* uhm, that looks strange... */
		CIO_CRW_EVENT(0, "Strange subchannel type %d"
			      " for sch %x\n", ssd_area->st, irq);
		/*
		 * There may have been a new subchannel type defined in the
		 * time since this code was written; since we don't know which
		 * fields have meaning and what to do with it we just jump out
		 */
		return 0;
	} else {
		const char *type[4] = {"I/O", "chsc", "message", "ADM"};
		CIO_CRW_EVENT(6, "ssd: sch %x is %s subchannel\n",
			      irq, type[ssd_area->st]);
		if (ioinfo[irq] == NULL)
			/* FIXME: we should do device rec. here... */
			return 0;

		ioinfo[irq]->ssd_info.valid = 1;
		ioinfo[irq]->ssd_info.type = ssd_area->st;
	}

	if (ssd_area->st == 0 || ssd_area->st == 2) {
		for (j = 0; j < 8; j++) {
			if (!((0x80 >> j) & ssd_area->path_mask &
			      ssd_area->fla_valid_mask))
				continue;
			chpid = ssd_area->chpid[j];
			if (chpid
			    && (!test_and_set_bit (chpid, chpids_known))
			    && (test_bit (chpid, chpids_logical)))
				set_bit	 (chpid, chpids);

			ioinfo[irq]->ssd_info.chpid[j] = chpid;
			ioinfo[irq]->ssd_info.fla[j]   = ssd_area->fla[j];
		}
	}

	return 0;
}

static int
chsc_get_sch_descriptions(void)
{
	int irq;
	int err;
	void *page;

	CIO_TRACE_EVENT( 4, "gsdesc");

	/*
	 * get information about chpids and link addresses
	 * by executing the chsc command 'store subchannel description'
	 */
	page = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!page)
		return -ENOMEM;

	err = 0;
	for (irq = 0; irq <= highest_subchannel; irq++) {
		/*
		 * retrieve information for each sch
		 */
		err = chsc_get_sch_desc_irq(irq, page);
		if (err) {
			static int cio_chsc_err_msg;

			if (!cio_chsc_err_msg) {
				printk(KERN_ERR
				       "chsc_get_sch_descriptions:"
				       " Error %d while doing chsc; "
				       "processing some machine checks may "
				       "not work\n", err);
				cio_chsc_err_msg = 1;
			}
			goto out;
		}
		clear_page(page);
	}
	cio_chsc_desc_avail = 1;
out:
	free_page((unsigned long)page);
	return err;
}

__initcall(chsc_get_sch_descriptions);


static inline void
s390_subchannel_remove_chpid(struct subchannel *sch, __u8 chpid)
{
	int j;
	int mask;

	for (j = 0; j < 8; j++)
		if (sch->schib.pmcw.chpid[j] == chpid)
			break;
	if (j >= 8)
		return;

	mask = 0x80 >> j;
	spin_lock(&sch->lock);

	chsc_validate_chpids(sch);

	stsch(sch->irq, &sch->schib);
	if (sch->vpm == mask) {
		dev_fsm_event(sch->dev.driver_data, DEV_EVENT_NOTOPER);
		goto out_unlock;
	}
	if ((sch->schib.scsw.actl & (SCSW_ACTL_CLEAR_PEND |
				     SCSW_ACTL_HALT_PEND |
				     SCSW_ACTL_START_PEND |
				     SCSW_ACTL_RESUME_PEND)) &&
	    (sch->schib.pmcw.lpum == mask)) {
		int cc = cio_cancel(sch);
		
		if (cc == -ENODEV) {
			dev_fsm_event(sch->dev.driver_data, DEV_EVENT_NOTOPER);
			goto out_unlock;
		}

		if (cc == -EINVAL) {
			struct ccw_device *cdev;

			cc = cio_clear(sch);
			if (cc == -ENODEV) {
				dev_fsm_event(sch->dev.driver_data,
					      DEV_EVENT_NOTOPER);
				goto out_unlock;
			}
			/* Call handler. */
			cdev = sch->dev.driver_data;
			cdev->private->state = DEV_STATE_CLEAR_VERIFY;
			if (cdev->handler)
				cdev->handler(cdev, cdev->private->intparm,
					      ERR_PTR(-EIO));
			goto out_unlock;
		}
	} else if ((sch->schib.scsw.actl & SCSW_ACTL_DEVACT) &&
		   (sch->schib.scsw.actl & SCSW_ACTL_SCHACT) &&
		   (sch->schib.pmcw.lpum == mask)) {
		struct ccw_device *cdev;
		int cc;

		cc = cio_clear(sch);
		if (cc == -ENODEV) {
			dev_fsm_event(sch->dev.driver_data, DEV_EVENT_NOTOPER);
			goto out_unlock;
		}
		/* Call handler. */
		cdev = sch->dev.driver_data;
		cdev->private->state = DEV_STATE_CLEAR_VERIFY;
		if (cdev->handler)
			cdev->handler(cdev, cdev->private->intparm,
				      ERR_PTR(-EIO));
		goto out_unlock;
	}

	/* trigger path verification. */
	dev_fsm_event(sch->dev.driver_data, DEV_EVENT_VERIFY);
out_unlock:
	spin_unlock(&sch->lock);
}

/* FIXME: don't iterate all subchannels but use driver_for_each_dev */
static inline void
s390_set_chpid_offline( __u8 chpid)
{
	char dbf_txt[15];
	struct subchannel *sch;
	int irq;

	sprintf(dbf_txt, "chpr%x", chpid);
	CIO_TRACE_EVENT(2, dbf_txt);

	clear_bit(chpid, chpids);
	if (!test_and_clear_bit(chpid, chpids_known))
		return;	 /* we didn't know the chpid anyway */

	set_chp_status(chpid, CHP_OFFLINE);

#if 0
	driver_for_each_dev(io_subchannel_driver, chpid, s390_subchannel_remove_chpid);
#else

	for (irq = 0; irq <= highest_subchannel; irq++) {
		sch = ioinfo[irq];
		if (sch == NULL)
			continue;  /* we don't know the device anyway */

		s390_subchannel_remove_chpid(sch, chpid);
	}
#endif
}

static int
s390_process_res_acc_sch(u8 chpid, __u16 fla, u32 fla_mask,
			 struct subchannel *sch, void *page)
{
	int found;
	int chp;
	int ccode;
	
	/* Update our ssd_info */
	if (chsc_get_sch_desc_irq(sch->irq, page))
		return 0;
	
	found = 0;
	for (chp = 0; chp <= 7; chp++)
		/*
		 * check if chpid is in information updated by ssd
		 */
		if (sch->ssd_info.valid &&
		    sch->ssd_info.chpid[chp] == chpid &&
		    (sch->ssd_info.fla[chp] & fla_mask) == fla) {
			found = 1;
			break;
		}
	
	if (found == 0)
		return 0;

	/*
	 * Do a stsch to update our subchannel structure with the
	 * new path information and eventually check for logically
	 * offline chpids.
	 */
	ccode = stsch(sch->irq, &sch->schib);
	if (ccode > 0)
		return 0;

	return 0x80 >> chp;
}

static void
s390_process_res_acc (u8 chpid, __u16 fla, u32 fla_mask)
{
	struct subchannel *sch;
	int irq;
	int ret;
	char dbf_txt[15];
	void *page;

	sprintf(dbf_txt, "accpr%x", chpid);
	CIO_TRACE_EVENT( 2, dbf_txt);
	if (fla != 0) {
		sprintf(dbf_txt, "fla%x", fla);
		CIO_TRACE_EVENT( 2, dbf_txt);
	}

	/*
	 * I/O resources may have become accessible.
	 * Scan through all subchannels that may be concerned and
	 * do a validation on those.
	 * The more information we have (info), the less scanning
	 * will we have to do.
	 */

	if (!test_bit(chpid, chpids_logical))
		return; /* no need to do the rest */

	page = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!page)
		return;

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		int chp_mask;

		sch = ioinfo[irq];
		if (!sch) {
			/*
			 * We don't know the device yet, but since a path
			 * may be available now to the device we'll have
			 * to do recognition again.
			 * Since we don't have any idea about which chpid
			 * that beast may be on we'll have to do a stsch
			 * on all devices, grr...
			 */
			ret = css_probe_device(irq);
			if (ret == -ENXIO)
				/* We're through */
				break;
			continue;
		}
	
		spin_lock_irq(&sch->lock);

		chp_mask = s390_process_res_acc_sch(chpid, fla, fla_mask,
						    sch, page);
		clear_page(page);

		if (chp_mask == 0) {

			spin_unlock_irq(&sch->lock);

			if (fla_mask != 0)
				break;
			else
				continue;
		}

		sch->lpm = (sch->schib.pmcw.pim &
			    sch->schib.pmcw.pam &
			    sch->schib.pmcw.pom)
			| chp_mask;

		chsc_validate_chpids(sch);

		dev_fsm_event(sch->dev.driver_data, DEV_EVENT_VERIFY);

		spin_unlock_irq(&sch->lock);

		if (fla_mask != 0)
			break;
	}
	free_page((unsigned long)page);
}

static int
__get_chpid_from_lir(void *data)
{
	struct lir {
		u8  iq;
		u8  ic;
		u16 sci;
		/* incident-node descriptor */
		u32 indesc[28];
		/* attached-node descriptor */
		u32 andesc[28];
		/* incident-specific information */
		u32 isinfo[28];
	} *lir;

	lir = (struct lir*) data;
	if (!(lir->iq&0x80))
		/* NULL link incident record */
		return -EINVAL;
	if (!(lir->indesc[0]&0xc0000000))
		/* node descriptor not valid */
		return -EINVAL;
	if (!(lir->indesc[0]&0x10000000))
		/* don't handle device-type nodes - FIXME */
		return -EINVAL;
	/* Byte 3 contains the chpid. Could also be CTCA, but we don't care */

	return (u16) (lir->indesc[0]&0x000000ff);
}

static void
do_process_crw(void *ignore)
{
	int chpid;
	struct {
		struct chsc_header request;
		u32 reserved1;
		u32 reserved2;
		u32 reserved3;
		struct chsc_header response;
		u32 reserved4;
		u8  flags;
		u8  vf;		/* validity flags */
		u8  rs;		/* reporting source */
		u8  cc;		/* content code */
		u16 fla;	/* full link address */
		u16 rsid;	/* reporting source id */
		u32 reserved5;
		u32 reserved6;
		u32 ccdf[96];	/* content-code dependent field */
		/* ccdf has to be big enough for a link-incident record */
	} *sei_area;

	/*
	 * build the chsc request block for store event information
	 * and do the call
	 */
	sei_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);

	if (!sei_area) {
		CIO_CRW_EVENT(0, "No memory for sei area!\n");
		return;
	}

	CIO_TRACE_EVENT( 2, "prcss");

	do {
		int ccode;
		memset(sei_area, 0, sizeof(*sei_area));

		sei_area->request = (struct chsc_header) {
			.length = 0x0010,
			.code   = 0x000e,
		};

		ccode = chsc(sei_area);
		if (ccode > 0)
			goto out;

		switch (sei_area->response.code) {
			/* for debug purposes, check for problems */
		case 0x0001:
			CIO_CRW_EVENT(4, "chsc_process_crw: event information "
					"successfully stored\n");
			break; /* everything ok */
		case 0x0002:
			CIO_CRW_EVENT(2,
				      "chsc_process_crw: invalid command!\n");
			goto out;
		case 0x0003:
			CIO_CRW_EVENT(2, "chsc_process_crw: error in chsc "
				      "request block!\n");
			goto out;
		case 0x0005:
			CIO_CRW_EVENT(2, "chsc_process_crw: no event "
				      "information stored\n");
			goto out;
		default:
			CIO_CRW_EVENT(2, "chsc_process_crw: chsc response %d\n",
				      sei_area->response.code);
			goto out;
		}

		/* Check if we might have lost some information. */
		if (sei_area->flags & 0x40)
			CIO_CRW_EVENT(2, "chsc_process_crw: Event information "
				       "has been lost due to overflow!\n");

		if (sei_area->rs != 4) {
			CIO_CRW_EVENT(2, "chsc_process_crw: reporting source "
				      "(%04X) isn't a chpid!\n",
				      sei_area->rsid);
			continue;
		}

		/* which kind of information was stored? */
		switch (sei_area->cc) {
		case 1: /* link incident*/
			CIO_CRW_EVENT(4, "chsc_process_crw: "
				      "channel subsystem reports link incident,"
				      " reporting source is chpid %x\n",
				      sei_area->rsid);
			chpid = __get_chpid_from_lir(sei_area->ccdf);
			if (chpid < 0)
				CIO_CRW_EVENT(4, "%s: Invalid LIR, skipping\n",
					      __FUNCTION__);
			else
				s390_set_chpid_offline(chpid);
			break;
			
		case 2: /* i/o resource accessibiliy */
			CIO_CRW_EVENT(4, "chsc_process_crw: "
				      "channel subsystem reports some I/O "
				      "devices may have become accessible\n");
			pr_debug("Data received after sei: \n");
			pr_debug("Validity flags: %x\n", sei_area->vf);
			
			/* allocate a new channel path structure, if needed */
			if (chps[sei_area->rsid] == NULL)
				new_channel_path(sei_area->rsid, CHP_ONLINE);
			else
				set_chp_status(sei_area->rsid, CHP_ONLINE);
			
			if ((sei_area->vf & 0x80) == 0) {
				pr_debug("chpid: %x\n", sei_area->rsid);
				s390_process_res_acc(sei_area->rsid, 0, 0);
			} else if ((sei_area->vf & 0xc0) == 0x80) {
				pr_debug("chpid: %x link addr: %x\n",
					 sei_area->rsid, sei_area->fla);
				s390_process_res_acc(sei_area->rsid,
						     sei_area->fla, 0xff00);
			} else if ((sei_area->vf & 0xc0) == 0xc0) {
				pr_debug("chpid: %x full link addr: %x\n",
					 sei_area->rsid, sei_area->fla);
				s390_process_res_acc(sei_area->rsid,
						     sei_area->fla, 0xffff);
			}
			pr_debug("\n");
			
			break;
			
		default: /* other stuff */
			CIO_CRW_EVENT(4, "chsc_process_crw: event %d\n",
				      sei_area->cc);
			break;
		}
	} while (sei_area->flags & 0x80);

out:
	free_page((unsigned long)sei_area);
}

void
chsc_process_crw(void)
{
	static DECLARE_WORK(work, do_process_crw, 0);

	queue_work(ccw_device_work, &work);
}

static void
chp_add(int chpid)
{
	struct subchannel *sch;
	int irq, ret;
	char dbf_txt[15];

	if (!test_bit(chpid, chpids_logical))
		return; /* no need to do the rest */
	
	sprintf(dbf_txt, "cadd%x", chpid);
	CIO_TRACE_EVENT(2, dbf_txt);

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		int i;

		sch = ioinfo[irq];
		if (!sch) {
			ret = css_probe_device(irq);
			if (ret == -ENXIO)
				/* We're through */
				return;
			continue;
		}
	
		spin_lock(&sch->lock);
		for (i=0; i<8; i++)
			if (sch->schib.pmcw.chpid[i] == chpid) {
				if (stsch(sch->irq, &sch->schib) != 0) {
					/* Endgame. */
					spin_unlock(&sch->lock);
					return;
				}
				break;
			}
		if (i==8) {
			spin_unlock(&sch->lock);
			return;
		}
		sch->lpm = (sch->schib.pmcw.pim &
			    sch->schib.pmcw.pam &
			    sch->schib.pmcw.pom)
			| 0x80 >> i;

		chsc_validate_chpids(sch);

		dev_fsm_event(sch->dev.driver_data, DEV_EVENT_VERIFY);

		spin_unlock(&sch->lock);
	}
}

/* 
 * Handling of crw machine checks with channel path source.
 */
void
chp_process_crw(int chpid, int on)
{
	if (on == 0) {
		/* Path has gone. We use the link incident routine.*/
		s390_set_chpid_offline(chpid);
	} else {
		/* 
		 * Path has come. Allocate a new channel path structure,
		 * if needed. 
		 */
		if (chps[chpid] == NULL)
			new_channel_path(chpid, CHP_ONLINE);
		else
			set_chp_status(chpid, CHP_ONLINE);
		/* Avoid the extra overhead in process_rec_acc. */
		chp_add(chpid);
	}
}

/*
 * Function: s390_vary_chpid
 * Varies the specified chpid online or offline
 */
static int
s390_vary_chpid( __u8 chpid, int on)
{
	char dbf_text[15];
	struct subchannel *sch;
	int irq;

	sprintf(dbf_text, on?"varyon%x":"varyoff%x", chpid);
	CIO_TRACE_EVENT( 2, dbf_text);

	chsc_get_sch_descriptions();
	if (!cio_chsc_desc_avail) {
		printk(KERN_ERR "Could not get chpid status, "
		       "vary on/off not available\n");
		return -EPERM;
	}

	if (!test_bit(chpid, chpids_known)) {
		printk(KERN_ERR "Can't vary unknown chpid %02X\n", chpid);
		return -EINVAL;
	}

	if (test_bit(chpid, chpids) == on) {
		printk(KERN_ERR "chpid %x is "
		       "already %sline\n", chpid, on ? "on" : "off");
		return -EINVAL;
	}

	if (on) {
		set_bit(chpid, chpids_logical);
		set_bit(chpid, chpids);
		set_chp_status(chpid, CHP_ONLINE);
	} else {
		clear_bit(chpid, chpids_logical);
		clear_bit(chpid, chpids);
		set_chp_status(chpid, CHP_LOGICALLY_OFFLINE);
	}

	/*
	 * Redo PathVerification on the devices the chpid connects to
	 */

	for (irq = 0; irq <= highest_subchannel; irq++) {
		int chp;
		/*
		 * We don't need to adjust the lpm, as this will be done in
		 * DevicePathVerification...
		 */
		sch = ioinfo[irq];
		if (sch == NULL || sch->st || !sch->ssd_info.valid)
			continue;

		for (chp = 0; chp < 8; chp++) {
			if (sch->ssd_info.chpid[chp] == chpid) {
				if (on)
					sch->lpm |= (0x80 >> chp);
				else
					sch->lpm &= ~(0x80 >> chp);
				dev_fsm_event(sch->dev.driver_data,
					      DEV_EVENT_VERIFY);
				break;
			}
		}
	}
	return 0;
}

/*
 * Files for the channel path entries.
 */
static ssize_t
chp_status_show(struct device *dev, char *buf)
{
	struct channel_path *chp = container_of(dev, struct channel_path, dev);

	if (!chp)
		return 0;

	switch(chp->state) {
	case CHP_OFFLINE:
		return sprintf(buf, "n/a\n");
	case CHP_LOGICALLY_OFFLINE:
		return sprintf(buf, "logically offline\n");
	case CHP_STANDBY:
		return sprintf(buf, "n/a\n");
	case CHP_ONLINE:
		return sprintf(buf, "online\n");
	default:
		return 0;
	}
}

static ssize_t
chp_status_write(struct device *dev, const char *buf, size_t count)
{
	struct channel_path *cp = container_of(dev, struct channel_path, dev);
	char cmd[10];
	int num_args;
	int error;

	num_args = sscanf(buf, "%5s", cmd);
	if (!num_args)
		return count;

	if (!strnicmp(cmd, "on", 2))
		error = s390_vary_chpid(cp->id, 1);
	else if (!strnicmp(cmd, "off", 3))
		error = s390_vary_chpid(cp->id, 0);
	else
		error = -EINVAL;

	return error < 0 ? error : count;

}

static DEVICE_ATTR(status, 0644, chp_status_show, chp_status_write);


static void
chp_release(struct device *dev)
{
}

/*
 * Entries for chpids on the system bus.
 * This replaces /proc/chpids.
 */
static int
new_channel_path(int chpid, int status)
{
	struct channel_path *chp;
	int ret;

	chp = kmalloc(sizeof(struct channel_path), GFP_KERNEL);
	if (!chp)
		return -ENOMEM;
	memset(chp, 0, sizeof(struct channel_path));

	chps[chpid] = chp;

	/* fill in status, etc. */
	chp->id = chpid;
	chp->state = status;
	chp->dev = (struct device) {
		.parent  = &css_bus_device,
		.release = chp_release,
	};
	snprintf(chp->dev.bus_id, BUS_ID_SIZE, "chp0.%x", chpid);

	/* make it known to the system */
	ret = device_register(&chp->dev);
	if (ret) {
		printk(KERN_WARNING "%s: could not register %02x\n",
		       __func__, chpid);
		return ret;
	}
	ret = device_create_file(&chp->dev, &dev_attr_status);
	if (ret)
		device_unregister(&chp->dev);

	return ret;
}

static int __init
register_channel_paths(void)
{
	int i;
	int ret;

	/* walk through the chpids arrays */
	for (i = 0; i < NR_CHPIDS; i++) {
		/* we are only interested in known chpids */
		if (!test_bit(i, chpids_known))
			continue;
		if (!test_bit(i, chpids))
			/* standby */
			ret = new_channel_path(i, CHP_STANDBY);

		else if (test_bit(i, chpids_logical))
			/* online */
			ret = new_channel_path(i, CHP_ONLINE);
		else
			/* logically offline */
			ret = new_channel_path(i, CHP_LOGICALLY_OFFLINE);

		if (ret)
			return ret;
	}
	return 0;
}

module_init(register_channel_paths);
