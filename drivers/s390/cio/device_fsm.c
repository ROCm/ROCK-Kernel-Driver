/*
 * drivers/s390/cio/device_fsm.c
 * finite state machine for device handling
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Cornelia Huck(cohuck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>

#include <asm/ccwdev.h>
#include <asm/qdio.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "qdio.h"

/*
 * Timeout function. It just triggers a DEV_EVENT_TIMEOUT.
 */
static void
ccw_device_timeout(unsigned long data)
{
	struct ccw_device *cdev;

	cdev = (struct ccw_device *) data;
	spin_lock_irq(cdev->ccwlock);
	dev_fsm_event(cdev, DEV_EVENT_TIMEOUT);
	spin_unlock_irq(cdev->ccwlock);
}

/*
 * Set timeout
 */
void
ccw_device_set_timeout(struct ccw_device *cdev, int expires)
{
	if (expires == 0) {
		if (timer_pending(&cdev->private->timer))
			del_timer(&cdev->private->timer);
		return;
	}
	if (timer_pending(&cdev->private->timer)) {
		if (mod_timer(&cdev->private->timer, jiffies + expires))
			return;
	}
	cdev->private->timer.function = ccw_device_timeout;
	cdev->private->timer.data = (unsigned long) cdev;
	cdev->private->timer.expires = jiffies + expires;
	add_timer(&cdev->private->timer);
}

/*
 * Cancel running i/o. This is called repeatedly since halt/clear are
 * asynchronous operations. We do one try with cio_cancel, two tries
 * with cio_halt, 255 tries with cio_clear. If everythings fails panic.
 */
static int
ccw_device_cancel_halt_clear(struct ccw_device *cdev)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	if (sch->schib.scsw.actl == 0)
		/* Not operational or no activity -> done. */
		return 0;
	/* Stage 1: cancel io. */
	if (!(sch->schib.scsw.actl & SCSW_ACTL_HALT_PEND) &&
	    !(sch->schib.scsw.actl & SCSW_ACTL_CLEAR_PEND)) {
		if (cio_cancel (sch) == 0)
			return 0;
		/* cancel io unsuccessful. From now on it is asynchronous. */
		cdev->private->iretry = 3;	/* 3 halt retries. */
	}
	if (!(sch->schib.scsw.actl & SCSW_ACTL_CLEAR_PEND)) {
		/* Stage 2: halt io. */
		while (cdev->private->iretry-- > 0)
			if (cio_halt (sch) == 0)
				return -EBUSY;
		/* halt io unsuccessful. */
		cdev->private->iretry = 255;	/* 255 clear retries. */
	}
	/* Stage 3: clear io. */
	while (cdev->private->iretry-- > 0)
		if (cio_clear (sch) == 0)
			return -EBUSY;
	panic("Can't stop i/o on subchannel.\n");
}

/*
 * Stop device recognition.
 */
static void
ccw_device_recog_done(struct ccw_device *cdev, int state)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);

	ccw_device_set_timeout(cdev, 0);
	cio_disable_subchannel(sch);
	cdev->private->state = state;

	switch (state) {
	case DEV_STATE_NOT_OPER:
		CIO_DEBUG(KERN_WARNING, 2,
			  "SenseID : unknown device %04X on subchannel %04X\n",
			  sch->schib.pmcw.dev, sch->irq);
		break;
	case DEV_STATE_OFFLINE:
		/* fill out sense information */
		cdev->id = (struct ccw_device_id) {
			.cu_type   = cdev->private->senseid.cu_type,
			.cu_model  = cdev->private->senseid.cu_model,
			.dev_type  = cdev->private->senseid.dev_type,
			.dev_model = cdev->private->senseid.dev_model,
		};
		/* Issue device info message. */
		CIO_DEBUG(KERN_INFO, 2, "SenseID : device %04X reports: "
			  "CU  Type/Mod = %04X/%02X, Dev Type/Mod = "
			  "%04X/%02X\n", sch->schib.pmcw.dev,
			  cdev->id.cu_type, cdev->id.cu_model,
			  cdev->id.dev_type, cdev->id.dev_model);
		break;
	case DEV_STATE_BOXED:
		CIO_DEBUG(KERN_WARNING, 2,
			  "SenseID : boxed device %04X on subchannel %04X\n",
			  sch->schib.pmcw.dev, sch->irq);
		break;
	}
	io_subchannel_recog_done(cdev);
	if (state != DEV_STATE_NOT_OPER)
		wake_up(&cdev->private->wait_q);
}

