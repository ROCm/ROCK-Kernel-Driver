/*
    video4linux-parts of the saa7146 device driver
    
    Copyright (C) 1998,1999 Michael Hunold <michael@mihu.de>

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

#include <linux/module.h>	/* for module-version */
#include <linux/string.h>
#include <linux/slab.h>		/* for kmalloc/kfree */
#include <linux/delay.h>	/* for delay-stuff */
#include <asm/uaccess.h>	/* for copy_to/from_user */
#include <linux/wrapper.h>	/* for mem_map_reserve */
#include <linux/vmalloc.h>
#include <linux/videodev.h>

#include "saa7146_defs.h"
#include "saa7146_core.h"
#include "saa7146_v4l.h"


static int saa7146_v4l_debug = 0;

#define dprintk	if (saa7146_v4l_debug) printk
#define hprintk	if (saa7146_v4l_debug >= 2) printk
#define gprintk	if (saa7146_v4l_debug >= 3) printk

#define __COMPILE_SAA7146__
#include "saa7146.c"

/* transform video4linux-cliplist to plain arrays -- we assume that the arrays
   are big enough -- if not: your fault! */	
int	saa7146_v4lclip2plain(struct video_clip *clips, u16 clipcount, int x[], int y[], int width[], int height[])
{
	int	i = 0;
	struct	video_clip* vc = NULL;

	dprintk("saa7146_v4l.o: ==> saa7146_v4lclip2plain, cc:%d\n",clipcount);

	/* anything to do here? */	
	if( 0 == clipcount )
		return 0;

	/* copy to kernel-space */
	vc = vmalloc(sizeof(struct video_clip)*(clipcount));
	if( NULL == vc	) {
		printk("saa7146_v4l.o: ==> v4lclip2saa7146_v4l.o: no memory #2!\n");
		return -ENOMEM;
	}
	if(copy_from_user(vc,clips,sizeof(struct video_clip)*clipcount)) {
		printk("saa7146_v4l.o: ==> v4lclip2saa7146_v4l.o: could not copy from user-space!\n");
		return -EFAULT;
	}

	/* copy the clip-list to the arrays
	   note: the video_clip-struct may contain negative values to indicate that a window
		 doesn't lay completly over the video window. Thus, we correct the values right here */
	for(i = 0; i < clipcount; i++) {

		if( vc[i].width < 0) {
			vc[i].x += vc[i].width; vc[i].width = -vc[i].width;
		}
		if( vc[i].height < 0) {
			vc[i].y += vc[i].height; vc[i].height = -vc[i].height;
		}			

		if( vc[i].x < 0) {
			vc[i].width += vc[i].x; vc[i].x = 0;
		}
		if( vc[i].y < 0) {
			vc[i].height += vc[i].y; vc[i].y = 0;
		}		

		if(vc[i].width <= 0 || vc[i].height <= 0) {
			vfree(vc);
			return -EINVAL;
		}
		
		x[i]		= vc[i].x;
		y[i]		= vc[i].y;
		width[i]	= vc[i].width;
		height[i]	= vc[i].height;
	}		

	/* free memory used for temporary clips */
	vfree(vc);

	return 0;	
}
	
struct saa7146_v4l_struct {
	struct video_buffer	buffer;
	struct video_mbuf	mbuf;
	struct video_window	window;
	struct video_picture	picture;
}; 

