/*
 *	Video for Linux Two
 *	Backward Compatibility Layer
 *
 *	Support subroutines for providing V4L2 drivers with backward
 *	compatibility with applications using the old API.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Author:	Bill Dirks <bdirks@pacbell.net>
 *		et al.
 *
 */

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/config.h>

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/videodev.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

static unsigned int debug  = 0;
MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug,"enable debug messages");
MODULE_AUTHOR("Bill Dirks");
MODULE_DESCRIPTION("v4l(1) compatibility layer for v4l2 drivers.");
MODULE_LICENSE("GPL");

#define dprintk(fmt, arg...)	if (debug) \
	printk(KERN_DEBUG "v4l1-compat: " fmt, ## arg)

/*
 *	I O C T L   T R A N S L A T I O N
 *
 *	From here on down is the code for translating the numerous
 *	ioctl commands from the old API to the new API.
 */

static int
get_v4l_control(struct inode            *inode,
		struct file             *file,
		int			cid,
		v4l2_kioctl             drv)
{
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_control	ctrl2;
	int			err;

	qctrl2.id = cid;
	err = drv(inode, file, VIDIOC_QUERYCTRL, &qctrl2);
	if (err < 0)
		dprintk("VIDIOC_QUERYCTRL: %d\n",err);
	if (err == 0 &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED))
	{
		ctrl2.id = qctrl2.id;
		err = drv(inode, file, VIDIOC_G_CTRL, &ctrl2);
		if (err < 0)
			dprintk("VIDIOC_G_CTRL: %d\n",err);
		return ((ctrl2.value - qctrl2.minimum) * 65535
			 + (qctrl2.maximum - qctrl2.minimum) / 2)
			/ (qctrl2.maximum - qctrl2.minimum);
	}
	return 0;
}

static int
set_v4l_control(struct inode            *inode,
		struct file             *file,
		int			cid,
		int			value,
		v4l2_kioctl             drv)
{
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_control	ctrl2;
	int			err;

	qctrl2.id = cid;
	err = drv(inode, file, VIDIOC_QUERYCTRL, &qctrl2);
	if (err < 0)
		dprintk("VIDIOC_QUERYCTRL: %d\n",err);
	if (err == 0 &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED) &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_GRABBED))
	{
		if (value < 0)
			value = 0;
		if (value > 65535)
			value = 65535;
		if (value && qctrl2.type == V4L2_CTRL_TYPE_BOOLEAN)
			value = 65535;
		ctrl2.id = qctrl2.id;
		ctrl2.value = 
			(value * (qctrl2.maximum - qctrl2.minimum)
			 + 32767)
			/ 65535;
		ctrl2.value += qctrl2.minimum;
		err = drv(inode, file, VIDIOC_S_CTRL, &ctrl2);
		if (err < 0)
			dprintk("VIDIOC_S_CTRL: %d\n",err);
	}
	return 0;
}

static int palette2pixelformat[] = {
	[VIDEO_PALETTE_GREY]    = V4L2_PIX_FMT_GREY,
	[VIDEO_PALETTE_RGB555]  = V4L2_PIX_FMT_RGB555,
	[VIDEO_PALETTE_RGB565]  = V4L2_PIX_FMT_RGB565,
	[VIDEO_PALETTE_RGB24]   = V4L2_PIX_FMT_BGR24,
	[VIDEO_PALETTE_RGB32]   = V4L2_PIX_FMT_BGR32,
	/* yuv packed pixel */
	[VIDEO_PALETTE_YUYV]    = V4L2_PIX_FMT_YUYV,
	[VIDEO_PALETTE_YUV422]  = V4L2_PIX_FMT_YUYV,
	[VIDEO_PALETTE_UYVY]    = V4L2_PIX_FMT_UYVY,
	/* yuv planar */
	[VIDEO_PALETTE_YUV410P] = V4L2_PIX_FMT_YUV410,
	[VIDEO_PALETTE_YUV420]  = V4L2_PIX_FMT_YUV420,
	[VIDEO_PALETTE_YUV420P] = V4L2_PIX_FMT_YUV420,
	[VIDEO_PALETTE_YUV411P] = V4L2_PIX_FMT_YUV411P,
	[VIDEO_PALETTE_YUV422P] = V4L2_PIX_FMT_YUV422P,
};

static int
palette_to_pixelformat(int palette)
{
	if (palette < sizeof(palette2pixelformat)/sizeof(int))
		return palette2pixelformat[palette];
	else
		return 0;
}