/*
 * Function called from device_id.c after sense id has completed.
 */
void
ccw_device_sense_id_done(struct ccw_device *cdev, int err)
{
	switch (err) {
	case 0:
		ccw_device_recog_done(cdev, DEV_STATE_OFFLINE);
		break;
	case -ETIME:		/* Sense id stopped by timeout. */
		ccw_device_recog_done(cdev, DEV_STATE_BOXED);
		break;
	default:
		ccw_device_recog_done(cdev, DEV_STATE_NOT_OPER);
		break;
	}
}

/*
 * Finished with online/offline processing.
 */
static void
ccw_device_done(struct ccw_device *cdev, int state)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);

	if (state != DEV_STATE_ONLINE)
		cio_disable_subchannel(sch);

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	cdev->private->state = state;


	if (state == DEV_STATE_BOXED) {
		CIO_DEBUG(KERN_WARNING, 2,
			  "Boxed device %04X on subchannel %04X\n",
			  sch->schib.pmcw.dev, sch->irq);
		INIT_WORK(&cdev->private->kick_work,
			  ccw_device_add_stlck, (void *) cdev);
		queue_work(ccw_device_work, &cdev->private->kick_work);
	}

	wake_up(&cdev->private->wait_q);

	if (css_init_done && state != DEV_STATE_ONLINE)
		put_device (&cdev->dev);
}

/*
 * Function called from device_pgid.c after sense path ground has completed.
 */
void
ccw_device_sense_pgid_done(struct ccw_device *cdev, int err)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	switch (err) {
	case 0:
		/* Start Path Group verification. */
		sch->vpm = 0;	/* Start with no path groups set. */
		cdev->private->state = DEV_STATE_VERIFY;
		ccw_device_verify_start(cdev);
		break;
	case -ETIME:		/* Sense path group id stopped by timeout. */
	case -EUSERS:		/* device is reserved for someone else. */
		ccw_device_done(cdev, DEV_STATE_BOXED);
		break;
	case -EOPNOTSUPP: /* path grouping not supported, just set online. */
		cdev->private->options.pgroup = 0;
		ccw_device_done(cdev, DEV_STATE_ONLINE);
		break;
	default:
		ccw_device_done(cdev, DEV_STATE_NOT_OPER);
		break;
	}
}

/*
 * Start device recognition.
 */
int
ccw_device_recognition(struct ccw_device *cdev)
{
	struct subchannel *sch;

	if (cdev->private->state != DEV_STATE_NOT_OPER)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);
	if (cio_enable_subchannel(sch, sch->schib.pmcw.isc) != 0)
		/* Couldn't enable the subchannel for i/o. Sick device. */
		return -ENODEV;

	/* After 60s the device recognition is considered to have failed. */
	ccw_device_set_timeout(cdev, 60*HZ);

	/*
	 * We used to start here with a sense pgid to find out whether a device
	 * is locked by someone else. Unfortunately, the sense pgid command
	 * code has other meanings on devices predating the path grouping
	 * algorithm, so we start with sense id and box the device after an
	 * timeout (or if sense pgid during path verification detects the device
	 * is locked, as may happen on newer devices).
	 */
	cdev->private->state = DEV_STATE_SENSE_ID;
	ccw_device_sense_id_start(cdev);
	return 0;
}

/*
 * Handle timeout in device recognition.
 */
static void
ccw_device_recog_timeout(struct ccw_device *cdev, enum dev_event dev_event)
{
	if (ccw_device_cancel_halt_clear(cdev) == 0)
		ccw_device_recog_done(cdev, DEV_STATE_BOXED);
	else
		ccw_device_set_timeout(cdev, 3*HZ);
}


void
ccw_device_verify_done(struct ccw_device *cdev, int err)
{
	cdev->private->flags.doverify = 0;
	switch (err) {
	case 0:
		ccw_device_done(cdev, DEV_STATE_ONLINE);
		break;
	case -ETIME:
		ccw_device_done(cdev, DEV_STATE_BOXED);
		break;
	default:
		ccw_device_done(cdev, DEV_STATE_NOT_OPER);
		break;
	}
}