static int saa7146_v4l_command(struct saa7146* saa, void *p, unsigned int cmd, void *arg)
{
	struct saa7146_v4l_struct* data = (struct saa7146_v4l_struct*)p;

	hprintk("saa7146_v4l.o: ==> saa7146_v4l_command\n");

	if( NULL == saa)
		return -EINVAL;

	switch(cmd) {
		case SAA7146_V4L_GPICT:
		{
			struct video_picture *p = arg;

			hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_GPICT\n");

			memcpy(p, &data->picture, sizeof(struct video_picture));

		}
		break;

		case SAA7146_V4L_SPICT:
		{
			struct video_picture *p = arg;
			
			hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_SPICT\n");

			memcpy(&data->picture, p, sizeof(struct video_picture));
			set_picture_prop(saa, (u32)(data->picture.brightness>>8),(u32)(data->picture.contrast>>9),(u32)(data->picture.colour>>9));

		}
		break;

		case SAA7146_V4L_SWIN:
		{
			struct video_window *vw = arg;
			int	*x = NULL, *y = NULL, *w = NULL, *h = NULL;

			u32 palette = 0;
			
			hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_SWIN\n");
			
			video_setmode(saa, 0);
			saa7146_write(saa->mem, MC1, (MASK_21));

			set_window(saa, vw->width, vw->height,0,0,0);
			//saa->port, saa->sync);
			if (move_to(saa, vw->x, vw->y, vw->height, data->buffer.width,
				    data->buffer.depth, data->buffer.bytesperline,
				    (u32)data->buffer.base, 0)<0)
			  return -1;

			switch( data->picture.palette ) {

			case VIDEO_PALETTE_RGB555:
				palette = RGB15_COMPOSED;
				break;

			case VIDEO_PALETTE_RGB24:
				palette = RGB24_COMPOSED;
				break;

			case VIDEO_PALETTE_RGB32:
				palette = RGB32_COMPOSED;
				break;

			case VIDEO_PALETTE_UYVY:
				palette = YUV422_COMPOSED;
				break;

			case VIDEO_PALETTE_YUV422P:
				palette = YUV422_DECOMPOSED;
				break;

			case VIDEO_PALETTE_YUV420P:
				palette = YUV420_DECOMPOSED;
				break;

			case VIDEO_PALETTE_YUV411P:
				palette = YUV411_DECOMPOSED;
				break;

			default:
			/*case VIDEO_PALETTE_RGB565:*/
				palette = RGB16_COMPOSED;
				break;
			}

			set_output_format(saa, palette);

			if (vw->flags==VIDEO_CLIP_BITMAP) {
			  clip_windows(saa, SAA7146_CLIPPING_MASK, vw->width, vw->height,
				       (u32 *) vw->clips, 1, 0, 0, 0, 0);
			} else {
			  
			  
			  /* this is tricky, but helps us saving kmalloc/kfree-calls 
			     and boring if/else-constructs ... */
			  x = (int*)kmalloc(sizeof(int)*vw->clipcount*4,GFP_KERNEL);
			  if( NULL == x ) {
			    hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_SWIN: out of kernel-memory.\n");
			    return -ENOMEM;
			  }
			  y = x+(1*vw->clipcount);
			  w = x+(2*vw->clipcount);
			  h = x+(3*vw->clipcount);
			  
			  /* transform clipping-windows */
			  if (0 != saa7146_v4lclip2plain(vw->clips, vw->clipcount,x,y,w,h))
			    break;
			  clip_windows(saa, SAA7146_CLIPPING_RECT, vw->width, vw->height,
				       NULL, vw->clipcount, x, y, w, h);
			  kfree(x);
			  
			  memcpy(&data->window, arg, sizeof(struct video_window));
			}
			video_setmode(saa, 1);
			break;
		}

		case SAA7146_V4L_CCAPTURE:
		{
			int* i = arg;

			hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_CCAPTURE\n");

			if ( 0 == *i ) {
				video_setmode(saa, 0);
			}
			else {
				video_setmode(saa, 1);
			}
						
			break;
		}

		case SAA7146_V4L_GFBUF:
		{
			struct video_buffer *b = arg;

			hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_GFBUF\n");

			memcpy(b, &data->buffer, sizeof(struct video_buffer));

			break;
		}

		case SAA7146_V4L_SFBUF:
		{
			struct video_buffer *b = arg;
		
			memcpy(&data->buffer, b, sizeof(struct video_buffer));
			hprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_SFBUF: b:0x%08x, h:%d, w:%d, d:%d\n", (u32)data->buffer.base, data->buffer.height, data->buffer.width, data->buffer.depth);			

			break;
		}


		case SAA7146_V4L_CSYNC:
		{
			int i = *((int*)arg);

			int count = 0, k = 0;
			unsigned char* grabbfr;
			unsigned char y, uv;

			/* sanity checks */
                        if ( i >= saa->buffers || i < 0) {
				gprintk("saa7146_v4l.o: SAA7146_V4L_CSYNC, invalid buffer %d\n",i);
				return -EINVAL;
			}

			/* get the state */
			switch ( saa->frame_stat[i] ) {
				case GBUFFER_UNUSED:
				{
					/* there was no grab to this buffer */
					gprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_CSYNC, invalid frame (fr:%d)\n",i);
					return -EINVAL;
				}
				case GBUFFER_GRABBING:
				{		
					/* wait to be woken up by the irq-handler */
					interruptible_sleep_on(&saa->rps0_wq);
					break;
				}
				case GBUFFER_DONE:
				{
					gprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_CSYNC, frame done! (fr:%d)\n",i);
					break;
				}
			}

			/* all saa7146´s below chip-revision 3 are not capable of
			   doing byte-swaps with video-dma1. for rgb-grabbing this
			   does not matter, but yuv422-grabbing has the wrong
			   byte-order, so we have to swap in software */
			if ( ( saa->revision<3) && 
			     (saa->grab_format[i] == YUV422_COMPOSED)) {
				/* swap UYVY to YUYV */
				count = saa->grab_height[i]*saa->grab_width[i]*2;
				grabbfr = ((unsigned char*)(saa->grabbing))+i*GRABBING_MEM_SIZE;
				for (k=0; k<count; k=k+2) {
					y = grabbfr[k+1];
					uv = grabbfr[k];
					grabbfr[k] = y;
					grabbfr[k+1] = uv;
				}
			}
						
			/* set corresponding buffer to ´unused´ */
			saa->frame_stat[i] = GBUFFER_UNUSED;

			break;
		}
		case SAA7146_V4L_CMCAPTURE:
		{
                        struct video_mmap *vm = arg;

			gprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_CMCAPTURE, trying buffer:%d\n", vm->frame);

			/* check status for wanted frame */
			if ( GBUFFER_GRABBING == saa->frame_stat[vm->frame] ) {
				gprintk("saa7146_v4l.o: frame #%d still grabbing!\n",vm->frame);
				return -EBUSY;
			}

			/* do necessary transformations from the videodev-structure to our own format. */		

			/* sanity checks */
			if ( vm->width <= 0 || vm->height <= 0 ) {
				gprintk("saa7146_v4l.o: set_up_grabbing, invalid dimension for wanted buffer %d\n",vm->frame);
				return -EINVAL;
			}

			/* set corresponding buffer to ´grabbing´ */
			saa->frame_stat[vm->frame] = GBUFFER_GRABBING;

			/* copy grabbing informtaion for the buffer */
			saa->grab_height[vm->frame] = vm->height;
			saa->grab_width[vm->frame]  = vm->width;
			/* fixme: setting of grabbing port ?!*/
			saa->grab_port[vm->frame]   = 0;

			switch( vm->format ) {

				case VIDEO_PALETTE_RGB555:
					saa->grab_format[vm->frame] = RGB15_COMPOSED;
					break;

				case VIDEO_PALETTE_RGB24:
					saa->grab_format[vm->frame] = RGB24_COMPOSED;
					break;

				case VIDEO_PALETTE_RGB32:
					saa->grab_format[vm->frame] = RGB32_COMPOSED;
					break;

				case VIDEO_PALETTE_YUV420P:
					return -EINVAL;

				case VIDEO_PALETTE_YUV422:
					saa->grab_format[vm->frame] = YUV422_COMPOSED;
					break;

				case VIDEO_PALETTE_YUV422P:
					saa->grab_format[vm->frame] = YUV422_DECOMPOSED;
					break;

				case VIDEO_PALETTE_YUV411P:
					saa->grab_format[vm->frame] = YUV411_DECOMPOSED;
					break;

				default:
					/*case VIDEO_PALETTE_RGB565:*/
					saa->grab_format[vm->frame] = RGB16_COMPOSED;
					break;
				}

			set_up_grabbing(saa,vm->frame);
			break;
		}
		case SAA7146_V4L_GMBUF:
		{
                        struct video_mbuf *m = arg;
			int i = 0;

			m->size = saa->buffers * GRABBING_MEM_SIZE;
			m->frames = saa->buffers;

			for(i = 0; i < saa->buffers; i++)
				m->offsets[i]=(i*GRABBING_MEM_SIZE);

			gprintk(KERN_ERR "saa7146_v4l.o: SAA7146_V4L_GMBUF, providing %d buffers.\n", saa->buffers);
			
			break;
		}

		default:
			return -ENOIOCTLCMD;
	}
	
	return 0;
}

