/*
 *  drivers/s390/cio/chsc.c
 *   S/390 common I/O routines -- channel subsystem call
 *   $Revision: 1.46 $
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

static inline void
chsc_clear_chpid(struct subchannel *sch, int chp)
{
	clear_bit(sch->schib.pmcw.chpid[chp], chpids);
}

void
chsc_validate_chpids(struct subchannel *sch)
{
	int mask, chp;

	if (sch->lpm == 0)
		return;

	for (chp = 0; chp <= 7; chp++) {
		mask = 0x80 >> chp;
		if (sch->lpm & mask) {
			if (!chsc_chpid_logical(sch, chp))
				/* disable using this path */
				sch->lpm &= ~mask;
		} else {
			/* This chpid is not
			 * available to us */
			chsc_clear_chpid(sch, chp);
			if (test_bit(chp, chpids_known))
				set_chp_status(chp, CHP_STANDBY);
		}
	}
}

/* FIXME: this is _always_ called for every subchannel. shouldn't we
 *	  process more than one at a time?*/
static int
chsc_get_sch_desc_irq(int irq)
{
	int ccode, chpid, j;

	/* FIXME: chsc_area_sei cannot be on the stack since it needs to
	 * be page-aligned. Implement proper locking or dynamic
	 * allocation or prove that this function does not have to be
	 * reentrant! */
	static struct ssd_area chsc_area_ssd 
		__attribute__ ((aligned(PAGE_SIZE)));

	typeof (chsc_area_ssd.response_block)
		*ssd_res = &chsc_area_ssd.response_block;

	chsc_area_ssd = (struct ssd_area) {
		.request_block = {
			.command_code1 = 0x0010,
			.command_code2 = 0x0004,
			.f_sch = irq,
			.l_sch = irq,
		}
	};

	ccode = chsc(&chsc_area_ssd);
	if (ccode > 0) {
		pr_debug("chsc returned with ccode = %d\n", ccode);
		if (ccode == 3)
			return -ENODEV;
		return -EBUSY;
	}

	switch (chsc_area_ssd.response_block.response_code) {
	case 0x0001: /* everything ok */
		break;
	case 0x0002:
		CIO_CRW_EVENT(2, "Invalid command!\n");
	case 0x0003:
		CIO_CRW_EVENT(2, "Error in chsc request block!\n");
		return -EINVAL;
	case 0x0004:
		CIO_CRW_EVENT(2, "Model does not provide ssd\n");
		return -EOPNOTSUPP;
	default:
		CIO_CRW_EVENT(2, "Unknown CHSC response %d\n",
			      chsc_area_ssd.response_block.response_code);
		return -EIO;
	}

	/*
	 * ssd_res->st stores the type of the detected
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
	if (ssd_res->st > 3) { /* uhm, that looks strange... */
		CIO_CRW_EVENT(0, "Strange subchannel type %d"
			      " for sch %x\n", ssd_res->st, irq);
		/*
		 * There may have been a new subchannel type defined in the
		 * time since this code was written; since we don't know which
		 * fields have meaning and what to do with it we just jump out
		 */
		return 0;
	} else {
		const char type[4][8] = {"I/O", "chsc", "message", "ADM"};
		CIO_CRW_EVENT(6, "ssd: sch %x is %s subchannel\n",
			      irq, type[ssd_res->st]);
		if (ioinfo[irq] == NULL)
			/* FIXME: we should do device rec. here... */
			return 0;

		ioinfo[irq]->ssd_info.valid = 1;
		ioinfo[irq]->ssd_info.type = ssd_res->st;
	}

	if (ssd_res->st == 0 || ssd_res->st == 2) {
		for (j = 0; j < 8; j++) {
			if (!((0x80 >> j) & ssd_res->path_mask &
			      ssd_res->fla_valid_mask))
				continue;
			chpid = ssd_res->chpid[j];
			if (chpid
			    && (!test_and_set_bit (chpid, chpids_known))
			    && (test_bit (chpid, chpids_logical)))
				set_bit	 (chpid, chpids);

			ioinfo[irq]->ssd_info.chpid[j] = chpid;
			ioinfo[irq]->ssd_info.fla[j]   = ssd_res->fla[j];
		}
	}
	return 0;
}

static int
chsc_get_sch_descriptions(void)
{
	int irq;
	int err;

	CIO_TRACE_EVENT( 4, "gsdesc");

	/*
	 * get information about chpids and link addresses
	 * by executing the chsc command 'store subchannel description'
	 */
	for (irq = 0; irq <= highest_subchannel; irq++) {
		/*
		 * retrieve information for each sch
		 */
		err = chsc_get_sch_desc_irq(irq);
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
			return err;
		}
	}
	cio_chsc_desc_avail = 1;
	return 0;
}

__initcall(chsc_get_sch_descriptions);


static inline void
s390_subchannel_remove_chpid(struct subchannel *sch, __u8 chpid)
{
	int j;

	for (j = 0; j < 8; j++)
		if (sch->schib.pmcw.chpid[j] == chpid)
			break;
	if (j >= 8)
		return;

	spin_lock(&sch->lock);

	chsc_validate_chpids(sch);

	/* just to be sure... */
	sch->lpm &= ~(0x80>>j);

	/* trigger path verification. */
	dev_fsm_event(sch->dev.driver_data, DEV_EVENT_VERIFY);

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

	/*
	 * TODO: the chpid may be not the chpid with the link incident,
	 * but the chpid the report came in through. How to handle???
	 */
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
			 struct subchannel *sch)
{
	int found;
	int chp;
	int ccode;
	
	/* Update our ssd_info */
	if (chsc_get_sch_desc_irq(sch->irq))
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

	if (!cio_chsc_desc_avail)
		chsc_get_sch_descriptions();

