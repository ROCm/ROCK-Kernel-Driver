/*
 * $Id: cx88-dvb.c,v 1.9 2004/09/15 16:15:24 kraxel Exp $
 *
 * device driver for Conexant 2388x based TV cards
 * MPEG Transport Stream (DVB) routines
 *
 * (c) 2004 Chris Pascoe <c.pascoe@itee.uq.edu.au>
 * (c) 2004 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/file.h>

#include "cx88.h"

MODULE_DESCRIPTION("driver for cx2388x based DVB cards");
MODULE_AUTHOR("Chris Pascoe <c.pascoe@itee.uq.edu.au>");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int debug = 0;
MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug,"enable debug messages [dvb]");

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/2-dvb: " fmt, dev->core->name , ## arg)

/* ------------------------------------------------------------------ */

static int dvb_buf_setup(struct file *file, unsigned int *count, unsigned int *size)
{
	struct cx8802_dev *dev = file->private_data;

	dev->ts_packet_size  = 188 * 4;
	dev->ts_packet_count = 32;

	*size  = dev->ts_packet_size * dev->ts_packet_count;
	*count = 32;
	return 0;
}

static int dvb_buf_prepare(struct file *file, struct videobuf_buffer *vb,
			   enum v4l2_field field)
{
	struct cx8802_dev *dev = file->private_data;
	return cx8802_buf_prepare(dev, (struct cx88_buffer*)vb);
}

static void dvb_buf_queue(struct file *file, struct videobuf_buffer *vb)
{
	struct cx8802_dev *dev = file->private_data;
	cx8802_buf_queue(dev, (struct cx88_buffer*)vb);
}

static void dvb_buf_release(struct file *file, struct videobuf_buffer *vb)
{
	struct cx8802_dev *dev = file->private_data;
	cx88_free_buffer(dev->pci, (struct cx88_buffer*)vb);
}

struct videobuf_queue_ops dvb_qops = {
	.buf_setup    = dvb_buf_setup,
	.buf_prepare  = dvb_buf_prepare,
	.buf_queue    = dvb_buf_queue,
	.buf_release  = dvb_buf_release,
};

static int dvb_thread(void *data)
{
	struct cx8802_dev *dev = data;
	struct videobuf_buffer *buf;
	struct file *file;
	unsigned long flags;
	int err;
	
	dprintk(1,"dvb thread started\n");
	file = get_empty_filp();
	file->private_data = dev;
	videobuf_read_start(file, &dev->dvbq);
	
	for (;;) {
		/* fetch next buffer */
		buf = list_entry(dev->dvbq.stream.next,
				 struct videobuf_buffer, stream);
		list_del(&buf->stream);
		err = videobuf_waiton(buf,0,1);
		BUG_ON(0 != err);

		/* no more feeds left or stop_feed() asked us to quit */
		if (0 == dev->nfeeds)
			break;
		if (kthread_should_stop())
			break;

		/* feed buffer data to demux */
		if (buf->state == STATE_DONE)
			dvb_dmx_swfilter(&dev->demux, buf->dma.vmalloc,
					 buf->size);
		
		/* requeue buffer */
		list_add_tail(&buf->stream,&dev->dvbq.stream);
		spin_lock_irqsave(dev->dvbq.irqlock,flags);
		dev->dvbq.ops->buf_queue(file,buf);
		spin_unlock_irqrestore(dev->dvbq.irqlock,flags);
		
		/* log errors if any */
		if (dev->error_count || dev->stopper_count) {
			printk("%s: error=%d stopper=%d\n",
			       dev->core->name, dev->error_count,
			       dev->stopper_count);
			dev->error_count   = 0;
			dev->stopper_count = 0;
		}
		if (debug && dev->timeout_count) {
			printk("%s: timeout=%d (FE not locked?)\n",
			       dev->core->name, dev->timeout_count);
			dev->timeout_count = 0;
		}
	}

	videobuf_read_stop(file, &dev->dvbq);
	put_filp(file);
	dprintk(1,"dvb thread stopped\n");

	/* Hmm, linux becomes *very* unhappy without this ... */
	while (!kthread_should_stop()) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	return 0;
}

/* ---------------------------------------------------------------------------- */

static int dvb_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct cx8802_dev *dev = demux->priv;
	int rc;

	if (!demux->dmx.frontend)
		return -EINVAL;

	down(&dev->lock);
	dev->nfeeds++;
	rc = dev->nfeeds;

	if (NULL != dev->dvb_thread)
		goto out;
	dev->dvb_thread = kthread_run(dvb_thread, dev, "%s dvb", dev->core->name);
	if (IS_ERR(dev->dvb_thread)) {
		rc = PTR_ERR(dev->dvb_thread);
		dev->dvb_thread = NULL;
	}

out:
	up(&dev->lock);
	dprintk(2, "%s rc=%d\n",__FUNCTION__,rc);
	return rc;
}

static int dvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct cx8802_dev *dev = demux->priv;
	int err = 0;

	dprintk(2, "%s\n",__FUNCTION__);

	down(&dev->lock);
	dev->nfeeds--;
	if (0 == dev->nfeeds  &&  NULL != dev->dvb_thread) {
		cx8802_cancel_buffers(dev);
		err = kthread_stop(dev->dvb_thread);
		dev->dvb_thread = NULL;
	}
	up(&dev->lock);
	return err;
}