/*
 * Get device online.
 */
int
ccw_device_online(struct ccw_device *cdev)
{
	struct subchannel *sch;

	if (cdev->private->state != DEV_STATE_OFFLINE)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);
	if (css_init_done && !get_device(&cdev->dev))
		return -ENODEV;
	if (cio_enable_subchannel(sch, sch->schib.pmcw.isc) != 0) {
		/* Couldn't enable the subchannel for i/o. Sick device. */
		dev_fsm_event(cdev, DEV_EVENT_NOTOPER);
		return -ENODEV;
	}
	/* Do we want to do path grouping? */
	if (!cdev->private->options.pgroup) {
		/* No, set state online immediately. */
		ccw_device_done(cdev, DEV_STATE_ONLINE);
		return 0;
	}
	/* Do a SensePGID first. */
	cdev->private->state = DEV_STATE_SENSE_PGID;
	ccw_device_sense_pgid_start(cdev);
	return 0;
}

void
ccw_device_disband_done(struct ccw_device *cdev, int err)
{
	switch (err) {
	case 0:
		ccw_device_done(cdev, DEV_STATE_OFFLINE);
		break;
	case -ETIME:
		ccw_device_done(cdev, DEV_STATE_BOXED);
		break;
	default:
		ccw_device_done(cdev, DEV_STATE_NOT_OPER);
		break;
	}
}

/*
 * Shutdown device.
 */
int
ccw_device_offline(struct ccw_device *cdev)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	if (cdev->private->state != DEV_STATE_ONLINE) {
		if (sch->schib.scsw.actl != 0)
			return -EBUSY;
		return -EINVAL;
	}
	if (sch->schib.scsw.actl != 0)
		return -EBUSY;
	/* Are we doing path grouping? */
	if (!cdev->private->options.pgroup) {
		/* No, set state offline immediately. */
		ccw_device_done(cdev, DEV_STATE_OFFLINE);
		return 0;
	}
	/* Start Set Path Group commands. */
	cdev->private->state = DEV_STATE_DISBAND_PGID;
	ccw_device_disband_start(cdev);
	return 0;
}

/*
 * Handle timeout in device online/offline process.
 */
static void
ccw_device_onoff_timeout(struct ccw_device *cdev, enum dev_event dev_event)
{
	if (ccw_device_cancel_halt_clear(cdev) == 0)
		ccw_device_done(cdev, DEV_STATE_BOXED);
	else
		ccw_device_set_timeout(cdev, 3*HZ);
}

/*
 * Handle not oper event in device recognition.
 */
static void
ccw_device_recog_notoper(struct ccw_device *cdev, enum dev_event dev_event)
{
	ccw_device_recog_done(cdev, DEV_STATE_NOT_OPER);
}

/*
 * Handle not operational event while offline.
 */
static void
ccw_device_offline_notoper(struct ccw_device *cdev, enum dev_event dev_event)
{
	cdev->private->state = DEV_STATE_NOT_OPER;
	INIT_WORK(&cdev->private->kick_work,
		  ccw_device_unregister, (void *) &cdev->dev);
	queue_work(ccw_device_work, &cdev->private->kick_work);
	wake_up(&cdev->private->wait_q);
}

/*
 * Handle not operational event while online.
 */
static void
ccw_device_online_notoper(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	cdev->private->state = DEV_STATE_NOT_OPER;
	cio_disable_subchannel(sch);
	if (sch->schib.scsw.actl != 0) {
		// FIXME: not-oper indication to device driver ?
		ccw_device_call_handler(cdev);
	}
	INIT_WORK(&cdev->private->kick_work,
		  ccw_device_unregister, (void *) &cdev->dev);
	queue_work(ccw_device_work, &cdev->private->kick_work);
	wake_up(&cdev->private->wait_q);
}

/*
 * Handle path verification event.
 */