	if (!cio_chsc_desc_avail) {
		/*
		 * Something went wrong...
		 */
		CIO_CRW_EVENT(0, "Error: Could not retrieve "
			      "subchannel descriptions, will not process css"
			      "machine check...\n");
		return;
	}

	if (!test_bit(chpid, chpids_logical))
		return; /* no need to do the rest */

	for (irq = 0; irq <= __MAX_SUBCHANNELS; irq++) {
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
				return;
			continue;
		}
	
		spin_lock_irq(&sch->lock);

		chp_mask = s390_process_res_acc_sch(chpid, fla, fla_mask, sch);
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
}

static void
do_process_crw(void *ignore)
{
	int ccode;

	/*
	 * build the chsc request block for store event information
	 * and do the call
	 */

	/* FIXME: chsc_area_sei cannot be on the stack since it needs to
	 * be page-aligned. Implement proper locking or dynamic
	 * allocation or prove that this function does not have to be
	 * reentrant! */
	static struct sei_area chsc_area_sei
		__attribute__ ((aligned(PAGE_SIZE))) = {
			.request_block = {
				.command_code1 = 0x0010,
				.command_code2 = 0x000e
			}
		};

	typeof (chsc_area_sei.response_block)
		*sei_res = &chsc_area_sei.response_block;


	CIO_TRACE_EVENT( 2, "prcss");

	ccode = chsc(&chsc_area_sei);
	if (ccode > 0)
		return;

	switch (chsc_area_sei.response_block.response_code) {
		/* for debug purposes, check for problems */
	case 0x0001:
		break; /* everything ok */
	case 0x0002:
		CIO_CRW_EVENT(2, "chsc_process_crw:invalid command!\n");
	case 0x0003:
		CIO_CRW_EVENT(2, "chsc_process_crw: error in chsc "
			      "request block!\n");
		return;
	case 0x0005:
		CIO_CRW_EVENT(2, "chsc_process_crw: no event information "
			      "stored\n");
		return;
	default:
		CIO_CRW_EVENT(2, "chsc_process_crw: chsc response %d\n",
			      chsc_area_sei.response_block.response_code);
		return;
	}

	CIO_CRW_EVENT(4, "chsc_process_crw: event information successfully "
		      "stored\n");

	if (sei_res->rs != 4) {
		CIO_CRW_EVENT(2, "chsc_process_crw: "
			      "reporting source (%04X) isn't a chpid!"
			      "Aborting processing of machine check...\n",
			      sei_res->rsid);
		return;
	}

	/* which kind of information was stored? */
	switch (sei_res->cc) {
	case 1: /* link incident*/
		CIO_CRW_EVENT(4, "chsc_process_crw: "
			      "channel subsystem reports link incident,"
			      " source is chpid %x\n", sei_res->rsid);

		s390_set_chpid_offline(sei_res->rsid);
		break;

	case 2: /* i/o resource accessibiliy */
		CIO_CRW_EVENT(4, "chsc_process_crw: "
			      "channel subsystem reports some I/O "
			      "devices may have become accessible\n");
		pr_debug( KERN_DEBUG "Data received after sei: \n");
		pr_debug( KERN_DEBUG "Validity flags: %x\n", sei_res->vf);

		/* allocate a new channel path structure, if needed */
		if (chps[sei_res->rsid] == NULL)
			new_channel_path(sei_res->rsid, CHP_ONLINE);
		else
			set_chp_status(sei_res->rsid, CHP_ONLINE);

		if ((sei_res->vf & 0x80) == 0) {
			pr_debug( KERN_DEBUG "chpid: %x\n", sei_res->rsid);
			s390_process_res_acc(sei_res->rsid, 0, 0);
		} else if ((sei_res->vf & 0xc0) == 0x80) {
			pr_debug( KERN_DEBUG "chpid: %x link addr: %x\n",
			       sei_res->rsid, sei_res->fla);
			s390_process_res_acc(sei_res->rsid, sei_res->fla,
					     0xff00);
		} else if ((sei_res->vf & 0xc0) == 0xc0) {
			pr_debug( KERN_DEBUG "chpid: %x full link addr: %x\n",
			       sei_res->rsid, sei_res->fla);
			s390_process_res_acc(sei_res->rsid, sei_res->fla,
					     0xffff);
		}
		pr_debug( KERN_DEBUG "\n");

		break;

	default: /* other stuff */
		CIO_CRW_EVENT(4, "chsc_process_crw: event %d\n", sei_res->cc);
		break;
	}
}

void
chsc_process_crw(void)
{
	static DECLARE_WORK(work, do_process_crw, 0);

	schedule_work(&work);
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

	if (chpid <=0 || chpid >= NR_CHPIDS)
		return -EINVAL;

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
	struct sys_device *sdev = container_of(dev, struct sys_device, dev);
	struct channel_path *chp = container_of(sdev, struct channel_path, sdev);

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
	struct sys_device *sdev = container_of(dev, struct sys_device, dev);
	struct channel_path *cp = container_of(sdev, struct channel_path, sdev);
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

	chps[chpid] = chp;

	/* fill in status, etc. */
	chp->id = chpid;
	chp->state = status;

	snprintf(chp->sdev.dev.name, DEVICE_NAME_SIZE,
		 "channel path %x", chpid);
	chp->sdev.name = "channel_path";

	chp->sdev.id = chpid;

	/* make it known to the system */
	ret = sys_device_register(&chp->sdev);
	if (ret) {
		printk(KERN_WARNING "%s: could not register %02x\n",
		       __func__, chpid);
		return ret;
	}
	ret = device_create_file(&chp->sdev.dev, &dev_attr_status);
	if (ret)
		sys_device_unregister(&chp->sdev);

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
