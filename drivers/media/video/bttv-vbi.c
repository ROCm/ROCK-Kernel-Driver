/*
    bttv - Bt848 frame grabber driver
    vbi interface
    
    (c) 2002 Gerd Knorr <kraxel@bytesex.org>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <asm/io.h>
#include "bttvp.h"

#define VBI_DEFLINES 16
#define VBI_MAXLINES 32

static unsigned int vbibufs = 4;
static unsigned int vbi_debug = 0;

MODULE_PARM(vbibufs,"i");
MODULE_PARM_DESC(vbibufs,"number of vbi buffers, range 2-32, default 4");
MODULE_PARM(vbi_debug,"i");
MODULE_PARM_DESC(vbi_debug,"vbi code debug messages, default is 0 (no)");

#ifdef dprintk
# undef dprintk
#endif
#define dprintk(fmt, arg...)	if (vbi_debug) \
	printk(KERN_DEBUG "bttv%d/vbi: " fmt, btv->nr, ## arg)

#ifndef HAVE_V4L2
/* some dummy defines to avoid cluttering up the source code with
   a huge number of ifdef's for V4L2 */
# define V4L2_BUF_TYPE_CAPTURE -1
# define V4L2_BUF_TYPE_VBI     -1
#endif

/* ----------------------------------------------------------------------- */
/* vbi risc code + mm                                                      */

static int
vbi_buffer_risc(struct bttv *btv, struct bttv_buffer *buf)
{
	int bpl = 2048;

	bttv_risc_packed(btv, &buf->odd, buf->vb.dma.sglist,
			 0, bpl-4, 4, btv->vbi.lines);
	bttv_risc_packed(btv, &buf->even, buf->vb.dma.sglist,
			 btv->vbi.lines * bpl, bpl-4, 4, btv->vbi.lines);
	return 0;
}

static int vbi_buffer_prepare(struct bttv *btv, struct bttv_buffer *buf)
{
	int rc;
	
	buf->vb.size = btv->vbi.lines * 2 * 2048;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (STATE_NEEDS_INIT == buf->vb.state) {
		if (0 != (rc = videobuf_iolock(btv->dev,&buf->vb)))
			goto fail;
		if (0 != (rc = vbi_buffer_risc(btv,buf)))
			goto fail;
	}
	buf->vb.state = STATE_PREPARED;
	dprintk("buf prepare ok: odd=%p even=%p\n",&buf->odd,&buf->even);
	return 0;

 fail:
	bttv_dma_free(btv,buf);
	return rc;
}

static void
vbi_buffer_queue(struct bttv *btv, struct bttv_buffer *buf)
{
	unsigned long flags;
	
	buf->vb.state = STATE_QUEUED;
	spin_lock_irqsave(&btv->s_lock,flags);
	list_add_tail(&buf->vb.queue,&btv->vcapture);
	bttv_set_dma(btv,0x0c,1);
	spin_unlock_irqrestore(&btv->s_lock,flags);
}

static void vbi_buffer_release(struct file *file, struct videobuf_buffer *vb)
{
	struct bttv *btv = file->private_data;
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;

	bttv_dma_free(btv,buf);
}

static void
vbi_cancel_all(struct bttv *btv)
{
	unsigned long flags;
	int i;

	/* remove queued buffers from list */
	spin_lock_irqsave(&btv->s_lock,flags);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == btv->vbi.bufs[i])
			continue;
		if (btv->vbi.bufs[i]->vb.state == STATE_QUEUED) {
			list_del(&btv->vbi.bufs[i]->vb.queue);
			btv->vbi.bufs[i]->vb.state = STATE_ERROR;
		}
	}
	spin_unlock_irqrestore(&btv->s_lock,flags);

	/* free all buffers + clear queue */
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == btv->vbi.bufs[i])
			continue;
		bttv_dma_free(btv,btv->vbi.bufs[i]);
	}
	INIT_LIST_HEAD(&btv->vbi.stream);
}

/* ----------------------------------------------------------------------- */