static void
ccw_device_online_verify(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;

	if (!cdev->private->options.pgroup)
		return;
	if (cdev->private->state == DEV_STATE_W4SENSE) {
		cdev->private->flags.doverify = 1;
		return;
	}
	sch = to_subchannel(cdev->dev.parent);
	if (sch->schib.scsw.actl != 0 ||
	    (cdev->private->irb.scsw.stctl & SCSW_STCTL_STATUS_PEND)) {
		/*
		 * No final status yet or final status not yet delivered
		 * to the device driver. Can't do path verfication now,
		 * delay until final status was delivered.
		 */
		cdev->private->flags.doverify = 1;
		return;
	}
	/* Device is idle, we can do the path verification. */
	cdev->private->state = DEV_STATE_VERIFY;
	ccw_device_verify_start(cdev);
}

/*
 * Got an interrupt for a normal io (state online).
 */
static void
ccw_device_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct irb *irb;

	irb = (struct irb *) __LC_IRB;
	/* Check for unsolicited interrupt. */
	if (irb->scsw.stctl ==
	    		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (cdev->handler)
			cdev->handler (cdev, 0, irb);
		return;
	}
	/* Accumulate status and find out if a basic sense is needed. */
	ccw_device_accumulate_irb(cdev, irb);
	if (cdev->private->flags.dosense) {
		if (ccw_device_do_sense(cdev, irb) == 0) {
			cdev->private->state = DEV_STATE_W4SENSE;
		}
		return;
	}
	/* Call the handler. */
	if (ccw_device_call_handler(cdev) && cdev->private->flags.doverify)
		/* Start delayed path verification. */
		ccw_device_online_verify(cdev, 0);
}

/*
 * Got an timeout in online state.
 */
static void
ccw_device_online_timeout(struct ccw_device *cdev, enum dev_event dev_event)
{
	ccw_device_set_timeout(cdev, 0);
	if (ccw_device_cancel_halt_clear(cdev) != 0) {
		ccw_device_set_timeout(cdev, 3*HZ);
		cdev->private->state = DEV_STATE_TIMEOUT_KILL;
		return;
	}
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      ERR_PTR(-ETIMEDOUT));
}

/*
 * Got an interrupt for a basic sense.
 */
void
ccw_device_w4sense(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct irb *irb;

	irb = (struct irb *) __LC_IRB;
	/* Check for unsolicited interrupt. */
	if (irb->scsw.stctl ==
	    		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (cdev->handler)
			cdev->handler (cdev, 0, irb);
		return;
	}
	/* Add basic sense info to irb. */
	ccw_device_accumulate_basic_sense(cdev, irb);
	if (cdev->private->flags.dosense) {
		/* Another basic sense is needed. */
		ccw_device_do_sense(cdev, irb);
		return;
	}
	cdev->private->state = DEV_STATE_ONLINE;
	/* Call the handler. */
	if (ccw_device_call_handler(cdev) && cdev->private->flags.doverify)
		/* Start delayed path verification. */
		ccw_device_online_verify(cdev, 0);
}

static void
ccw_device_clear_verify(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct irb *irb;

	irb = (struct irb *) __LC_IRB;
	/* Check for unsolicited interrupt. */
	if (irb->scsw.stctl ==
	    		(SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (cdev->handler)
			cdev->handler (cdev, 0, irb);
		return;
	}
	/* Accumulate status. We don't do basic sense. */
	ccw_device_accumulate_irb(cdev, irb);
	/* Try to start delayed device verification. */
	ccw_device_online_verify(cdev, 0);
	/* Note: Don't call handler for cio initiated clear! */
}

static void
ccw_device_killing_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	/* OK, i/o is dead now. Call interrupt handler. */
	cdev->private->state = DEV_STATE_ONLINE;
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      ERR_PTR(-ETIMEDOUT));
}

static void
ccw_device_killing_timeout(struct ccw_device *cdev, enum dev_event dev_event)
{
	if (ccw_device_cancel_halt_clear(cdev) != 0) {
		ccw_device_set_timeout(cdev, 3*HZ);
		return;
	}
	//FIXME: Can we get here?
	cdev->private->state = DEV_STATE_ONLINE;
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      ERR_PTR(-ETIMEDOUT));
}

static void
ccw_device_stlck_done(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct irb *irb;

	switch (dev_event) {
	case DEV_EVENT_INTERRUPT:
		irb = (struct irb *) __LC_IRB;
		/* Check for unsolicited interrupt. */
		if (irb->scsw.stctl ==
		    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS))
			goto out_wakeup;

		ccw_device_accumulate_irb(cdev, irb);
		/* We don't care about basic sense etc. */
		break;
	default: /* timeout */
		break;
	}
