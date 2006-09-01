/*
 * linux/drivers/input/keyboard/xenkbd.c -- Xen para-virtual input device
 *
 * Copyright (C) 2005
 *
 *      Anthony Liguori <aliguori@us.ibm.com>
 *
 *  Based on linux/drivers/input/mouse/sermouse.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/input.h>
#include <asm/hypervisor.h>
#include <xen/evtchn.h>
#include <xen/xenbus.h>
#include <linux/xenkbd.h>

struct xenkbd_device
{
	struct input_dev *dev;
	struct xenkbd_info *info;
	unsigned evtchn;
};

static irqreturn_t input_handler(int rq, void *dev_id, struct pt_regs *regs)
{
	struct xenkbd_device *dev = dev_id;
	struct xenkbd_info *info = dev ? dev->info : 0;
	static int button_map[3] = { BTN_RIGHT, BTN_MIDDLE, BTN_LEFT };
	__u32 cons, prod;

	if (!info || !info->initialized)
		return IRQ_NONE;

	prod = info->in_prod;
	rmb();			/* ensure we see ring contents up to prod */
	for (cons = info->in_cons; cons != prod; cons++) {
		union xenkbd_in_event *event;
		event = &XENKBD_RING_REF(info->in, cons);

		switch (event->type) {
		case XENKBD_TYPE_MOTION:
			input_report_rel(dev->dev, REL_X, event->motion.rel_x);
			input_report_rel(dev->dev, REL_Y, event->motion.rel_y);
			break;
		case XENKBD_TYPE_BUTTON:
			if (event->button.button < 3)
				input_report_key(dev->dev,
						 button_map[event->button.button],
						 event->button.pressed);
			break;
		case XENKBD_TYPE_KEY:
			input_report_key(dev->dev, event->key.keycode, event->key.pressed);
			break;
		}

		notify_remote_via_evtchn(dev->evtchn);
	}
	input_sync(dev->dev);
	/* FIXME do I need a wmb() here? */
	info->in_cons = cons;

	return IRQ_HANDLED;
}

static struct xenkbd_device *xenkbd_dev;
static int xenkbd_irq;

int __init xenkbd_init(void)
{
	int ret = 0;
	int i;
	struct xenkbd_device *dev;
	struct input_dev *input_dev;
	struct evtchn_alloc_unbound alloc_unbound;
	struct xenbus_transaction xbt;

	if (is_initial_xendomain())
		return -ENODEV;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!dev || !input_dev)
		return -ENOMEM;

	dev->dev = input_dev;
	dev->info = (void *)__get_free_page(GFP_KERNEL);
	if (!dev->info) {
		ret = -ENOMEM;
		goto error;
	}
	dev->info->initialized = 0;

	alloc_unbound.dom = DOMID_SELF;
	alloc_unbound.remote_dom = 0;
	ret = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound,
					  &alloc_unbound);
	if (ret)
		goto error_freep;
	dev->evtchn = alloc_unbound.port;
	ret = bind_evtchn_to_irqhandler(dev->evtchn, input_handler, 0,
					"xenkbd", dev);
	if (ret < 0)
		goto error_freep;

	xenkbd_irq = ret;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	input_dev->keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT);
	input_dev->relbit[0] = BIT(REL_X) | BIT(REL_Y);

	/* FIXME not sure this is quite right */
	for (i = 0; i < 256; i++)
		set_bit(i, input_dev->keybit);

	input_dev->name = "Xen Virtual Keyboard/Mouse";

	input_register_device(input_dev);

 again:
	ret = xenbus_transaction_start(&xbt);
	if (ret)
		goto error_unreg;
	ret = xenbus_printf(xbt, "vkbd", "page-ref", "%lu",
			    virt_to_mfn(dev->info));
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, "vkbd", "event-channel", "%u",
			    dev->evtchn);
	if (ret)
		goto error_xenbus;
	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN)
			goto again;
		/* FIXME really retry forever? */
		goto error_unreg;
	}

	dev->info->in_cons = dev->info->in_prod = 0;
	dev->info->out_cons = dev->info->out_prod = 0;
	dev->info->initialized = 1; /* FIXME needed?  move up? */

	xenkbd_dev = dev;

	return ret;


 error_xenbus:
	xenbus_transaction_end(xbt, 1);
 error_unreg:
	input_unregister_device(input_dev);
	unbind_from_irqhandler(xenkbd_irq, dev);
	xenkbd_irq = 0;
 error_freep:
	free_page((unsigned long)dev->info);
 error:
	kfree(dev);
	xenkbd_dev = NULL;
	return ret;
}

static void __exit xenkbd_cleanup(void)
{
	input_unregister_device(xenkbd_dev->dev);
	unbind_from_irqhandler(xenkbd_irq, xenkbd_dev);
	xenkbd_irq = 0;
	free_page((unsigned long)xenkbd_dev->info);
	kfree(xenkbd_dev);
	xenkbd_dev = NULL;
}

void xenkbd_resume(void)
{
#if 0 /* FIXME */
	int ret;

	if (xenkbd_dev && xen_start_info->kbd_evtchn) {
		if (xenkbd_irq)
			unbind_from_irqhandler(xenkbd_irq, NULL);

		ret = bind_evtchn_to_irqhandler(xen_start_info->kbd_evtchn,
						input_handler,
						0,
						"xenkbd",
						xenkbd_dev);

		if (ret <= 0)
			return;

		xenkbd_irq = ret;
		xenkbd_dev->info = mfn_to_virt(xen_start_info->kbd_mfn);
	}
#else
	printk(KERN_DEBUG "xenkbd_resume not implemented\n");
#endif
}

module_init(xenkbd_init);
module_exit(xenkbd_cleanup);

MODULE_LICENSE("GPL");
