/*
 *  drivers/s390/cio/device_ops.c
 *
 *   $Revision: 1.34 $
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>

#include <asm/ccwdev.h>
#include <asm/idals.h>
#include <asm/qdio.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "qdio.h"

int
ccw_device_set_options(struct ccw_device *cdev, unsigned long flags)
{
       /*
	* The flag usage is mutal exclusive ...
	*/
	if ((flags & CCWDEV_EARLY_NOTIFICATION) &&
	    (flags & CCWDEV_REPORT_ALL))
		return -EINVAL;
	cdev->private->options.fast = (flags & CCWDEV_EARLY_NOTIFICATION) != 0;
	cdev->private->options.repall = (flags & CCWDEV_REPORT_ALL) != 0;
	return 0;
}

int
ccw_device_clear(struct ccw_device *cdev, unsigned long intparm)
{
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE &&
	    cdev->private->state != DEV_STATE_W4SENSE &&
	    cdev->private->state != DEV_STATE_ONLINE_VERIFY &&
	    cdev->private->state != DEV_STATE_W4SENSE_VERIFY)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	ret = cio_clear(sch);
	if (ret == 0)
		cdev->private->intparm = intparm;
	return ret;
}

int
ccw_device_start(struct ccw_device *cdev, struct ccw1 *cpa,
		 unsigned long intparm, __u8 lpm, unsigned long flags)
{
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE ||
	    sch->schib.scsw.actl != 0)
		return -EBUSY;
	ret = cio_set_options (sch, flags);
	if (ret)
		return ret;
	/* 0xe4e2c5d9 == ebcdic "USER" */
	ret = cio_start (sch, cpa, 0xe4e2c5d9, lpm);
	if (ret == 0)
		cdev->private->intparm = intparm;
	return ret;
}

int
ccw_device_start_timeout(struct ccw_device *cdev, struct ccw1 *cpa,
			 unsigned long intparm, __u8 lpm, unsigned long flags,
			 int expires)
{
	int ret;

	if (!cdev)
		return -ENODEV;
	ccw_device_set_timeout(cdev, expires);
	ret = ccw_device_start(cdev, cpa, intparm, lpm, flags);
	if (ret != 0)
		ccw_device_set_timeout(cdev, 0);
	return ret;
}

int
ccw_device_halt(struct ccw_device *cdev, unsigned long intparm)
{
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE &&
	    cdev->private->state != DEV_STATE_W4SENSE &&
	    cdev->private->state != DEV_STATE_ONLINE_VERIFY &&
	    cdev->private->state != DEV_STATE_W4SENSE_VERIFY)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	ret = cio_halt(sch);
	if (ret == 0)
		cdev->private->intparm = intparm;
	return ret;
}

int
ccw_device_resume(struct ccw_device *cdev)
{
	struct subchannel *sch;

	if (!cdev)
		return -ENODEV;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE ||
	    !(sch->schib.scsw.actl & SCSW_ACTL_SUSPENDED))
		return -EINVAL;
	return cio_resume(sch);
}

/*
 * Pass interrupt to device driver.
 */
void
ccw_device_call_handler(struct ccw_device *cdev)
{
	struct subchannel *sch;
	unsigned int stctl;

	sch = to_subchannel(cdev->dev.parent);

	/*
	 * we allow for the device action handler if .
	 *  - we received ending status
	 *  - the action handler requested to see all interrupts
	 *  - we received an intermediate status
	 *  - fast notification was requested (primary status)
	 *  - unsolicited interrupts
	 */
	stctl = cdev->private->irb.scsw.stctl;
	if (sch->schib.scsw.actl != 0 &&
	    !cdev->private->options.repall &&
	    !(stctl & SCSW_STCTL_INTER_STATUS) &&
	    !(cdev->private->options.fast &&
	      (stctl & SCSW_STCTL_PRIM_STATUS)))
		return;

	/*
	 * Now we are ready to call the device driver interrupt handler.
	 */
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      &cdev->private->irb);

	/*
	 * Clear the old and now useless interrupt response block.
	 */
	memset(&cdev->private->irb, 0, sizeof(struct irb));
}

/*
 * Search for CIW command in extended sense data.
 */
struct ciw *
ccw_device_get_ciw(struct ccw_device *cdev, __u32 ct)
{
	int ciw_cnt;

	for (ciw_cnt = 0; ciw_cnt < MAX_CIWS; ciw_cnt++)
		if (cdev->private->senseid.ciw[ciw_cnt].ct == ct)
			return cdev->private->senseid.ciw + ciw_cnt;
	return NULL;
}

__u8
ccw_device_get_path_mask(struct ccw_device *cdev)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return 0;
	else
		return sch->vpm;
}

static void
ccw_device_wake_up(struct ccw_device *cdev, unsigned long ip, struct irb *irb)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	if (!IS_ERR(irb))
		memcpy(&sch->schib.scsw, &irb->scsw, sizeof(struct scsw));
	wake_up(&cdev->private->wait_q);
}

/*
 * This routine returns the characteristics for the device
 *  specified. Some old devices might not provide the necessary
 *  command code information during SenseID processing. In this
 *  case the function returns -EINVAL. Otherwise the function
 *  allocates a decice specific data buffer and provides the
 *  device characteristics together with the buffer size. Its
 *  the callers responability to release the kernel memory if
 *  not longer needed. In case of persistent I/O problems -EBUSY
 *  is returned.
 */