static void dvb_unregister(struct cx8802_dev *dev)
{
#if 1 /* really needed? */
	down(&dev->lock);
	if (NULL != dev->dvb_thread) {
		kthread_stop(dev->dvb_thread);
		BUG();
	}
	up(&dev->lock);
#endif

	dvb_net_release(&dev->dvbnet);
	dev->demux.dmx.remove_frontend(&dev->demux.dmx, &dev->fe_mem);
	dev->demux.dmx.remove_frontend(&dev->demux.dmx, &dev->fe_hw);
	dvb_dmxdev_release(&dev->dmxdev);
	dvb_dmx_release(&dev->demux);
	cx88_call_i2c_clients(dev->core, FE_UNREGISTER, dev->core->dvb_adapter);
	dvb_unregister_adapter(dev->core->dvb_adapter);
	dev->core->dvb_adapter = NULL;
	return;
}

static int dvb_register(struct cx8802_dev *dev)
{
	int result;

	result = dvb_register_adapter(&dev->core->dvb_adapter, dev->core->name,
				      THIS_MODULE);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_register_adapter failed (errno = %d)\n",
		       dev->core->name, result);
		goto fail1;
	}
	cx88_call_i2c_clients(dev->core, FE_REGISTER, dev->core->dvb_adapter);

	dev->demux.dmx.capabilities =
		DMX_TS_FILTERING | DMX_SECTION_FILTERING |
		DMX_MEMORY_BASED_FILTERING;
	dev->demux.priv       = dev;
	dev->demux.filternum  = 256;
	dev->demux.feednum    = 256;
	dev->demux.start_feed = dvb_start_feed;
	dev->demux.stop_feed  = dvb_stop_feed;
	result = dvb_dmx_init(&dev->demux);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_dmx_init failed (errno = %d)\n",
		       dev->core->name, result);
		goto fail2;
	}

	dev->dmxdev.filternum    = 256;
	dev->dmxdev.demux        = &dev->demux.dmx;
	dev->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dev->dmxdev, dev->core->dvb_adapter);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_dmxdev_init failed (errno = %d)\n",
		       dev->core->name, result);
		goto fail3;
	}

	dev->fe_hw.source = DMX_FRONTEND_0;
	result = dev->demux.dmx.add_frontend(&dev->demux.dmx, &dev->fe_hw);
	if (result < 0) {
		printk(KERN_WARNING "%s: add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       dev->core->name, result);
		goto fail4;
	}

	dev->fe_mem.source = DMX_MEMORY_FE;
	result = dev->demux.dmx.add_frontend(&dev->demux.dmx, &dev->fe_mem);
	if (result < 0) {
		printk(KERN_WARNING "%s: add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
		       dev->core->name, result);
		goto fail5;
	}

	result = dev->demux.dmx.connect_frontend(&dev->demux.dmx, &dev->fe_hw);
	if (result < 0) {
		printk(KERN_WARNING "%s: connect_frontend failed (errno = %d)\n",
		       dev->core->name, result);
		goto fail6;
	}

	dvb_net_init(dev->core->dvb_adapter, &dev->dvbnet, &dev->demux.dmx);
	return 0;

fail6:
	dev->demux.dmx.remove_frontend(&dev->demux.dmx, &dev->fe_mem);
fail5:
	dev->demux.dmx.remove_frontend(&dev->demux.dmx, &dev->fe_hw);
fail4:
	dvb_dmxdev_release(&dev->dmxdev);
fail3:
	dvb_dmx_release(&dev->demux);
fail2:
	cx88_call_i2c_clients(dev->core, FE_UNREGISTER, dev->core->dvb_adapter);
	dvb_unregister_adapter(dev->core->dvb_adapter);
fail1:
	return result;
}

/* ----------------------------------------------------------- */

static int __devinit dvb_probe(struct pci_dev *pci_dev,
			       const struct pci_device_id *pci_id)
{
	struct cx8802_dev *dev;
	struct cx88_core  *core;
	int err;

	/* general setup */
	core = cx88_core_get(pci_dev);
	if (NULL == core)
		return -EINVAL;

	err = -ENODEV;
	if (!cx88_boards[core->board].dvb)
		goto fail_core;

	err = -ENOMEM;
	dev = kmalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		goto fail_core;
	memset(dev,0,sizeof(*dev));
	dev->pci = pci_dev;
	dev->core = core;

	err = cx8802_init_common(dev);
	if (0 != err)
		goto fail_free;

	/* dvb stuff */
	printk("%s/2: cx2388x based dvb card\n", core->name);
	videobuf_queue_init(&dev->dvbq, &dvb_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_TOP,
			    sizeof(struct cx88_buffer));
	init_MUTEX(&dev->dvbq.lock);
	
	err = dvb_register(dev);
	if (0 != err)
		goto fail_free;
	return 0;

 fail_free:
	kfree(dev);
 fail_core:
	cx88_core_put(core,pci_dev);
	return err;
}

static void __devexit dvb_remove(struct pci_dev *pci_dev)
{
        struct cx8802_dev *dev = pci_get_drvdata(pci_dev);

	/* dvb */
	dvb_unregister(dev);

	/* common */
	cx8802_fini_common(dev);
	cx88_core_put(dev->core,dev->pci);
	kfree(dev);
}

static struct pci_device_id cx8802_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8802,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, cx8802_pci_tbl);

static struct pci_driver dvb_pci_driver = {
        .name     = "cx88-dvb",
        .id_table = cx8802_pci_tbl,
        .probe    = dvb_probe,
        .remove   = dvb_remove,
};

static int dvb_init(void)
{
	printk(KERN_INFO "cx2388x dvb driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_module_init(&dvb_pci_driver);
}

static void dvb_fini(void)
{
	pci_unregister_driver(&dvb_pci_driver);
}

module_init(dvb_init);
module_exit(dvb_fini);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
