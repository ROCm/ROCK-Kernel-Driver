/*
 * device driver for philips saa7134 based TV cards
 * video4linux video interface
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
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

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "saa7134-reg.h"
#include "saa7134.h"

#include <media/saa6752hs.h>

/* ------------------------------------------------------------------ */

#define TS_PACKET_SIZE 188 /* TS packets 188 bytes */

static unsigned int ts_debug  = 0;
MODULE_PARM(ts_debug,"i");
MODULE_PARM_DESC(ts_debug,"enable debug messages [ts]");

static unsigned int tsbufs = 4;
MODULE_PARM(tsbufs,"i");
MODULE_PARM_DESC(tsbufs,"number of ts buffers, range 2-32");

static unsigned int ts_nr_packets = 30;
MODULE_PARM(ts_nr_packets,"i");
MODULE_PARM_DESC(ts_nr_packets,"size of a ts buffers (in ts packets)");

#define dprintk(fmt, arg...)	if (ts_debug) \
	printk(KERN_DEBUG "%s/ts: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */

static int buffer_activate(struct saa7134_dev *dev,
			   struct saa7134_buf *buf,
			   struct saa7134_buf *next)
{
	u32 control;
	
	dprintk("buffer_activate [%p]",buf);
	buf->vb.state = STATE_ACTIVE;
	buf->top_seen = 0;
	
        /* dma: setup channel 5 (= TS) */
        control = SAA7134_RS_CONTROL_BURST_16 |
                SAA7134_RS_CONTROL_ME |
                (buf->pt->dma >> 12);

	if (NULL == next)
		next = buf;
	if (V4L2_FIELD_TOP == buf->vb.field) {
		dprintk("- [top]     buf=%p next=%p\n",buf,next);
		saa_writel(SAA7134_RS_BA1(5),saa7134_buffer_base(buf));
		saa_writel(SAA7134_RS_BA2(5),saa7134_buffer_base(next));
	} else {
		dprintk("- [bottom]  buf=%p next=%p\n",buf,next);
		saa_writel(SAA7134_RS_BA1(5),saa7134_buffer_base(next));
		saa_writel(SAA7134_RS_BA2(5),saa7134_buffer_base(buf));
	}
	saa_writel(SAA7134_RS_PITCH(5),TS_PACKET_SIZE);
	saa_writel(SAA7134_RS_CONTROL(5),control);

	/* start DMA */
	saa7134_set_dmabits(dev);
	
	mod_timer(&dev->ts_q.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

static int buffer_prepare(struct file *file, struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct saa7134_dev *dev = file->private_data;
	struct saa7134_buf *buf = (struct saa7134_buf *)vb;
	unsigned int lines, llength, size;
	int err;
	
	dprintk("buffer_prepare [%p,%s]\n",buf,v4l2_field_names[field]);

	llength = TS_PACKET_SIZE;
	lines = ts_nr_packets;
	
	size = lines * llength;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	if (buf->vb.size != size) {
		saa7134_dma_free(dev,buf);
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		buf->vb.width  = llength;
		buf->vb.height = lines;
		buf->vb.size   = size;
		buf->pt        = &dev->ts.pt_ts;

		err = videobuf_iolock(dev->pci,&buf->vb,NULL);
		if (err)
			goto oops;
		err = saa7134_pgtable_build(dev->pci,buf->pt,
					    buf->vb.dma.sglist,
					    buf->vb.dma.sglen,
					    saa7134_buffer_startpage(buf));
		if (err)
			goto oops;
	}
	buf->vb.state = STATE_PREPARED;
	buf->activate = buffer_activate;
	buf->vb.field = field;
	return 0;

 oops:
	saa7134_dma_free(dev,buf);
	return err;
}

static int
buffer_setup(struct file *file, unsigned int *count, unsigned int *size)
{
	*size = TS_PACKET_SIZE * ts_nr_packets;
	if (0 == *count)
		*count = tsbufs;
	*count = saa7134_buffer_count(*size,*count);
	return 0;
}

static void buffer_queue(struct file *file, struct videobuf_buffer *vb)
{
	struct saa7134_dev *dev = file->private_data;
	struct saa7134_buf *buf = (struct saa7134_buf *)vb;
	
	saa7134_buffer_queue(dev,&dev->ts_q,buf);
}

static void buffer_release(struct file *file, struct videobuf_buffer *vb)
{
	struct saa7134_dev *dev = file->private_data;
	struct saa7134_buf *buf = (struct saa7134_buf *)vb;
	
	saa7134_dma_free(dev,buf);
}

static struct videobuf_queue_ops ts_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};


/* ------------------------------------------------------------------ */

static void ts_reset_encoder(struct saa7134_dev* dev) 
{
	saa_writeb(SAA7134_SPECIAL_MODE, 0x00);
	mdelay(10);
   	saa_writeb(SAA7134_SPECIAL_MODE, 0x01);
   	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/10);
}

static int ts_init_encoder(struct saa7134_dev* dev, void* arg) 
{
	ts_reset_encoder(dev);
	saa7134_i2c_call_clients(dev, MPEG_SETPARAMS, arg);
 	return 0;
}


/* ------------------------------------------------------------------ */

static int ts_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct saa7134_dev *h,*dev = NULL;
	struct list_head *list;
	int err;
	
	list_for_each(list,&saa7134_devlist) {
		h = list_entry(list, struct saa7134_dev, devlist);
		if (h->ts_dev && h->ts_dev->minor == minor)
			dev = h;
	}
	if (NULL == dev)
		return -ENODEV;

	dprintk("open minor=%d\n",minor);
	down(&dev->ts.ts.lock);
	err = -EBUSY;
	if (dev->ts.users)
		goto done;

	dev->ts.started = 0;
	dev->ts.users++;
	file->private_data = dev;
	err = 0;

 done:
	up(&dev->ts.ts.lock);
	return err;
}