int
read_dev_chars (struct ccw_device *cdev, void **buffer, int length)
{
	void (*handler)(struct ccw_device *, unsigned long, struct irb *);
	char dbf_txt[15];
	unsigned long flags;
	struct subchannel *sch;
	int retry;
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EINVAL;
	if (!buffer || !length)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);

	sprintf (dbf_txt, "rddevch%x", sch->irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	cdev->private->iccws[0].cmd_code = CCW_CMD_RDC;
	cdev->private->iccws[0].count = length;
	cdev->private->iccws[0].flags = CCW_FLAG_SLI;
	ret = set_normalized_cda (cdev->private->iccws, (*buffer));
	if (ret != 0)
		return ret;

	spin_lock_irqsave(&sch->lock, flags);
	/* Save interrupt handler. */
	handler = cdev->handler;
	/* Temporarily install own handler. */
	cdev->handler = ccw_device_wake_up;
	for (retry = 5; retry > 0; retry--) {
		/* 0x00524443 == ebcdic "RDC" */
		ret = cio_start (sch, cdev->private->iccws, 0x00524443, 0);
		if (ret == -ENODEV)
			break;
		if (ret == 0) {
			/* Wait for end of request. */
			spin_unlock_irqrestore(&sch->lock, flags);
			wait_event(cdev->private->wait_q,
				   sch->schib.scsw.actl == 0);
			spin_lock_irqsave(&sch->lock, flags);
			/* Check at least for channel end / device end */
			if ((sch->schib.scsw.dstat !=
			     (DEV_STAT_CHN_END|DEV_STAT_DEV_END)) ||
			    (sch->schib.scsw.cstat != 0)) {
				ret = -EIO;
				continue;
			}
			break;
		}
	}
	/* Restore interrupt handler. */
	cdev->handler = handler;
	spin_unlock_irqrestore(&sch->lock, flags);

	clear_normalized_cda (cdev->private->iccws);

	return ret;
}

/*
 *  Read Configuration data
 */
int
read_conf_data (struct ccw_device *cdev, void **buffer, int *length)
{
	void (*handler)(struct ccw_device *, unsigned long, struct irb *);
	char dbf_txt[15];
	struct subchannel *sch;
	struct ciw *ciw;
	unsigned long flags;
	char *rcd_buf;
	int retry;
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EINVAL;
	if (cdev->private->flags.esid == 0)
		return -EOPNOTSUPP;
	if (!buffer || !length)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);

	sprintf (dbf_txt, "rdconf%x", sch->irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * scan for RCD command in extended SenseID data
	 */
	ciw = ccw_device_get_ciw(cdev, CIW_TYPE_RCD);
	if (!ciw || ciw->cmd == 0)
		return -EOPNOTSUPP;

	rcd_buf = kmalloc(ciw->count, GFP_KERNEL | GFP_DMA);
 	if (!rcd_buf)
		return -ENOMEM;
 	memset (rcd_buf, 0, ciw->count);
	cdev->private->iccws[0].cmd_code = ciw->cmd;
	cdev->private->iccws[0].cda = (__u32) __pa (rcd_buf);
	cdev->private->iccws[0].count = ciw->count;
	cdev->private->iccws[0].flags = CCW_FLAG_SLI;

	spin_lock_irqsave(&sch->lock, flags);
	/* Save interrupt handler. */
	handler = cdev->handler;
	/* Temporarily install own handler. */
	cdev->handler = ccw_device_wake_up;
	for (ret = 0, retry = 5; retry > 0; retry--) {
		/* 0x00524344 == ebcdic "RCD" */
		ret = cio_start (sch, cdev->private->iccws, 0x00524344, 0);
		if (ret == -ENODEV)
			break;
		if (ret)
			continue;
		/* Wait for end of request. */
		spin_unlock_irqrestore(&sch->lock, flags);
		wait_event(cdev->private->wait_q, sch->schib.scsw.actl == 0);
		spin_lock_irqsave(&sch->lock, flags);
		/* Check at least for channel end / device end */
		if ((sch->schib.scsw.dstat != 
		     (DEV_STAT_CHN_END|DEV_STAT_DEV_END)) ||
		    (sch->schib.scsw.cstat != 0)) {
			ret = -EIO;
			continue;
		}
		break;
	}
	/* Restore interrupt handler. */
	cdev->handler = handler;
	spin_unlock_irqrestore(&sch->lock, flags);

 	/*
 	 * on success we update the user input parms
 	 */
 	if (ret) {
 		kfree (rcd_buf);
 		*buffer = NULL;
 		*length = 0;
 	} else {
		*length = ciw->count;
		*buffer = rcd_buf;
	}

	return ret;
}

// FIXME: these have to go:

int
_ccw_device_get_subchannel_number(struct ccw_device *cdev)
{
	return cdev->private->irq;
}

int
_ccw_device_get_device_number(struct ccw_device *cdev)
{
	return cdev->private->devno;
}


MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccw_device_set_options);
EXPORT_SYMBOL(ccw_device_clear);
EXPORT_SYMBOL(ccw_device_halt);
EXPORT_SYMBOL(ccw_device_resume);
EXPORT_SYMBOL(ccw_device_start_timeout);
EXPORT_SYMBOL(ccw_device_start);
EXPORT_SYMBOL(ccw_device_get_ciw);
EXPORT_SYMBOL(ccw_device_get_path_mask);
EXPORT_SYMBOL(read_conf_data);
EXPORT_SYMBOL(read_dev_chars);
EXPORT_SYMBOL(_ccw_device_get_subchannel_number);
EXPORT_SYMBOL(_ccw_device_get_device_number);