int saa7146_v4l_attach(struct saa7146* adap, void** p)
{
	struct saa7146_v4l_struct* data;

	hprintk("saa7146_v4l.o: ==> saa7146_v4l_inc_use_attach\n");
					
	if (!(data = kmalloc(sizeof(struct saa7146_v4l_struct), GFP_KERNEL))) {
	    	printk (KERN_ERR "%s: out of memory!\n", __FUNCTION__);
		return -ENOMEM;
	}
	*(struct saa7146_v4l_struct**)p = data;

	memset(&data->buffer, 0x0, sizeof(struct video_buffer));
	memset(&data->mbuf,   0x0, sizeof(struct video_mbuf));
	memset(&data->window, 0x0, sizeof(struct video_window));
	memset(&data->picture,0x0, sizeof(struct video_picture));

	data->picture.brightness = 32768;
	data->picture.contrast = 32768;
	data->picture.colour = 32768;	/* saturation */
	data->picture.depth = 16;
	data->picture.palette = VIDEO_PALETTE_RGB565;

	return 0;
}


void saa7146_v4l_inc_use(struct saa7146* adap)
{
	MOD_INC_USE_COUNT;
}


int saa7146_v4l_detach(struct saa7146* adap, void** p)
{
	struct saa7146_v4l_struct** data = (struct saa7146_v4l_struct**)p;

	kfree(*data);
	*data = NULL;

	return 0;
}


void saa7146_v4l_dec_use(struct saa7146* adap)
{
	MOD_DEC_USE_COUNT;
}


static struct saa7146_extension saa7146_v4l_extension = {
	"v4l extension\0",
	MASK_27,                            /* handles rps0 irqs */
	saa7146_std_grab_irq_callback_rps0,
	saa7146_v4l_command,
	saa7146_v4l_attach,
	saa7146_v4l_detach,
	saa7146_v4l_inc_use,
	saa7146_v4l_dec_use
};	


int saa7146_v4l_init (void) 
{
	int res = 0;

	if((res = saa7146_add_extension(&saa7146_v4l_extension))) {
		printk("saa7146_v4l.o: extension registration failed, module not inserted.\n");
		return res;
	}
	
	return 0;
}


void saa7146_v4l_exit (void) 
{
	int res = 0;
	if ((res = saa7146_del_extension(&saa7146_v4l_extension))) {
		printk("saa7146_v4l.o: extension deregistration failed, module not removed.\n");
	}
}

MODULE_PARM(saa7146_v4l_debug, "i");
MODULE_PARM_DESC(saa7146_v4l_debug, "set saa7146_v4l.c in debug mode");