static int
pixelformat_to_palette(int pixelformat)
{
	int	palette = 0;
	switch (pixelformat)
	{
	case V4L2_PIX_FMT_GREY:
		palette = VIDEO_PALETTE_GREY;
		break;
	case V4L2_PIX_FMT_RGB555:
		palette = VIDEO_PALETTE_RGB555;
		break;
	case V4L2_PIX_FMT_RGB565:
		palette = VIDEO_PALETTE_RGB565;
		break;
	case V4L2_PIX_FMT_BGR24:
		palette = VIDEO_PALETTE_RGB24;
		break;
	case V4L2_PIX_FMT_BGR32:
		palette = VIDEO_PALETTE_RGB32;
		break;
	/* yuv packed pixel */
	case V4L2_PIX_FMT_YUYV:
		palette = VIDEO_PALETTE_YUYV;
		break;
	case V4L2_PIX_FMT_UYVY:
		palette = VIDEO_PALETTE_UYVY;
		break;
	/* yuv planar */
	case V4L2_PIX_FMT_YUV410:
		palette = VIDEO_PALETTE_YUV420;
		break;
	case V4L2_PIX_FMT_YUV420:
		palette = VIDEO_PALETTE_YUV420;
		break;
	case V4L2_PIX_FMT_YUV411P:
		palette = VIDEO_PALETTE_YUV411P;
		break;
	case V4L2_PIX_FMT_YUV422P:
		palette = VIDEO_PALETTE_YUV422P;
		break;
	}
	return palette;
}

/*  Do an 'in' (wait for input) select on a single file descriptor  */
/*  This stuff plaigarized from linux/fs/select.c     */
#define __FD_IN(fds, n)	(fds->in + n)
#define BIT(i)		(1UL << ((i)&(__NFDBITS-1)))
#define SET(i,m)	(*(m) |= (i))
extern int do_select(int n, fd_set_bits *fds, long *timeout);


static int
simple_select(struct file *file)
{
	fd_set_bits fds;
	char *bits;
	long timeout;
	int i, fd, n, ret, size;

	for (i = 0; i < current->files->max_fds; ++i)
		if (file == current->files->fd[i])
			break;
	if (i == current->files->max_fds)
		return -EINVAL;
	fd = i;
	n = fd + 1;

	timeout = MAX_SCHEDULE_TIMEOUT;
	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = kmalloc(6 * size, GFP_KERNEL);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	/*  All zero except our one file descriptor bit, for input  */
	memset(bits, 0, 6 * size);
	SET(BIT(fd), __FD_IN((&fds), fd / __NFDBITS));

	ret = do_select(n, &fds, &timeout);

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}
out:
	kfree(bits);
out_nofds:
	return ret;
}

static int count_inputs(struct inode         *inode,
			struct file          *file,
			v4l2_kioctl          drv)
{
	struct v4l2_input input2;
	int i;

	for (i = 0;; i++) {
		memset(&input2,0,sizeof(input2));
		input2.index = i;
		if (0 != drv(inode,file,VIDIOC_ENUMINPUT, &input2))
			break;
	}
	return i;
}

static int check_size(struct inode         *inode,
		      struct file          *file,
		      v4l2_kioctl          drv,
		      int *maxw, int *maxh)
{
	struct v4l2_fmtdesc desc2;
	struct v4l2_format  fmt2;

	memset(&desc2,0,sizeof(desc2));
	memset(&fmt2,0,sizeof(fmt2));
	
	desc2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 != drv(inode,file,VIDIOC_ENUM_FMT, &desc2))
		goto done;

	fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt2.fmt.pix.width       = 10000;
	fmt2.fmt.pix.height      = 10000;
	fmt2.fmt.pix.pixelformat = desc2.pixelformat;
	if (0 != drv(inode,file,VIDIOC_TRY_FMT, &fmt2))
		goto done;

	*maxw = fmt2.fmt.pix.width;
	*maxh = fmt2.fmt.pix.height;

 done:
	return 0;
}


/*
 *	This function is exported.
 */
