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

static int vbi_buffer_setup(struct file *file, int *count, int *size)
{
	struct bttv *btv = file->private_data;

	if (0 == *count)
		*count = vbibufs;
	*size = btv->vbi.lines * 2 * 2048;
	return 0;
}

static int vbi_buffer_prepare(struct file *file, struct videobuf_buffer *vb,
			      int fields)
{
	struct bttv *btv = file->private_data;
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;
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
	dprintk("buf prepare %p: odd=%p even=%p\n",
		vb,&buf->odd,&buf->even);
	return 0;

 fail:
	bttv_dma_free(btv,buf);
	return rc;
}

static void
vbi_buffer_queue(struct file *file, struct videobuf_buffer *vb)
{
	struct bttv *btv = file->private_data;
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;
	
	dprintk("queue %p\n",vb);
	buf->vb.state = STATE_QUEUED;
	list_add_tail(&buf->vb.queue,&btv->vcapture);
	bttv_set_dma(btv,0x0c,1);
}

static void vbi_buffer_release(struct file *file, struct videobuf_buffer *vb)
{
	struct bttv *btv = file->private_data;
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;
	
	dprintk("free %p\n",vb);
	bttv_dma_free(btv,buf);
}

struct videobuf_queue_ops vbi_qops = {
	buf_setup:    vbi_buffer_setup,
	buf_prepare:  vbi_buffer_prepare,
	buf_queue:    vbi_buffer_queue,
	buf_release:  vbi_buffer_release,
};

/* ----------------------------------------------------------------------- */

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

	down(&btv->vbi.q.lock);
	if (btv->vbi.users) {
		up(&btv->vbi.q.lock);
		return -EBUSY;
	}
	dprintk("open minor=%d\n",minor);
	file->private_data = btv;
	btv->vbi.users++;
	vbi_setlines(btv,VBI_DEFLINES);
	bttv_field_count(btv);
	
	up(&btv->vbi.q.lock);
	
	return 0;
}

static int vbi_release(struct inode *inode, struct file *file)
{
	struct bttv    *btv = file->private_data;

	if (btv->vbi.q.streaming)
		videobuf_streamoff(file,&btv->vbi.q);
	down(&btv->vbi.q.lock);
	if (btv->vbi.q.reading)
		videobuf_read_stop(file,&btv->vbi.q);
	btv->vbi.users--;
	bttv_field_count(btv);
	vbi_setlines(btv,0);
	up(&btv->vbi.q.lock);
	return 0;
}

static int vbi_do_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *arg)
{
	struct bttv *btv = file->private_data;
	
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

		if (btv->vbi.q.reading || btv->vbi.q.streaming)
			return -EBUSY;
		vbi_setlines(btv,f->fmt.vbi.count[0]);
		vbi_fmt(btv,f);
		return 0;
	}

	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(file,&btv->vbi.q,arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(&btv->vbi.q, arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(file, &btv->vbi.q, arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(file, &btv->vbi.q, arg);

	case VIDIOC_STREAMON:
		return videobuf_streamon(file, &btv->vbi.q);

	case VIDIOC_STREAMOFF:
		return videobuf_streamoff(file, &btv->vbi.q);

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
}

static int vbi_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, vbi_do_ioctl);
}

static ssize_t vbi_read(struct file *file, char *data,
			size_t count, loff_t *ppos)
{
	struct bttv *btv = file->private_data;

	if (btv->errors)
		bttv_reinit_bt848(btv);
	dprintk("read %d\n",count);
	return videobuf_read_stream(file, &btv->vbi.q, data, count, ppos, 1);
}

static unsigned int vbi_poll(struct file *file, poll_table *wait)
{
	struct bttv *btv = file->private_data;

	dprintk("poll%s\n","");
	return videobuf_poll_stream(file, &btv->vbi.q, wait);
}

static int
vbi_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct bttv *btv = file->private_data;
	
	dprintk("mmap 0x%lx+%ld\n",vma->vm_start,
		vma->vm_end - vma->vm_start);
	return videobuf_mmap_mapper(vma, &btv->vbi.q);
}

static struct file_operations vbi_fops =
{
	owner:	  THIS_MODULE,
	open:	  vbi_open,
	release:  vbi_release,
	ioctl:	  vbi_ioctl,
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
	minor:    -1,
};

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