static int vbi_read_start(struct file *file, struct bttv *btv)
{
	int err,size,count,i;
	
	if (vbibufs < 2 || vbibufs > VIDEO_MAX_FRAME)
		vbibufs = 2;

	count = vbibufs;
	size  = btv->vbi.lines * 2 * 2048;
	err = videobuf_mmap_setup(file,
				  (struct videobuf_buffer**)btv->vbi.bufs,
				  sizeof(struct bttv_buffer),
				  count,size,V4L2_BUF_TYPE_VBI,
				  vbi_buffer_release);
	if (err)
		return err;
	for (i = 0; i < count; i++) {
		err = vbi_buffer_prepare(btv,btv->vbi.bufs[i]);
		if (err)
			return err;
		list_add_tail(&btv->vbi.bufs[i]->vb.stream,&btv->vbi.stream);
		vbi_buffer_queue(btv,btv->vbi.bufs[i]);
	}
	btv->vbi.reading = 1;
	return 0;
}

static void vbi_read_stop(struct bttv *btv)
{
	int i;

	vbi_cancel_all(btv);
	INIT_LIST_HEAD(&btv->vbi.stream);
	for (i = 0; i < vbibufs; i++) {
		kfree(btv->vbi.bufs[i]);
		btv->vbi.bufs[i] = NULL;
	}
	btv->vbi.reading = 0;
}

static void vbi_setlines(struct bttv *btv, int lines)
{
	int vdelay;

	if (lines < 1)
		lines = 1;
	if (lines > VBI_MAXLINES)
		lines = VBI_MAXLINES;
	btv->vbi.lines = lines;

	vdelay = btread(BT848_E_VDELAY_LO);
	if (vdelay < lines*2) {
		vdelay = lines*2;
		btwrite(vdelay,BT848_E_VDELAY_LO);
		btwrite(vdelay,BT848_O_VDELAY_LO);
	}
}

#ifdef HAVE_V4L2
static void vbi_fmt(struct bttv *btv, struct v4l2_format *f)
{
	memset(f,0,sizeof(*f));
	f->type = V4L2_BUF_TYPE_VBI;
	f->fmt.vbi.sampling_rate    = 35468950;
	f->fmt.vbi.samples_per_line = 2048;
	f->fmt.vbi.sample_format    = V4L2_VBI_SF_UBYTE;
	f->fmt.vbi.offset           = 244;
	f->fmt.vbi.count[0]         = btv->vbi.lines;
	f->fmt.vbi.count[1]         = btv->vbi.lines;
	f->fmt.vbi.flags            = 0;
	switch (btv->tvnorm) {
	case 1: /* NTSC */
		f->fmt.vbi.start[0] = 10;
		f->fmt.vbi.start[1] = 273;
		break;
	case 0: /* PAL */
	case 2: /* SECAM */
	default:
		f->fmt.vbi.start[0] = 7;
		f->fmt.vbi.start[1] = 319;
	}
}
#endif

/* ----------------------------------------------------------------------- */
/* vbi interface                                                           */

static int vbi_open(struct inode *inode, struct file *file)
{
	unsigned int minor = minor(inode->i_rdev);
	struct bttv *btv = NULL;
	int i;


	for (i = 0; i < bttv_num; i++) {
		if (bttvs[i].vbi_dev.minor == minor) {
			btv = &bttvs[i];
			break;
		}
	}
	if (NULL == btv)
		return -ENODEV;

	down(&btv->vbi.lock);
	if (btv->vbi.users) {
		up(&btv->vbi.lock);
		return -EBUSY;
	}
	dprintk("open minor=%d\n",minor);
	file->private_data = btv;
	btv->vbi.users++;
	bttv_field_count(btv);
	vbi_setlines(btv,VBI_DEFLINES);
	
	up(&btv->vbi.lock);
	
	return 0;
}