int
v4l_compat_translate_ioctl(struct inode         *inode,
			   struct file		*file,
			   int			cmd,
			   void			*arg,
			   v4l2_kioctl          drv)
{
	int	             err = -ENOIOCTLCMD;

	switch (cmd)
	{
	case VIDIOCGCAP:	/* capability */
	{
		struct video_capability *cap = arg;
		struct v4l2_capability cap2;
		struct v4l2_framebuffer fbuf2;
		
		memset(cap, 0, sizeof(*cap));
		memset(&cap2, 0, sizeof(cap2));
		memset(&fbuf2, 0, sizeof(fbuf2));

		err = drv(inode, file, VIDIOC_QUERYCAP, &cap2);
		if (err < 0) {
			dprintk("VIDIOCGCAP / VIDIOC_QUERYCAP: %d\n",err);
			break;
		}
		if (cap2.capabilities & V4L2_CAP_VIDEO_OVERLAY) {
			err = drv(inode, file, VIDIOC_G_FBUF, &fbuf2);
			if (err < 0) {
				dprintk("VIDIOCGCAP / VIDIOC_G_FBUF: %d\n",err);
				memset(&fbuf2, 0, sizeof(fbuf2));
			}
			err = 0;
		}

		memcpy(cap->name, cap2.card, 
		       min(sizeof(cap->name), sizeof(cap2.card)));
		cap->name[sizeof(cap->name) - 1] = 0;
		if (cap2.capabilities & V4L2_CAP_VIDEO_CAPTURE)
			cap->type |= VID_TYPE_CAPTURE;
		if (cap2.capabilities & V4L2_CAP_TUNER)
			cap->type |= VID_TYPE_TUNER;
		if (cap2.capabilities & V4L2_CAP_VBI_CAPTURE)
			cap->type |= VID_TYPE_TELETEXT;
		if (cap2.capabilities & V4L2_CAP_VIDEO_OVERLAY)
			cap->type |= VID_TYPE_OVERLAY;
		if (fbuf2.capability & V4L2_FBUF_CAP_LIST_CLIPPING)
			cap->type |= VID_TYPE_CLIPPING;

		cap->channels  = count_inputs(inode,file,drv);
		check_size(inode,file,drv,
			   &cap->maxwidth,&cap->maxheight);
		cap->audios    =  0; /* FIXME */
		cap->minwidth  = 48; /* FIXME */
		cap->minheight = 32; /* FIXME */
		break;
	}
	case VIDIOCGFBUF: /*  get frame buffer  */
	{
		struct video_buffer	*buffer = arg;
		struct v4l2_framebuffer	fbuf2;

		err = drv(inode, file, VIDIOC_G_FBUF, &fbuf2);
		if (err < 0) {
			dprintk("VIDIOCGFBUF / VIDIOC_G_FBUF: %d\n",err);
			break;
		}
		buffer->base   = fbuf2.base;
		buffer->height = fbuf2.fmt.height;
		buffer->width  = fbuf2.fmt.width;

		switch (fbuf2.fmt.pixelformat) {
		case V4L2_PIX_FMT_RGB332:
			buffer->depth = 8;
				break;
		case V4L2_PIX_FMT_RGB555:
			buffer->depth = 15;
			break;
		case V4L2_PIX_FMT_RGB565:
			buffer->depth = 16;
			break;
		case V4L2_PIX_FMT_BGR24:
			buffer->depth = 24;
			break;
		case V4L2_PIX_FMT_BGR32:
			buffer->depth = 32;
			break;
		default:
			buffer->depth = 0;
		}
		if (0 != fbuf2.fmt.bytesperline)
			buffer->bytesperline = fbuf2.fmt.bytesperline;
		else {
			buffer->bytesperline = 
				(buffer->width * buffer->depth + 7) & 7;
			buffer->bytesperline >>= 3;
		}
		break;
	}
	case VIDIOCSFBUF: /*  set frame buffer  */
	{
		struct video_buffer	*buffer = arg;
		struct v4l2_framebuffer	fbuf2;

		memset(&fbuf2, 0, sizeof(fbuf2));
		fbuf2.base       = buffer->base;
		fbuf2.fmt.height = buffer->height;
		fbuf2.fmt.width  = buffer->width;
		switch (buffer->depth) {
		case 8:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_RGB332;
			break;
		case 15:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_RGB555;
			break;
		case 16:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_RGB565;
			break;
		case 24:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_BGR24;
			break;
		case 32:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_BGR32;
			break;
		}
		fbuf2.fmt.bytesperline = buffer->bytesperline;
		err = drv(inode, file, VIDIOC_S_FBUF, &fbuf2);
		if (err < 0)
			dprintk("VIDIOCSFBUF / VIDIOC_S_FBUF: %d\n",err);
		break;
	}
	case VIDIOCGWIN: /*  get window or capture dimensions  */
	{
		struct video_window	*win = arg;
		struct v4l2_format	fmt2;

		memset(win,0,sizeof(*win));
		memset(&fmt2,0,sizeof(fmt2));

		fmt2.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
		err = drv(inode, file, VIDIOC_G_FMT, &fmt2);
		if (err < 0)
			dprintk("VIDIOCGWIN / VIDIOC_G_WIN: %d\n",err);
		if (err == 0) {
			win->x         = fmt2.fmt.win.w.left;
			win->y         = fmt2.fmt.win.w.top;
			win->width     = fmt2.fmt.win.w.width;
			win->height    = fmt2.fmt.win.w.height;
			win->chromakey = fmt2.fmt.win.chromakey;
			win->clips     = NULL;
			win->clipcount = 0;
			break;
		}

		fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, &fmt2);
		if (err < 0) {
			dprintk("VIDIOCGWIN / VIDIOC_G_FMT: %d\n",err);
			break;
		}
		win->x         = 0;
		win->y         = 0;
		win->width     = fmt2.fmt.pix.width;
		win->height    = fmt2.fmt.pix.height;
		win->chromakey = 0;
		win->clips     = NULL;
		win->clipcount = 0;
		break;
	}
	case VIDIOCSWIN: /*  set window and/or capture dimensions  */
	{
		struct video_window	*win = arg;
		struct v4l2_format	fmt2;

		memset(&fmt2,0,sizeof(fmt2));
		fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, &fmt2);
		if (err < 0)
			dprintk("VIDIOCSWIN / VIDIOC_G_FMT: %d\n",err);
		if (err == 0) {
			fmt2.fmt.pix.width  = win->width;
			fmt2.fmt.pix.height = win->height;
			fmt2.fmt.pix.field  = V4L2_FIELD_ANY;
			err = drv(inode, file, VIDIOC_S_FMT, &fmt2);
			if (err < 0)
				dprintk("VIDIOCSWIN / VIDIOC_S_FMT #1: %d\n",
					err);
			win->width  = fmt2.fmt.pix.width;
			win->height = fmt2.fmt.pix.height;
		}

		memset(&fmt2,0,sizeof(fmt2));
		fmt2.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
		fmt2.fmt.win.w.left    = win->x;
		fmt2.fmt.win.w.top     = win->y;
		fmt2.fmt.win.w.width   = win->width;
		fmt2.fmt.win.w.height  = win->height;
		fmt2.fmt.win.chromakey = win->chromakey;
		fmt2.fmt.win.clips     = (void *)win->clips;
		fmt2.fmt.win.clipcount = win->clipcount;
		err = drv(inode, file, VIDIOC_S_FMT, &fmt2);
		if (err < 0)
			dprintk("VIDIOCSWIN / VIDIOC_S_FMT #2: %d\n",err);
		break;
	}
	case VIDIOCCAPTURE: /*  turn on/off preview  */
	{
		err = drv(inode, file, VIDIOC_OVERLAY, arg);
		if (err < 0)
			dprintk("VIDIOCCAPTURE / VIDIOC_PREVIEW: %d\n",err);
		break;
	}
	case VIDIOCGCHAN: /*  get input information  */
	{
		struct video_channel	*chan = arg;
		struct v4l2_input	input2;
		v4l2_std_id    		sid;

		memset(&input2,0,sizeof(input2));
		input2.index = chan->channel;
		err = drv(inode, file, VIDIOC_ENUMINPUT, &input2);
		if (err < 0) {
			dprintk("VIDIOCGCHAN / VIDIOC_ENUMINPUT: "
				"channel=%d err=%d\n",chan->channel,err);
			break;
		}
		chan->channel = input2.index;
		memcpy(chan->name, input2.name,
		       min(sizeof(chan->name), sizeof(input2.name)));
		chan->name[sizeof(chan->name) - 1] = 0;
		chan->tuners = (input2.type == V4L2_INPUT_TYPE_TUNER) ? 1 : 0;
		chan->flags = (chan->tuners) ? VIDEO_VC_TUNER : 0;
		switch (input2.type) {
		case V4L2_INPUT_TYPE_TUNER:
			chan->type = VIDEO_TYPE_TV;
			break;
		default:
		case V4L2_INPUT_TYPE_CAMERA:
			chan->type = VIDEO_TYPE_CAMERA;
			break;
		}
		chan->norm = 0;
		err = drv(inode, file, VIDIOC_G_STD, &sid);
		if (err < 0)
			dprintk("VIDIOCGCHAN / VIDIOC_G_STD: %d\n",err);
		if (err == 0) {
			if (sid & V4L2_STD_PAL)
				chan->norm = VIDEO_MODE_PAL;
			if (sid & V4L2_STD_NTSC)
				chan->norm = VIDEO_MODE_NTSC;
			if (sid & V4L2_STD_SECAM)
				chan->norm = VIDEO_MODE_SECAM;
		}
		break;
	}
	case VIDIOCSCHAN: /*  set input  */
	{
		struct video_channel *chan = arg;
		
		err = drv(inode, file, VIDIOC_S_INPUT, &chan->channel);
		if (err < 0)
			dprintk("VIDIOCSCHAN / VIDIOC_S_INPUT: %d\n",err);
		break;
	}
	case VIDIOCGPICT: /*  get tone controls & partial capture format  */
	{
		struct video_picture	*pict = arg;
		struct v4l2_format	fmt2;

		pict->brightness = get_v4l_control(inode, file,
						   V4L2_CID_BRIGHTNESS,drv);
		pict->hue = get_v4l_control(inode, file,
					    V4L2_CID_HUE, drv);
		pict->contrast = get_v4l_control(inode, file,
						 V4L2_CID_CONTRAST, drv);
		pict->colour = get_v4l_control(inode, file,
					       V4L2_CID_SATURATION, drv);
		pict->whiteness = get_v4l_control(inode, file,
						  V4L2_CID_WHITENESS, drv);

		memset(&fmt2,0,sizeof(fmt2));
		fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, &fmt2);
		if (err < 0) {
			dprintk("VIDIOCGPICT / VIDIOC_G_FMT: %d\n",err);
			break;
		}