out_wakeup:
	wake_up(&cdev->private->wait_q);
}

/*
 * No operation action. This is used e.g. to ignore a timeout event in
 * state offline.
 */
static void
ccw_device_nop(struct ccw_device *cdev, enum dev_event dev_event)
{
}

/*
 * Bug operation action. 
 */
static void
ccw_device_bug(struct ccw_device *cdev, enum dev_event dev_event)
{
	printk(KERN_EMERG "dev_jumptable[%i][%i] == NULL\n",
	       cdev->private->state, dev_event);
	BUG();
}

/*
 * device statemachine
 */
fsm_func_t *dev_jumptable[NR_DEV_STATES][NR_DEV_EVENTS] = {
	[DEV_STATE_NOT_OPER] {
		[DEV_EVENT_NOTOPER]	ccw_device_nop,
		[DEV_EVENT_INTERRUPT]	ccw_device_bug,
		[DEV_EVENT_TIMEOUT]	ccw_device_nop,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_SENSE_PGID] {
		[DEV_EVENT_NOTOPER]	ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_sense_pgid_irq,
		[DEV_EVENT_TIMEOUT]	ccw_device_onoff_timeout,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_SENSE_ID] {
		[DEV_EVENT_NOTOPER]	ccw_device_recog_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_sense_id_irq,
		[DEV_EVENT_TIMEOUT]	ccw_device_recog_timeout,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_OFFLINE] {
		[DEV_EVENT_NOTOPER]	ccw_device_offline_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_bug,
		[DEV_EVENT_TIMEOUT]	ccw_device_nop,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_VERIFY] {
		[DEV_EVENT_NOTOPER]	ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_verify_irq,
		[DEV_EVENT_TIMEOUT]	ccw_device_onoff_timeout,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_ONLINE] {
		[DEV_EVENT_NOTOPER]	ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_irq,
		[DEV_EVENT_TIMEOUT]	ccw_device_online_timeout,
		[DEV_EVENT_VERIFY]	ccw_device_online_verify,
	},
	[DEV_STATE_W4SENSE] {
		[DEV_EVENT_NOTOPER]	ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_w4sense,
		[DEV_EVENT_TIMEOUT]	ccw_device_nop,
		[DEV_EVENT_VERIFY]	ccw_device_online_verify,
	},
	[DEV_STATE_DISBAND_PGID] {
		[DEV_EVENT_NOTOPER]	ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_disband_irq,
		[DEV_EVENT_TIMEOUT]	ccw_device_onoff_timeout,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_BOXED] {
		[DEV_EVENT_NOTOPER]	ccw_device_offline_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_stlck_done,
		[DEV_EVENT_TIMEOUT]	ccw_device_stlck_done,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	/* states to wait for i/o completion before doing something */
	[DEV_STATE_CLEAR_VERIFY] {
		[DEV_EVENT_NOTOPER]     ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]   ccw_device_clear_verify,
		[DEV_EVENT_TIMEOUT]	ccw_device_nop,
		[DEV_EVENT_VERIFY]	ccw_device_nop,
	},
	[DEV_STATE_TIMEOUT_KILL] {
		[DEV_EVENT_NOTOPER]	ccw_device_online_notoper,
		[DEV_EVENT_INTERRUPT]	ccw_device_killing_irq,
		[DEV_EVENT_TIMEOUT]	ccw_device_killing_timeout,
		[DEV_EVENT_VERIFY]	ccw_device_nop, //FIXME
	},
};

/*
 * io_subchannel_irq is called for "real" interrupts or for status
 * pending conditions on msch.
 */
void
io_subchannel_irq (struct device *pdev)
{
	char dbf_txt[15];
	struct ccw_device *cdev;

	cdev = to_subchannel(pdev)->dev.driver_data;

	sprintf (dbf_txt, "IRQ%04x", cdev->private->irq);
	CIO_TRACE_EVENT (3, dbf_txt);

	dev_fsm_event(cdev, DEV_EVENT_INTERRUPT);
}

EXPORT_SYMBOL_GPL(ccw_device_set_timeout);