static int vbi_release(struct inode *inode, struct file *file)
{
	struct bttv    *btv = file->private_data;

	down(&btv->vbi.lock);
	if (btv->vbi.reading) {
		vbi_read_stop(btv);
		btv->vbi.read_buf = NULL;
	}
	btv->vbi.users--;
	bttv_field_count(btv);
	vbi_setlines(btv,0);
	up(&btv->vbi.lock);
	return 0;
}

static int vbi_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, void *arg)
{
	struct bttv *btv = file->private_data;
#ifdef HAVE_V4L2
	unsigned long flags;
	int err;
#endif
	
	if (btv->errors)
		bttv_reinit_bt848(btv);
	switch (cmd) {
	case VIDIOCGCAP:
	{
                struct video_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->name,btv->vbi_dev.name);
                cap->type = VID_TYPE_TUNER|VID_TYPE_TELETEXT;
                return 0;
	}

	/* vbi/teletext ioctls */
	case BTTV_VBISIZE:
		return btv->vbi.lines * 2 * 2048;

	case BTTV_VERSION:
        case VIDIOCGFREQ:
        case VIDIOCSFREQ:
        case VIDIOCGTUNER:
        case VIDIOCSTUNER:
        case VIDIOCGCHAN:
        case VIDIOCSCHAN:
		return bttv_common_ioctls(btv,cmd,arg);

#ifdef HAVE_V4L2
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->name, btv->name);
		cap->type = V4L2_TYPE_VBI;
		cap->flags = V4L2_FLAG_TUNER | V4L2_FLAG_READ |
			V4L2_FLAG_STREAMING | V4L2_FLAG_SELECT;
		return 0;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;

		vbi_fmt(btv,f);
		return 0;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;

		if (btv->vbi.reading || btv->vbi.streaming)
			return -EBUSY;
		vbi_setlines(btv,f->fmt.vbi.count[0]);
		vbi_fmt(btv,f);
		return 0;
	}

	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *req = arg;
		int size,count;

		if ((req->type & V4L2_BUF_TYPE_field) != V4L2_BUF_TYPE_VBI)
			return -EINVAL;
		if (req->count < 1)
			return -EINVAL;

		down(&btv->vbi.lock);
		err = -EINVAL;

		size  = btv->vbi.lines * 2 * 2048;
		size  = (size + PAGE_SIZE - 1) & PAGE_MASK;
		count = req->count;
		if (count > VIDEO_MAX_FRAME)
			count = VIDEO_MAX_FRAME;
		err = videobuf_mmap_setup(file,
					  (struct videobuf_buffer**)btv->vbi.bufs,
					  sizeof(struct bttv_buffer),
					  count,size,V4L2_BUF_TYPE_CAPTURE,
					  vbi_buffer_release);
		if (err < 0)
			goto fh_unlock_and_return;
		req->type  = V4L2_BUF_TYPE_VBI;
		req->count = count;
		up(&btv->vbi.lock);
		return 0;
	}
	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *b = arg;

		if ((b->type & V4L2_BUF_TYPE_field) != V4L2_BUF_TYPE_VBI)
			return -EINVAL;
		if (b->index < 0 || b->index > VIDEO_MAX_FRAME)
			return -EINVAL;
		if (NULL == btv->vbi.bufs[b->index])
			return -EINVAL;
		videobuf_status(b,&btv->vbi.bufs[b->index]->vb);
		return 0;
	}
	case VIDIOC_QBUF:
	{
		struct v4l2_buffer *b = arg;
		struct bttv_buffer *buf;
		
		if ((b->type & V4L2_BUF_TYPE_field) != V4L2_BUF_TYPE_VBI)
			return -EINVAL;
		if (b->index < 0 || b->index > VIDEO_MAX_FRAME)
			return -EINVAL;

		down(&btv->vbi.lock);
		err = -EINVAL;
		buf = btv->vbi.bufs[b->index];
		if (NULL == buf)
			goto fh_unlock_and_return;
		if (0 == buf->vb.baddr)
			goto fh_unlock_and_return;
		if (buf->vb.state == STATE_QUEUED ||
		    buf->vb.state == STATE_ACTIVE)
			goto fh_unlock_and_return;
		err = vbi_buffer_prepare(btv,buf);
		if (0 != err)
			goto fh_unlock_and_return;

		list_add_tail(&buf->vb.stream,&btv->vbi.stream);
		if (btv->vbi.streaming)
			vbi_buffer_queue(btv,buf);
		up(&btv->vbi.lock);
		return 0;
	}
	case VIDIOC_DQBUF:
	{
		struct v4l2_buffer *b = arg;
		struct bttv_buffer *buf;

		if ((b->type & V4L2_BUF_TYPE_field) != V4L2_BUF_TYPE_VBI)
			return -EINVAL;

		down(&btv->vbi.lock);
		err = -EINVAL;
		if (list_empty(&btv->vbi.stream))
			goto fh_unlock_and_return;
		buf = list_entry(btv->vbi.stream.next,
				 struct bttv_buffer, vb.stream);
		err = videobuf_waiton(&buf->vb,0,1);
		if (err < 0)
			goto fh_unlock_and_return;
		switch (buf->vb.state) {
		case STATE_ERROR:
			err = -EIO;
			/* fall through */
		case STATE_DONE:
			videobuf_dma_pci_sync(btv->dev,&buf->vb.dma);
			buf->vb.state = STATE_IDLE;
			break;
		default:
			err = -EINVAL;
			goto fh_unlock_and_return;
		}
		list_del(&buf->vb.stream);
		memset(b,0,sizeof(*b));
		videobuf_status(b,&buf->vb);
		up(&btv->vbi.lock);
		return err;
	}
	case VIDIOC_STREAMON:
	{
		struct list_head *list;
		struct bttv_buffer *buf;

		down(&btv->vbi.lock);
		err = -EBUSY;
		if (btv->vbi.reading)
			goto fh_unlock_and_return;
		spin_lock_irqsave(&btv->s_lock,flags);
		list_for_each(list,&btv->vbi.stream) {
			buf = list_entry(list, struct bttv_buffer, vb.stream);
			if (buf->vb.state == STATE_PREPARED)
				vbi_buffer_queue(btv,buf);
		}
		spin_unlock_irqrestore(&btv->s_lock,flags);
		btv->vbi.streaming = 1;
		up(&btv->vbi.lock);
		return 0;
	}
	case VIDIOC_STREAMOFF:
	{
		down(&btv->vbi.lock);
		err = -EINVAL;
		if (!btv->vbi.streaming)
			goto fh_unlock_and_return;
		vbi_cancel_all(btv);
		INIT_LIST_HEAD(&btv->vbi.stream);
		btv->vbi.streaming = 0;
		up(&btv->vbi.lock);
		return 0;
	}

	case VIDIOC_ENUMSTD:
	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_ENUMINPUT:
	case VIDIOC_G_INPUT:
	case VIDIOC_S_INPUT:
	case VIDIOC_G_TUNER:
	case VIDIOC_S_TUNER:
	case VIDIOC_G_FREQ:
	case VIDIOC_S_FREQ:
		return bttv_common_ioctls(btv,cmd,arg);