#if 0 /* FIXME */
		pict->depth   = fmt2.fmt.pix.depth;
#endif
		pict->palette = pixelformat_to_palette(
			fmt2.fmt.pix.pixelformat);
		break;
	}
	case VIDIOCSPICT: /*  set tone controls & partial capture format  */
	{
		struct video_picture	*pict = arg;
		struct v4l2_format	fmt2;
		struct v4l2_framebuffer	fbuf2;

		set_v4l_control(inode, file,
				V4L2_CID_BRIGHTNESS, pict->brightness, drv);
		set_v4l_control(inode, file,
				V4L2_CID_HUE, pict->hue, drv);
		set_v4l_control(inode, file,
				V4L2_CID_CONTRAST, pict->contrast, drv);
		set_v4l_control(inode, file,
				V4L2_CID_SATURATION, pict->colour, drv);
		set_v4l_control(inode, file,
				V4L2_CID_WHITENESS, pict->whiteness, drv);

		memset(&fmt2,0,sizeof(fmt2));
		fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, &fmt2);
		if (err < 0)
			dprintk("VIDIOCSPICT / VIDIOC_G_FMT: %d\n",err);
		if (fmt2.fmt.pix.pixelformat != 
		    palette_to_pixelformat(pict->palette)) {
			fmt2.fmt.pix.pixelformat = palette_to_pixelformat(
				pict->palette);
			err = drv(inode, file, VIDIOC_S_FMT, &fmt2);
			if (err < 0)
				dprintk("VIDIOCSPICT / VIDIOC_S_FMT: %d\n",err);
		}

		err = drv(inode, file, VIDIOC_G_FBUF, &fbuf2);
		if (err < 0)
			dprintk("VIDIOCSPICT / VIDIOC_G_FBUF: %d\n",err);
		if (fbuf2.fmt.pixelformat !=
		    palette_to_pixelformat(pict->palette)) {
			fbuf2.fmt.pixelformat = palette_to_pixelformat(
				pict->palette);
			err = drv(inode, file, VIDIOC_S_FBUF, &fbuf2);
			if (err < 0)
				dprintk("VIDIOCSPICT / VIDIOC_S_FBUF: %d\n",err);
			err = 0; /* likely fails for non-root */
		}
		break;
	}
	case VIDIOCGTUNER: /*  get tuner information  */
	{
		struct video_tuner	*tun = arg;
		struct v4l2_tuner	tun2;
		v4l2_std_id    		sid;

		memset(&tun2,0,sizeof(tun2));
		err = drv(inode, file, VIDIOC_G_TUNER, &tun2);
		if (err < 0) {
			dprintk("VIDIOCGTUNER / VIDIOC_G_TUNER: %d\n",err);
			break;
		}
		memcpy(tun->name, tun2.name,
		       min(sizeof(tun->name), sizeof(tun2.name)));
		tun->name[sizeof(tun->name) - 1] = 0;
		tun->rangelow = tun2.rangelow;
		tun->rangehigh = tun2.rangehigh;
		tun->flags = 0;
		tun->mode = VIDEO_MODE_AUTO;

		err = drv(inode, file, VIDIOC_G_STD, &sid);
		if (err < 0)
			dprintk("VIDIOCGTUNER / VIDIOC_G_STD: %d\n",err);
		if (err == 0) {
			if (sid & V4L2_STD_PAL)
				tun->mode = VIDEO_MODE_PAL;
			if (sid & V4L2_STD_NTSC)
				tun->mode = VIDEO_MODE_NTSC;
			if (sid & V4L2_STD_SECAM)
				tun->mode = VIDEO_MODE_SECAM;
		}

		if (tun2.capability & V4L2_TUNER_CAP_LOW)
			tun->flags |= VIDEO_TUNER_LOW;
		if (tun2.rxsubchans & V4L2_TUNER_SUB_STEREO)
			tun->flags |= VIDEO_TUNER_STEREO_ON;
		tun->signal = tun2.signal;
		break;
	}