static int ts_release(struct inode *inode, struct file *file)
{
	struct saa7134_dev *dev = file->private_data;

	if (dev->ts.ts.streaming)
		videobuf_streamoff(file,&dev->ts.ts);
	down(&dev->ts.ts.lock);
	if (dev->ts.ts.reading)
		videobuf_read_stop(file,&dev->ts.ts);
	dev->ts.users--;

	/* stop the encoder */
	if (dev->ts.started)
	      	ts_reset_encoder(dev);
  
	up(&dev->ts.ts.lock);
	return 0;
}

static ssize_t
ts_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct saa7134_dev *dev = file->private_data;

	if (!dev->ts.started) {
		ts_init_encoder(dev, NULL);
		dev->ts.started = 1;
	}
  
	return videobuf_read_stream(file, &dev->ts.ts, data, count, ppos, 0);
}

static unsigned int
ts_poll(struct file *file, struct poll_table_struct *wait)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_poll_stream(file, &dev->ts.ts, wait);
}


static int
ts_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_mmap_mapper(vma, &dev->ts.ts);
}

/*
 * This function is _not_ called directly, but from
 * video_generic_ioctl (and maybe others).  userspace
 * copying is done already, arg is a kernel pointer.
 */
static int ts_do_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, void *arg)
{
	struct saa7134_dev *dev = file->private_data;

	if (ts_debug > 1)
		saa7134_print_ioctl(dev->name,cmd);
	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->driver, "saa7134");
		strlcpy(cap->card, saa7134_boards[dev->board].name,
			sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
		cap->version = SAA7134_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE |
			V4L2_CAP_STREAMING;
		return 0;
	}

	/* --- input switching --------------------------------------- */
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;
		
		if (i->index != 0)
			return -EINVAL;
		i->type = V4L2_INPUT_TYPE_CAMERA;
		strcpy(i->name,"CCIR656");
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *i = arg;
		*i = 0;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		int *i = arg;
		
		if (*i != 0)
			return -EINVAL;
		return 0;
	}
	/* --- capture ioctls ---------------------------------------- */
	
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		int index;

		index = f->index;
		if (index != 0)
			return -EINVAL;
		
		memset(f,0,sizeof(*f));
		f->index = index;
		strlcpy(f->description, "MPEG TS", sizeof(f->description));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->pixelformat = V4L2_PIX_FMT_MPEG;
		return 0;
	}

	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;

		memset(f,0,sizeof(*f));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		
		/* FIXME: translate subsampling type EMPRESS into
		 *        width/height: */
		f->fmt.pix.width        = 720; /* D1 */
		f->fmt.pix.height       = 576;
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.sizeimage    = TS_PACKET_SIZE*ts_nr_packets;
		return 0;
	}
	
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;

		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		    return -EINVAL;

		/* 
		  FIXME: translate and round width/height into EMPRESS
		  subsample type:
		 
		          type  |   PAL   |  NTSC 
			---------------------------
			  SIF   | 352x288 | 352x240
			 1/2 D1 | 352x576 | 352x480
			 2/3 D1 | 480x576 | 480x480
			  D1    | 720x576 | 720x480
		*/

		f->fmt.pix.width        = 720; /* D1 */
		f->fmt.pix.height       = 576;
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.sizeimage    = TS_PACKET_SIZE*ts_nr_packets;
		return 0;
	}

	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(file,&dev->ts.ts,arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(&dev->ts.ts,arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(file,&dev->ts.ts,arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(file,&dev->ts.ts,arg);

	case VIDIOC_STREAMON:
		return videobuf_streamon(file,&dev->ts.ts);

	case VIDIOC_STREAMOFF:
		return videobuf_streamoff(file,&dev->ts.ts);

	case VIDIOC_QUERYCTRL:
	case VIDIOC_G_CTRL:
	case VIDIOC_S_CTRL:
		return saa7134_common_ioctl(dev, cmd, arg);

	case MPEG_SETPARAMS:
		return ts_init_encoder(dev, arg);

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int ts_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, ts_do_ioctl);
}


static struct file_operations ts_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = ts_open,
	.release  = ts_release,
	.read	  = ts_read,
	.poll	  = ts_poll,
	.mmap	  = ts_mmap,
	.ioctl	  = ts_ioctl,
	.llseek   = no_llseek,
};