#endif /* HAVE_V4L2 */

	default:
		return -ENOIOCTLCMD;
	}
	return 0;

#ifdef HAVE_V4L2
 fh_unlock_and_return:
	up(&btv->vbi.lock);
	return err;
#endif
}

static ssize_t vbi_read(struct file *file, char *data,
			size_t count, loff_t *ppos)
{
	unsigned int *fc;
	struct bttv *btv = file->private_data;
	int err, bytes, retval = 0;

	if (btv->errors)
		bttv_reinit_bt848(btv);
	down(&btv->vbi.lock);
	if (!btv->vbi.reading) {
		retval = vbi_read_start(file,btv);
		if (retval < 0)
			goto done;
	}

	while (count > 0) {
		/* get / wait for data */
		if (NULL == btv->vbi.read_buf) {
			btv->vbi.read_buf = list_entry(btv->vbi.stream.next,
						       struct bttv_buffer,
						       vb.stream);
			list_del(&btv->vbi.read_buf->vb.stream);
			btv->vbi.read_off = 0;
		}
		err = videobuf_waiton(&btv->vbi.read_buf->vb,
				      file->f_flags & O_NONBLOCK,1);
		if (err < 0) {
			if (0 == retval)
				retval = err;
			break;
		}

#if 1
		/* dirty, undocumented hack -- pass the frame counter
		 * within the last four bytes of each vbi data block.
		 * We need that one to maintain backward compatibility
		 * to all vbi decoding software out there ... */
		fc  = (unsigned int*)btv->vbi.read_buf->vb.dma.vmalloc;
		fc += (btv->vbi.read_buf->vb.size>>2) -1;
		*fc = btv->vbi.read_buf->vb.field_count >> 1;
#endif

		/* copy stuff */
		bytes = count;
		if (bytes > btv->vbi.read_buf->vb.size - btv->vbi.read_off)
			bytes = btv->vbi.read_buf->vb.size - btv->vbi.read_off;
		if (copy_to_user(data + retval,btv->vbi.read_buf->vb.dma.vmalloc +
				 btv->vbi.read_off,bytes)) {
			if (0 == retval)
				retval = -EFAULT;
			break;
		}
		count             -= bytes;
		retval            += bytes;
		btv->vbi.read_off += bytes;
		dprintk("read: %d bytes\n",bytes);

		/* requeue buffer when done with copying */
		if (btv->vbi.read_off == btv->vbi.read_buf->vb.size) {
			list_add_tail(&btv->vbi.read_buf->vb.stream,
				      &btv->vbi.stream);
			vbi_buffer_queue(btv,btv->vbi.read_buf);
			btv->vbi.read_buf = NULL;
		}
	}
 done:
	up(&btv->vbi.lock);
	return retval;
}