#if 0 /* FIXME */
	case VIDIOCSTUNER: /*  select a tuner input  */
	{
		int	i;

		err = drv(inode, file, VIDIOC_S_INPUT, &i);
		if (err < 0)
			dprintk("VIDIOCSTUNER / VIDIOC_S_INPUT: %d\n",err);
		break;
	}
#endif
	case VIDIOCGFREQ: /*  get frequency  */
	{
		int *freq = arg;
		struct v4l2_frequency freq2;
		
		err = drv(inode, file, VIDIOC_G_FREQUENCY, &freq2);
		if (err < 0)
			dprintk("VIDIOCGFREQ / VIDIOC_G_FREQUENCY: %d\n",err);
		if (0 == err)
			*freq = freq2.frequency;
		break;
	}
	case VIDIOCSFREQ: /*  set frequency  */
	{
		int *freq = arg;
		struct v4l2_frequency freq2;

		drv(inode, file, VIDIOC_G_FREQUENCY, &freq2);
		freq2.frequency = *freq;
		err = drv(inode, file, VIDIOC_S_FREQUENCY, &freq2);
		if (err < 0)
			dprintk("VIDIOCSFREQ / VIDIOC_S_FREQUENCY: %d\n",err);
		break;
	}
	case VIDIOCGAUDIO: /*  get audio properties/controls  */
	{
		struct video_audio	*aud = arg;
		struct v4l2_audio	aud2;
		struct v4l2_queryctrl	qctrl2;
		struct v4l2_tuner	tun2;
		int			v;

		err = drv(inode, file, VIDIOC_G_AUDIO, &aud2);
		if (err < 0) {
			dprintk("VIDIOCGAUDIO / VIDIOC_G_AUDIO: %d\n",err);
			break;
		}
		memcpy(aud->name, aud2.name,
		       min(sizeof(aud->name), sizeof(aud2.name)));
		aud->name[sizeof(aud->name) - 1] = 0;
		aud->audio = aud2.index;
		aud->flags = 0;
		v = get_v4l_control(inode, file, V4L2_CID_AUDIO_VOLUME, drv);
		if (v >= 0)
		{
			aud->volume = v;
			aud->flags |= VIDEO_AUDIO_VOLUME;
		}
		v = get_v4l_control(inode, file, V4L2_CID_AUDIO_BASS, drv);
		if (v >= 0)
		{
			aud->bass = v;
			aud->flags |= VIDEO_AUDIO_BASS;
		}
		v = get_v4l_control(inode, file, V4L2_CID_AUDIO_TREBLE, drv);
		if (v >= 0)
		{
			aud->treble = v;
			aud->flags |= VIDEO_AUDIO_TREBLE;
		}
		v = get_v4l_control(inode, file, V4L2_CID_AUDIO_BALANCE, drv);
		if (v >= 0)
		{
			aud->balance = v;
			aud->flags |= VIDEO_AUDIO_BALANCE;
		}
		v = get_v4l_control(inode, file, V4L2_CID_AUDIO_MUTE, drv);
		if (v >= 0)
		{
			if (v)
				aud->flags |= VIDEO_AUDIO_MUTE;
			aud->flags |= VIDEO_AUDIO_MUTABLE;
		}
		aud->step = 1;
		qctrl2.id = V4L2_CID_AUDIO_VOLUME;
		if (drv(inode, file, VIDIOC_QUERYCTRL, &qctrl2) == 0 &&
		    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED))
			aud->step = qctrl2.step;
		aud->mode = 0;
		err = drv(inode, file, VIDIOC_G_TUNER, &tun2);
		if (err < 0) {
			dprintk("VIDIOCGAUDIO / VIDIOC_G_TUNER: %d\n",err);
			err = 0;
			break;
		}
		if (tun2.rxsubchans & V4L2_TUNER_SUB_LANG2)
			aud->mode = VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
		else if (tun2.rxsubchans & V4L2_TUNER_SUB_STEREO)
			aud->mode = VIDEO_SOUND_STEREO;
		else if (tun2.rxsubchans & V4L2_TUNER_SUB_MONO)
			aud->mode = VIDEO_SOUND_MONO;
		break;
	}
	case VIDIOCSAUDIO: /*  set audio controls  */
	{
		struct video_audio	*aud = arg;
		struct v4l2_audio	aud2;
		struct v4l2_tuner	tun2;

		memset(&aud2,0,sizeof(aud2));
		memset(&tun2,0,sizeof(tun2));
		
		aud2.index = aud->audio;
		err = drv(inode, file, VIDIOC_S_AUDIO, &aud2);
		if (err < 0) {
			dprintk("VIDIOCSAUDIO / VIDIOC_S_AUDIO: %d\n",err);
			break;
		}

		set_v4l_control(inode, file, V4L2_CID_AUDIO_VOLUME, 
				aud->volume, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_BASS,
				aud->bass, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_TREBLE,
				aud->treble, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_BALANCE,
				aud->balance, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_MUTE,
				!!(aud->flags & VIDEO_AUDIO_MUTE), drv);

		err = drv(inode, file, VIDIOC_G_TUNER, &tun2);
		if (err < 0)
			dprintk("VIDIOCSAUDIO / VIDIOC_G_TUNER: %d\n",err);
		if (err == 0) {
			switch (aud->mode) {
			default:
			case VIDEO_SOUND_MONO:
			case VIDEO_SOUND_LANG1:
				tun2.audmode = V4L2_TUNER_MODE_MONO;
				break;
			case VIDEO_SOUND_STEREO:
				tun2.audmode = V4L2_TUNER_MODE_STEREO;
				break;
			case VIDEO_SOUND_LANG2:
				tun2.audmode = V4L2_TUNER_MODE_LANG2;
				break;
			}
			err = drv(inode, file, VIDIOC_S_TUNER, &tun2);
			if (err < 0)
				dprintk("VIDIOCSAUDIO / VIDIOC_S_TUNER: %d\n",err);
		}
		err = 0;
		break;
	}