/* ----------------------------------------------------------- */
/* exported stuff                                              */

struct video_device saa7134_ts_template =
{
	.name          = "saa7134-ts",
	.type          = 0 /* FIXME */,
	.type2         = 0 /* FIXME */,
	.hardware      = 0,
	.fops          = &ts_fops,
	.minor	       = -1,
};

int saa7134_ts_init1(struct saa7134_dev *dev)
{
	/* sanitycheck insmod options */
	if (tsbufs < 2)
		tsbufs = 2;
	if (tsbufs > VIDEO_MAX_FRAME)
		tsbufs = VIDEO_MAX_FRAME;
	if (ts_nr_packets < 4)
		ts_nr_packets = 4;
	if (ts_nr_packets > 312)
		ts_nr_packets = 312;

	INIT_LIST_HEAD(&dev->ts_q.queue);
	init_timer(&dev->ts_q.timeout);
	dev->ts_q.timeout.function = saa7134_buffer_timeout;
	dev->ts_q.timeout.data     = (unsigned long)(&dev->ts_q);
	dev->ts_q.dev              = dev;
	dev->ts_q.need_two         = 1;
	videobuf_queue_init(&dev->ts.ts, &ts_qops, dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf));
	saa7134_pgtable_alloc(dev->pci,&dev->ts.pt_ts);

	/* init TS hw */
	saa_writeb(SAA7134_TS_SERIAL1, 0x00);  /* deactivate TS softreset */
	saa_writeb(SAA7134_TS_PARALLEL, 0xec); /* TSSOP high active, TSVAL high active, TSLOCK ignored */
	saa_writeb(SAA7134_TS_PARALLEL_SERIAL, (TS_PACKET_SIZE-1));
	saa_writeb(SAA7134_TS_DMA0, ((ts_nr_packets-1)&0xff));
	saa_writeb(SAA7134_TS_DMA1, (((ts_nr_packets-1)>>8)&0xff));
	saa_writeb(SAA7134_TS_DMA2, ((((ts_nr_packets-1)>>16)&0x3f) | 0x00)); /* TSNOPIT=0, TSCOLAP=0 */
 
	return 0;
}

int saa7134_ts_fini(struct saa7134_dev *dev)
{
	/* nothing */
	saa7134_pgtable_free(dev->pci,&dev->ts.pt_ts);
	return 0;
}


void saa7134_irq_ts_done(struct saa7134_dev *dev, unsigned long status)
{
	enum v4l2_field field;

	spin_lock(&dev->slock);
	if (dev->ts_q.curr) {
		field = dev->ts_q.curr->vb.field;
		if (field == V4L2_FIELD_TOP) {
			if ((status & 0x100000) != 0x100000)
				goto done;
		} else {
			if ((status & 0x100000) != 0x000000)
				goto done;
		}
		saa7134_buffer_finish(dev,&dev->ts_q,STATE_DONE);
	}
	saa7134_buffer_next(dev,&dev->ts_q);

 done:
	spin_unlock(&dev->slock);
}

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