static unsigned int vbi_poll(struct file *file, poll_table *wait)
{
	struct bttv *btv = file->private_data;
	struct bttv_buffer *buf = NULL;
	unsigned int rc = 0;

	down(&btv->vbi.lock);
	if (btv->vbi.streaming) {
		if (!list_empty(&btv->vbi.stream))
			buf = list_entry(btv->vbi.stream.next,
					 struct bttv_buffer, vb.stream);
	} else {
		if (!btv->vbi.reading)
			vbi_read_start(file,btv);
		if (!btv->vbi.reading) {
			rc = POLLERR;
		} else if (NULL == btv->vbi.read_buf) {
			btv->vbi.read_buf = list_entry(btv->vbi.stream.next,
						       struct bttv_buffer,
						       vb.stream);
			list_del(&btv->vbi.read_buf->vb.stream);
			btv->vbi.read_off = 0;
		}
		buf = btv->vbi.read_buf;
	}
	if (!buf)
		rc = POLLERR;

	if (0 == rc) {
		poll_wait(file, &buf->vb.done, wait);
		if (buf->vb.state == STATE_DONE ||
		    buf->vb.state == STATE_ERROR)
			rc = POLLIN|POLLRDNORM;
	}
	up(&btv->vbi.lock);
	return rc;
}

static int
vbi_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct bttv *btv = file->private_data;
	int err;

	down(&btv->vbi.lock);
	err = videobuf_mmap_mapper
		(vma,(struct videobuf_buffer**)btv->vbi.bufs);
	up(&btv->vbi.lock);
	return err;
}

static struct file_operations vbi_fops =
{
	owner:	  THIS_MODULE,
	open:	  vbi_open,
	release:  vbi_release,
	ioctl:	  video_generic_ioctl,
	llseek:	  no_llseek,
	read:	  vbi_read,
	poll:	  vbi_poll,
	mmap:	  vbi_mmap,
};

struct video_device bttv_vbi_template =
{
	name:     "bt848/878 vbi",
	type:     VID_TYPE_TUNER|VID_TYPE_TELETEXT,
	hardware: VID_HARDWARE_BT848,
	fops:     &vbi_fops,
	kernel_ioctl: vbi_ioctl,
	minor:    -1,
};

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