#if 0
	case VIDIOCGMBUF: /*  get mmap parameters  */
	{
		struct video_mbuf		*mbuf = arg;
		struct v4l2_requestbuffers	reqbuf2;
		struct v4l2_buffer		buf2;
		struct v4l2_format		fmt2, fmt2o;
		struct v4l2_capability		cap2;
		int				i;

		/*  Set the format to maximum dimensions  */
		if ((err = drv(inode, file, VIDIOC_QUERYCAP, &cap2)) < 0)
			break;
		fmt2o.type = V4L2_BUF_TYPE_CAPTURE;
		if ((err = drv(inode, file, VIDIOC_G_FMT, &fmt2o)) < 0)
			break;
		fmt2 = fmt2o;
		fmt2.fmt.pix.width = cap2.maxwidth;
		fmt2.fmt.pix.height = cap2.maxheight;
		fmt2.fmt.pix.flags |= V4L2_FMT_FLAG_INTERLACED;
		if ((err = drv(inode, file, VIDIOC_S_FMT, &fmt2)) < 0)
			break;
		reqbuf2.count = 2; /* v4l always used two buffers */
		reqbuf2.type = V4L2_BUF_TYPE_CAPTURE | V4L2_BUF_REQ_CONTIG;
		err = drv(inode, file, VIDIOC_REQBUFS, &reqbuf2);
		if (err < 0 || reqbuf2.count < 2 || reqbuf2.type
		    != (V4L2_BUF_TYPE_CAPTURE | V4L2_BUF_REQ_CONTIG))
		{/*	Driver doesn't support v4l back-compatibility  */
			fmt2o.fmt.pix.flags |= V4L2_FMT_FLAG_INTERLACED;
			drv(inode, file, VIDIOC_S_FMT, &fmt2o);
			reqbuf2.count = 1;
			reqbuf2.type = V4L2_BUF_TYPE_CAPTURE;
			err = drv(inode, file, VIDIOC_REQBUFS, &reqbuf2);
			if (err < 0)
			{
				err = -EINVAL;
				break;
			}
			printk(KERN_INFO"V4L2: Device \"%s\" doesn't support"
			       " v4l memory mapping\n", vfl->name);
		}
		buf2.index = 0;
		buf2.type = V4L2_BUF_TYPE_CAPTURE;
		err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
		mbuf->size = buf2.length * reqbuf2.count;
		mbuf->frames = reqbuf2.count;
		memset(mbuf->offsets, 0, sizeof(mbuf->offsets));
		for (i = 0; i < mbuf->frames; ++i)
			mbuf->offsets[i] = i * buf2.length;
		break;
	}
#endif
	case VIDIOCMCAPTURE: /*  capture a frame  */
	{
		struct video_mmap	*mm = arg;
		struct v4l2_buffer	buf2;
		struct v4l2_format	fmt2;

		memset(&buf2,0,sizeof(buf2));
		memset(&fmt2,0,sizeof(fmt2));
		
		fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, &fmt2);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_G_FMT: %d\n",err);
			break;
		}
		if (mm->width   != fmt2.fmt.pix.width || 
		    mm->height != fmt2.fmt.pix.height ||
		    palette_to_pixelformat(mm->format) != 
		    fmt2.fmt.pix.pixelformat)
		{/* New capture format...  */
			fmt2.fmt.pix.width = mm->width;
			fmt2.fmt.pix.height = mm->height;
			fmt2.fmt.pix.pixelformat =
				palette_to_pixelformat(mm->format);
			fmt2.fmt.pix.field = V4L2_FIELD_ANY;
			err = drv(inode, file, VIDIOC_S_FMT, &fmt2);
			if (err < 0) {
				dprintk("VIDIOCMCAPTURE / VIDIOC_S_FMT: %d\n",err);
				break;
			}
		}
		buf2.index = mm->frame;
		buf2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_QUERYBUF: %d\n",err);
			break;
		}
		err = drv(inode, file, VIDIOC_QBUF, &buf2);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_QBUF: %d\n",err);
			break;
		}
		err = drv(inode, file, VIDIOC_STREAMON, &buf2.type);
		if (err < 0)
			dprintk("VIDIOCMCAPTURE / VIDIOC_STREAMON: %d\n",err);
		break;
	}
	case VIDIOCSYNC: /*  wait for a frame  */
	{
		int			*i = arg;
		struct v4l2_buffer	buf2;

		buf2.index = *i;
		buf2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
		if (err < 0) {
			/*  No such buffer */
			dprintk("VIDIOCSYNC / VIDIOC_QUERYBUF: %d\n",err);
			break;
		}
		if (!(buf2.flags & V4L2_BUF_FLAG_MAPPED)) {
			/* Buffer is not mapped  */
			err = -EINVAL;
			break;
		}

		/*  Loop as long as the buffer is queued, but not done  */
		while ((buf2.flags &
			(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))
		       == V4L2_BUF_FLAG_QUEUED)
		{
			err = simple_select(file);
			if (err < 0 ||	/* error or sleep was interrupted  */
			    err == 0)	/* timeout? Shouldn't occur.  */
				break;
			err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
			if (err < 0)
				dprintk("VIDIOCSYNC / VIDIOC_QUERYBUF: %d\n",err);
		}
		if (!(buf2.flags & V4L2_BUF_FLAG_DONE)) /* not done */
			break;
		do {
			err = drv(inode, file, VIDIOC_DQBUF, &buf2);
			if (err < 0)
				dprintk("VIDIOCSYNC / VIDIOC_DQBUF: %d\n",err);
		} while (err == 0 && buf2.index != *i);
		break;
	}
	case VIDIOCGUNIT: /*  get related device minors  */
		/*  No translation  */
		break;
	case VIDIOCGCAPTURE: /*    */
		/*  No translation, yet...  */
		printk(KERN_INFO"v4l1-compat: VIDIOCGCAPTURE not implemented."
		       " Send patches to bdirks@pacbell.net :-)\n");
		break;
	case VIDIOCSCAPTURE: /*    */
		/*  No translation, yet...  */
		printk(KERN_INFO"v4l1-compat: VIDIOCSCAPTURE not implemented."
		       " Send patches to bdirks@pacbell.net :-)\n");
		break;
	}
	return err;
}

EXPORT_SYMBOL(v4l_compat_translate_ioctl);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
