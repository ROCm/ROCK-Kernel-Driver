/* Linux driver for Philips webcam 
   USB and Video4Linux interface part.
   (C) 1999-2001 Nemosoft Unv.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*  
   This code forms the interface between the USB layers and the Philips
   specific stuff. Some adanved stuff of the driver falls under an
   NDA, signed between me and Philips B.V., Eindhoven, the Netherlands, and
   is thus not distributed in source form. The binary pwcx.o module 
   contains the code that falls under the NDA.
   
   In case you're wondering: 'pwc' stands for "Philips WebCam", but 
   I really didn't want to type 'philips_web_cam' every time (I'm lazy as
   any Linux kernel hacker, but I don't like uncomprehensible abbreviations
   without explanation).
   
   Oh yes, convention: to disctinguish between all the various pointers to
   device-structures, I use these names for the pointer variables:
   udev: struct usb_device *
   vdev: struct video_device *
   pdev: struct pwc_devive *
*/

/* Contributors:
   - Alvarado: adding whitebalance code
   - Alistar Moire: QuickCam 3000 Pro testing
*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wrapper.h>
#include <asm/io.h>

#include "pwc.h"
#include "pwc-ioctl.h"
#include "pwc-uncompress.h"

#if !defined(MAP_NR)
#define MAP_NR(a) virt_to_page(a)
#endif

/* Function prototypes and driver templates */

/* hotplug device table support */
static __devinitdata struct usb_device_id pwc_device_table [] = {
	{ USB_DEVICE(0x0471, 0x0302) },
	{ USB_DEVICE(0x0471, 0x0303) },
	{ USB_DEVICE(0x0471, 0x0304) },
	{ USB_DEVICE(0x0471, 0x0307) },
	{ USB_DEVICE(0x0471, 0x0308) },
	{ USB_DEVICE(0x0471, 0x030C) },
	{ USB_DEVICE(0x0471, 0x0310) },
	{ USB_DEVICE(0x0471, 0x0311) },
	{ USB_DEVICE(0x0471, 0x0312) },
	{ USB_DEVICE(0x069A, 0x0001) },
	{ USB_DEVICE(0x046D, 0x08b0) },
	{ USB_DEVICE(0x055D, 0x9000) },
	{ USB_DEVICE(0x055D, 0x9001) },
	{ }
};
MODULE_DEVICE_TABLE(usb, pwc_device_table);

static void *usb_pwc_probe(struct usb_device *udev, unsigned int ifnum, const struct usb_device_id *id);
static void usb_pwc_disconnect(struct usb_device *udev, void *ptr);

static struct usb_driver pwc_driver =
{
	name:			"Philips webcam",	/* name */
	id_table:		pwc_device_table,
	probe:			usb_pwc_probe,		/* probe() */
	disconnect:		usb_pwc_disconnect,	/* disconnect() */
};

#define MAX_DEV_HINTS 10

static int default_size = PSZ_QCIF;
static int default_fps = 10;
static int default_palette = VIDEO_PALETTE_YUV420P; /* This format is understood by most tools */
static int default_fbufs = 3;   /* Default number of frame buffers */
static int default_mbufs = 2;	/* Default number of mmap() buffers */
       int pwc_trace = TRACE_MODULE | TRACE_FLOW | TRACE_PWCX;
static int power_save = 0;
static int led_on = 1, led_off = 0; /* defaults to LED that is on while in use */
       int pwc_preferred_compression = 2; /* 0..3 = uncompressed..high */
static struct {
	int type;
	char serial_number[30];
	int device_node;
	struct pwc_device *pdev;
} device_hint[MAX_DEV_HINTS];

static struct semaphore mem_lock;
static void *mem_leak = NULL; /* For delayed kfree()s. See below */

/***/

static int  pwc_video_open(struct video_device *vdev, int mode);
static void pwc_video_close(struct video_device *vdev);
static long pwc_video_read(struct video_device *vdev, char *buf, unsigned long count, int noblock);
static long pwc_video_write(struct video_device *vdev, const char *buf, unsigned long count, int noblock);
static unsigned int pwc_video_poll(struct video_device *vdev, struct file *file, poll_table *wait);
static int  pwc_video_ioctl(struct video_device *vdev, unsigned int cmd, void *arg);
static int  pwc_video_mmap(struct video_device *dev, const char *adr, unsigned long size);

static struct video_device pwc_template = {
	owner:		THIS_MODULE,
	name:		"Philips Webcam",	/* Filled in later */
	type:		VID_TYPE_CAPTURE,
	hardware:	VID_HARDWARE_PWC,
	open:		pwc_video_open,
	close:		pwc_video_close,
	read:		pwc_video_read,
	write:		pwc_video_write,
	poll:		pwc_video_poll,
	ioctl:		pwc_video_ioctl,
	mmap:		pwc_video_mmap,
	initialize:	NULL,			/* initialize */
	minor:		0			/* minor */
};

/***************************************************************************/

/* Okay, this is some magic that I worked out and the reasoning behind it...

   The biggest problem with any USB device is of course: "what to do 
   when the user unplugs the device while it is in use by an application?"
   We have several options:
   1) Curse them with the 7 plagues when they do (requires divine intervention)
   2) Tell them not to (won't work: they'll do it anyway)
   3) Oops the kernel (this will have a negative effect on a user's uptime)
   4) Do something sensible.
   
   Of course, we go for option 4.

   It happens that this device will be linked to two times, once from
   usb_device and once from the video_device in their respective 'private'
   pointers. This is done when the device is probed() and all initialization
   succeeded. The pwc_device struct links back to both structures.

   When a device is unplugged while in use it will be removed from the 
   list of known USB devices; I also de-register as a V4L device, but 
   unfortunately I can't free the memory since the struct is still in use
   by the file descriptor. This free-ing is then deferend until the first
   opportunity. Crude, but it works.
   
   A small 'advantage' is that if a user unplugs the cam and plugs it back
   in, it should get assigned the same video device minor, but unfortunately
   it's non-trivial to re-link the cam back to the video device... (that 
   would surely be magic! :))
*/

/***************************************************************************/
/* Private functions */

/* Memory management functions, nicked from cpia.c, which nicked them from
   bttv.c. So far, I've counted duplication of this code 6 times 
   (bttv, cpia, ibmcam, ov511, pwc, ieee1394).
 */

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
        unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;
  
	if (!pgd_none(*pgd)) {
                pmd = pmd_offset(pgd, adr);
                if (!pmd_none(*pmd)) {
                        ptep = pte_offset(pmd, adr);
                        pte = *ptep;
                        if(pte_present(pte)) {
				ret  = (unsigned long) page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
				
			}
                }
        }
	return ret;
}



/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long kvirt_to_pa(unsigned long adr) 
{
        unsigned long va, kva, ret;

        va = VMALLOC_VMADDR(adr);
        kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);
        return ret;
}

static void * rvmalloc(signed long size)
{
	void * mem;
	unsigned long adr, page;

        /* Round it off to PAGE_SIZE */
        size += (PAGE_SIZE - 1);
        size &= ~(PAGE_SIZE - 1);	
        
        mem=vmalloc_32(size);
	if (mem) 
	{
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_pa(adr);
			mem_map_reserve(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void * mem, signed long size)
{
        unsigned long adr, page;
        
        /* Round it off to PAGE_SIZE */
        size += (PAGE_SIZE - 1);
        size &= ~(PAGE_SIZE - 1);	
	if (mem) 
	{
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_pa(adr);
			mem_map_unreserve(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}




static int pwc_allocate_buffers(struct pwc_device *pdev)
{
	int i;
	void *kbuf;

	Trace(TRACE_MEMORY, "Entering allocate_buffers(%p).\n", pdev);

	if (pdev == NULL)
		return -ENXIO;
		
#ifdef PWC_MAGIC
	if (pdev->magic != PWC_MAGIC) {
		Err("allocate_buffers(): magic failed.\n");
		return -ENXIO;
	}
#endif	
	/* Allocate Isochronuous pipe buffers */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (pdev->sbuf[i].data == NULL) {
			kbuf = kmalloc(ISO_BUFFER_SIZE, GFP_KERNEL);
			if (kbuf == NULL) {
				Err("Failed to allocate iso buffer %d.\n", i);
				return -ENOMEM;
			}
			Trace(TRACE_MEMORY, "Allocated iso buffer at %p.\n", kbuf);
			pdev->sbuf[i].data = kbuf;
			memset(kbuf, 0, ISO_BUFFER_SIZE);
		}
	}

	/* Allocate frame buffer structure */
	if (pdev->fbuf == NULL) {
		kbuf = kmalloc(default_fbufs * sizeof(struct pwc_frame_buf), GFP_KERNEL);
		if (kbuf == NULL) {
			Err("Failed to allocate frame buffer structure.\n");
			return -ENOMEM;
		}
		Trace(TRACE_MEMORY, "Allocated frame buffer structure at %p.\n", kbuf);
		pdev->fbuf = kbuf;
		memset(kbuf, 0, default_fbufs * sizeof(struct pwc_frame_buf));
	}
	/* create frame buffers, and make circular ring */
	for (i = 0; i < default_fbufs; i++) {
		if (pdev->fbuf[i].data == NULL) {
			kbuf = vmalloc(PWC_FRAME_SIZE); /* need vmalloc since frame buffer > 128K */
			if (kbuf == NULL) {
				Err("Failed to allocate frame buffer %d.\n", i);
				return -ENOMEM;
			}
			Trace(TRACE_MEMORY, "Allocated frame buffer %d at %p.\n", i, kbuf);
			pdev->fbuf[i].data = kbuf;
			memset(kbuf, 128, PWC_FRAME_SIZE);
		}
	}
	
	/* Allocate decompressor table space */
	kbuf = NULL;
	if (pdev->decompressor != NULL) {
		kbuf = kmalloc(pdev->decompressor->table_size, GFP_KERNEL);
		if (kbuf == NULL) {
			Err("Failed to allocate decompress table.\n");
			return -ENOMEM;
		}
		Trace(TRACE_MEMORY, "Allocated decompress table %p.\n", kbuf);
	}
	pdev->decompress_data = kbuf;
	
	/* Allocate image buffer; double buffer for mmap() */
	kbuf = rvmalloc(default_mbufs * pdev->len_per_image);
	if (kbuf == NULL) {
		Err("Failed to allocate image buffer(s).\n");
		return -ENOMEM;
	}
	Trace(TRACE_MEMORY, "Allocated image buffer at %p.\n", kbuf);
	pdev->image_data = kbuf;
	for (i = 0; i < default_mbufs; i++)
		pdev->image_ptr[i] = kbuf + i * pdev->len_per_image;
	for (; i < MAX_IMAGES; i++)
		pdev->image_ptr[i] = NULL;

	Trace(TRACE_MEMORY, "Leaving pwc_allocate_buffers().\n");
	return 0;
}

static void pwc_free_buffers(struct pwc_device *pdev)
{
	int i;

	Trace(TRACE_MEMORY, "Entering free_buffers(%p).\n", pdev);

	if (pdev == NULL)
		return;
#ifdef PWC_MAGIC
	if (pdev->magic != PWC_MAGIC) {
		Err("free_buffers(): magic failed.\n");
		return;
	}
#endif	

	/* Release Iso-pipe buffers */
	for (i = 0; i < MAX_ISO_BUFS; i++)
		if (pdev->sbuf[i].data != NULL) {
			Trace(TRACE_MEMORY, "Freeing ISO buffer at %p.\n", pdev->sbuf[i].data);
			kfree(pdev->sbuf[i].data);
			pdev->sbuf[i].data = NULL;
		}

	/* The same for frame buffers */
	if (pdev->fbuf != NULL) {
		for (i = 0; i < default_fbufs; i++) {
			if (pdev->fbuf[i].data != NULL) {
				Trace(TRACE_MEMORY, "Freeing frame buffer %d at %p.\n", i, pdev->fbuf[i].data);
				vfree(pdev->fbuf[i].data);
				pdev->fbuf[i].data = NULL;
			}
		}
		kfree(pdev->fbuf);
		pdev->fbuf = NULL;
	}

	/* Intermediate decompression buffer & tables */
	if (pdev->decompress_data != NULL) {
		Trace(TRACE_MEMORY, "Freeing decompression buffer at %p.\n", pdev->decompress_data);
		kfree(pdev->decompress_data);
		pdev->decompress_data = NULL;
	}
	pdev->decompressor = NULL;

	/* Release image buffers */
	if (pdev->image_data != NULL) {
		Trace(TRACE_MEMORY, "Freeing image buffer at %p.\n", pdev->image_data);
		rvfree(pdev->image_data, default_mbufs * pdev->len_per_image);
	}
	pdev->image_data = NULL;
	Trace(TRACE_MEMORY, "Leaving free_buffers().\n");
}

/* The frame & image buffer mess. 

   Yes, this is a mess. Well, it used to be simple, but alas...  In this
   module, 3 buffers schemes are used to get the data from the USB bus to
   the user program. The first scheme involves the ISO buffers (called thus
   since they transport ISO data from the USB controller), and not really
   interesting. Suffices to say the data from this buffer is quickly 
   gathered in an interrupt handler (pwc_isoc_handler) and placed into the 
   frame buffer.
   
   The frame buffer is the second scheme, and is the central element here.
   It collects the data from a single frame from the camera (hence, the
   name). Frames are delimited by the USB camera with a short USB packet,
   so that's easy to detect. The frame buffers form a list that is filled
   by the camera+USB controller and drained by the user process through 
   either read() or mmap().
   
   The image buffer is the third scheme, in which frames are decompressed
   and possibly converted into planar format. For mmap() there is more than
   one image buffer available.

   The frame buffers provide the image buffering, in case the user process
   is a bit slow. This introduces lag and some undesired side-effects.
   The problem arises when the frame buffer is full. I used to drop the last 
   frame, which makes the data in the queue stale very quickly. But dropping 
   the frame at the head of the queue proved to be a litte bit more difficult.
   I tried a circular linked scheme, but this introduced more problems than
   it solved.

   Because filling and draining are completely asynchronous processes, this
   requires some fiddling with pointers and mutexes.
   
   Eventually, I came up with a system with 2 lists: an 'empty' frame list
   and a 'full' frame list:
     * Initially, all frame buffers but one are on the 'empty' list; the one
       remaining buffer is our initial fill frame.
     * If a frame is needed for filling, we take it from the 'empty' list, 
       unless that list is empty, in which case we take the buffer at the 
       head of the 'full' list.
     * When our fill buffer has been filled, it is appended to the 'full' 
       list.
     * If a frame is needed by read() or mmap(), it is taken from the head of 
       the 'full' list, handled, and then appended to the 'empty' list. If no
       buffer is present on the 'full' list, we wait.
   The advantage is that the buffer that is currently being decompressed/
   converted, is on neither list, and thus not in our way (any other scheme 
   I tried had the problem of old data lingering in the queue).

   Whatever strategy you choose, it always remains a tradeoff: with more
   frame buffers the chances of a missed frame are reduced. On the other
   hand, on slower machines it introduces lag because the queue will 
   always be full.
 */

/**
  \brief Find next frame buffer to fill. Take from empty or full list, whichever comes first.
 */
static inline int pwc_next_fill_frame(struct pwc_device *pdev)
{
	int ret;
	unsigned long flags;
	
	ret = 0;
	spin_lock_irqsave(&pdev->ptrlock, flags);
	if (pdev->fill_frame != NULL) {
		/* append to 'full' list */
		if (pdev->full_frames == NULL) {
			pdev->full_frames = pdev->fill_frame;
			pdev->full_frames_tail = pdev->full_frames;
		}
		else {
			pdev->full_frames_tail->next = pdev->fill_frame;
			pdev->full_frames_tail = pdev->fill_frame;
		}
	}
	if (pdev->empty_frames != NULL) {
		/* We have empty frames available. That's easy */
		pdev->fill_frame = pdev->empty_frames;
		pdev->empty_frames = pdev->empty_frames->next;	
	}
	else {
		/* Hmm. Take it from the full list */
#if PWC_DEBUG		
		/* sanity check */
		if (pdev->full_frames == NULL) {
			Err("Neither empty or full frames available!\n");
			spin_unlock_irqrestore(&pdev->ptrlock, flags);
			return -EINVAL;
		}
#endif
		pdev->fill_frame = pdev->full_frames;
		pdev->full_frames = pdev->full_frames->next;
		ret = 1;
	}
	pdev->fill_frame->next = NULL;
#if PWC_DEBUG
	Trace(TRACE_SEQUENCE, "Assigning sequence number %d.\n", pdev->sequence);
	pdev->fill_frame->sequence = pdev->sequence++;
#endif
	spin_unlock_irqrestore(&pdev->ptrlock, flags);
	return ret;
}
 

/**
  \brief Reset all buffers, pointers and lists, except for the image_used[] buffer. 
  
  If the image_used[] buffer is cleared too, mmap()/VIDIOCSYNC will run into trouble.
 */
static void pwc_reset_buffers(struct pwc_device *pdev)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&pdev->ptrlock, flags);
	pdev->full_frames = NULL;
	pdev->full_frames_tail = NULL;
	for (i = 0; i < default_fbufs; i++) {
		pdev->fbuf[i].filled = 0;
		if (i > 0)
			pdev->fbuf[i].next = &pdev->fbuf[i - 1];
		else
			pdev->fbuf->next = NULL;
	}
	pdev->empty_frames = &pdev->fbuf[default_fbufs - 1];
	pdev->empty_frames_tail = pdev->fbuf;
	pdev->read_frame = NULL;
	pdev->fill_frame = pdev->empty_frames;
	pdev->empty_frames = pdev->empty_frames->next;

	pdev->image_read_pos = 0;
	pdev->fill_image = 0;
	spin_unlock_irqrestore(&pdev->ptrlock, flags);
}


/**
  \brief Do all the handling for getting one frame: get pointer, decompress, advance pointers.
 */
static int pwc_handle_frame(struct pwc_device *pdev)
{
	int ret = 0;
	unsigned long flags;
	
	spin_lock_irqsave(&pdev->ptrlock, flags);
	/* First grab our read_frame; this is removed from all lists, so
	   we can release the lock after this without problems */
	if (pdev->read_frame != NULL) {
		/* This can't theoretically happen */
		Err("Huh? Read frame still in use?\n");
	}
	else {
		if (pdev->full_frames == NULL) {
			Err("Woops. No frames ready.\n");
		}
		else {
			pdev->read_frame = pdev->full_frames;
			pdev->full_frames = pdev->full_frames->next;
			pdev->read_frame->next = NULL;
		}

		if (pdev->read_frame != NULL) {
#if PWC_DEBUG
			Trace(TRACE_SEQUENCE, "Decompressing frame %d\n", pdev->read_frame->sequence);
#endif
			/* Decompression is a lenghty process, so it's outside of the lock.
			   This gives the isoc_handler the opportunity to fill more frames 
			   in the mean time.
			*/
			spin_unlock_irqrestore(&pdev->ptrlock, flags);
			ret = pwc_decompress(pdev);
			spin_lock_irqsave(&pdev->ptrlock, flags);

			/* We're done with read_buffer, tack it to the end of the empty buffer list */
			if (pdev->empty_frames == NULL) {
				pdev->empty_frames = pdev->read_frame;
				pdev->empty_frames_tail = pdev->empty_frames;
			}
			else {
				pdev->empty_frames_tail->next = pdev->read_frame;
				pdev->empty_frames_tail = pdev->read_frame;
			}
			pdev->read_frame = NULL;
		}
	}
	spin_unlock_irqrestore(&pdev->ptrlock, flags);
	return ret;
}

/**
  \brief Advance pointers of image buffer (after each user request) 
*/
static inline void pwc_next_image(struct pwc_device *pdev)
{
	pdev->image_used[pdev->fill_image] = 0;
	pdev->fill_image = (pdev->fill_image + 1) % default_mbufs;
}

/* 2001-10-14: The YUV420 is still there, but you can only set it from within 
   a program (YUV420P being the default) */
static int pwc_set_palette(struct pwc_device *pdev, int pal)
{
	if (   pal == VIDEO_PALETTE_YUV420
            || pal == VIDEO_PALETTE_YUV420P
#if PWC_DEBUG
            || pal == VIDEO_PALETTE_RAW
#endif
	) {
		pdev->vpalette = pal;
		pwc_set_image_buffer_size(pdev);
		return 0;
	}
	Trace(TRACE_READ, "Palette %d not supported.\n", pal);
	return -1;
}



/* This gets called for the Isochronous pipe (video). This is done in
 * interrupt time, so it has to be fast, not crash, and not stall. Neat.
 */
static void pwc_isoc_handler(purb_t urb)
{
	struct pwc_device *pdev;
	int i, fst, flen;
	int awake;
	struct pwc_frame_buf *fbuf;
	unsigned char *fillptr, *iso_buf;

	pdev = (struct pwc_device *)urb->context;
	if (pdev == NULL) {
		Err("isoc_handler() called with NULL device?!\n");
		return;
	}
#ifdef PWC_MAGIC	
	if (pdev->magic != PWC_MAGIC) {
		Err("isoc_handler() called with bad magic!\n");
		return;
	}
#endif
	if (urb->status == -ENOENT || urb->status == -ECONNRESET) {
		Trace(TRACE_OPEN, "pwc_isoc_handler(): URB unlinked.\n");
		return;
	}
	if (urb->status != -EINPROGRESS && urb->status != 0) {
		char *errmsg;
		
		errmsg = "Unknown";
		switch(urb->status) {
			case -ENOSR:		errmsg = "Buffer error (overrun)"; break;
			case -EPIPE:		errmsg = "Stalled (device not responding)"; break;
			case -EOVERFLOW:	errmsg = "Babble (bad cable?)"; break;
			case -EPROTO:		errmsg = "Bit-stuff error (bad cable?)"; break;
			case -EILSEQ:		errmsg = "CRC/Timeout"; break;
			case -ETIMEDOUT:	errmsg = "NAK (device does not respond)"; break;
		}
		Trace(TRACE_FLOW, "pwc_isoc_handler() called with status %d [%s].\n", urb->status, errmsg);
		return;
	}

	fbuf = pdev->fill_frame;
	if (fbuf == NULL) {
		Err("pwc_isoc_handler without valid fill frame.\n");
		wake_up_interruptible(&pdev->frameq);
		return;
	}
	fillptr = fbuf->data + fbuf->filled;
	awake = 0;

	/* vsync: 0 = don't copy data
	          1 = sync-hunt
	          2 = synched 
	 */
	/* Compact data */
	for (i = 0; i < urb->number_of_packets; i++) {
		fst  = urb->iso_frame_desc[i].status;
		flen = urb->iso_frame_desc[i].actual_length;
		iso_buf = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		if (fst == 0) {
			if (flen > 0) { /* if valid data... */
				if (pdev->vsync > 0) { /* ...and we are not sync-hunting... */
					pdev->vsync = 2;

					/* ...copy data to frame buffer, if possible */
					if (flen + fbuf->filled > pdev->frame_size) {
						Trace(TRACE_FLOW, "Frame buffer overflow (flen = %d, frame_size = %d).\n", flen, pdev->frame_size);
						pdev->vsync = 0; /* Hmm, let's wait for an EOF (end-of-frame) */
						pdev->vframes_error++;
					}
					else {
						memmove(fillptr, iso_buf, flen);
						fillptr += flen;
					}
				}
				fbuf->filled += flen;
			} /* ..flen > 0 */

			if (flen < pdev->vlast_packet_size) {
				/* Shorter packet... We probably have the end of an image-frame; 
				   wake up read() process and let select()/poll() do something.
				   Decompression is done in user time over there. 
				 */
				if (pdev->vsync == 2) {
					/* The ToUCam Fun CMOS sensor causes the firmware to send 2 or 3 bogus 
					   frames on the USB wire after an exposure change. This conditition is 
					   however detected  in the cam and a bit is set in the header.
					 */
					if (pdev->type == 730) {
						unsigned char *ptr = (unsigned char *)fbuf->data;
						
						if (ptr[1] == 1 && ptr[0] & 0x10) {
#if PWC_DEBUG
							Debug("Hyundai CMOS sensor bug. Dropping frame %d.\n", fbuf->sequence);
#endif
							pdev->drop_frames = 2;
							pdev->vframes_error++;
						}
						/* Sometimes the trailer of the 730 is still sent as a 4 byte packet 
						   after a short frame; this condition is filtered out specifically. A 4 byte
						   frame doesn't make sense anyway.
						   So we get either this sequence: 
						   	drop_bit set -> 4 byte frame -> short frame -> good frame
						   Or this one:
						   	drop_bit set -> short frame -> good frame
						   So we drop either 3 or 2 frames in all!
						 */
						if (fbuf->filled == 4)
							pdev->drop_frames++;
					}

					/* In case we were instructed to drop the frame, do so silently.
					   The buffer pointers are not updated either (but the counters are reset below).
					 */
					if (pdev->drop_frames)
						pdev->drop_frames--;
					else {
						/* Check for underflow first */
						if (fbuf->filled < pdev->frame_size) {
							Trace(TRACE_FLOW, "Frame buffer underflow (%d bytes); discarded.\n", fbuf->filled);
							pdev->vframes_error++;
						}
						else {
							/* Send only once per EOF */
							awake = 1; /* delay wake_ups */
							
							/* Find our next frame to fill. This will always succeed, since we
							 * nick a frame from either empty or full list, but if we had to
							 * take it from the full list, it means a frame got dropped.
							 */
							if (pwc_next_fill_frame(pdev)) {
								pdev->vframes_dumped++;
								if ((pdev->vframe_count > FRAME_LOWMARK) && (pwc_trace & TRACE_FLOW)) {
									if (pdev->vframes_dumped < 20)
										Trace(TRACE_FLOW, "Dumping frame %d.\n", pdev->vframe_count);
									if (pdev->vframes_dumped == 20)
										Trace(TRACE_FLOW, "Dumping frame %d (last message).\n", pdev->vframe_count);
								}
							}
							fbuf = pdev->fill_frame;
						}
					} /* !drop_frames */
					pdev->vframe_count++;
				}
				fbuf->filled = 0;
				fillptr = fbuf->data;
				pdev->vsync = 1;
			} /* .. flen < last_packet_size */
			pdev->vlast_packet_size = flen;
		} /* ..status == 0 */
#ifdef PWC_DEBUG
		/* This is normally not interesting to the user, unless you are really debugging something */
		else 
			Trace(TRACE_FLOW, "Iso frame %d of USB has error %d\n", i, fst);
#endif			
	}
	if (awake)
		wake_up_interruptible(&pdev->frameq);
}


static int pwc_isoc_init(struct pwc_device *pdev)
{
	struct usb_device *udev;
	purb_t urb;
	int i, j, ret;

	struct usb_interface_descriptor *idesc;
	int cur_alt;

	if (pdev == NULL)
		return -EFAULT;
	if (pdev->iso_init)
		return 0;
	pdev->vsync = 0;
	udev = pdev->udev;
	
	/* Get the current alternate interface, adjust packet size */
	if (!udev->actconfig)
		return -EFAULT;
	cur_alt = udev->actconfig->interface[0].act_altsetting;
	idesc = &udev->actconfig->interface[0].altsetting[cur_alt];
	if (!idesc)
		return -EFAULT;

	/* Search video endpoint */
	pdev->vmax_packet_size = -1;
	for (i = 0; i < idesc->bNumEndpoints; i++)
		if ((idesc->endpoint[i].bEndpointAddress & 0xF) == pdev->vendpoint) {
			pdev->vmax_packet_size = idesc->endpoint[i].wMaxPacketSize;
			break;
		}
	
	if (pdev->vmax_packet_size < 0) {
		Err("Failed to find packet size for video endpoint in current alternate setting.\n");
		return -ENFILE; /* Odd error, that should be noticable */
	}

	ret = 0;
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		urb = usb_alloc_urb(ISO_FRAMES_PER_DESC);
		if (urb == NULL) {
			Err("Failed to allocate urb %d\n", i);
			ret = -ENOMEM;
			break;
		}
		pdev->sbuf[i].urb = urb;
	}
	if (ret) {
		/* De-allocate in reverse order */
		while (i >= 0) {
			if (pdev->sbuf[i].urb != NULL)
				usb_free_urb(pdev->sbuf[i].urb);
			pdev->sbuf[i].urb = NULL;
			i--;
		}
		return ret;
	}
	
	
	/* init URB structure */	
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		urb = pdev->sbuf[i].urb;

		urb->next = pdev->sbuf[(i + 1) % MAX_ISO_BUFS].urb;
		urb->dev = udev;
	        urb->pipe = usb_rcvisocpipe(udev, pdev->vendpoint);
		urb->transfer_flags = USB_ISO_ASAP;
	        urb->transfer_buffer = pdev->sbuf[i].data;
	        urb->transfer_buffer_length = ISO_BUFFER_SIZE;
	        urb->complete = pwc_isoc_handler;
	        urb->context = pdev;
		urb->start_frame = 0;
		urb->number_of_packets = ISO_FRAMES_PER_DESC;
		for (j = 0; j < ISO_FRAMES_PER_DESC; j++) {
			urb->iso_frame_desc[j].offset = j * ISO_MAX_FRAME_SIZE;
			urb->iso_frame_desc[j].length = ISO_MAX_FRAME_SIZE;
		}
	}

	/* link */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		ret = usb_submit_urb(pdev->sbuf[i].urb);
		if (ret)
			Err("isoc_init() submit_urb %d failed with error %d\n", i, ret);
		else
			Trace(TRACE_OPEN, "pwc_isoc_init(): URB submitted.\n");
	}

	/* data should stream in now */
	pdev->iso_init = 1;
	return 0;
}

static void pwc_isoc_cleanup(struct pwc_device *pdev)
{
	int i;
	
	if (pdev == NULL)
		return;
	if (!pdev->iso_init)
		return;
	/* Stop camera, but only if we are sure the camera is still there */
	if (!pdev->unplugged)
		usb_set_interface(pdev->udev, 0, 0);
	for (i = MAX_ISO_BUFS - 1; i >= 0; i--) {
		pdev->sbuf[i].urb->next = NULL;
		usb_unlink_urb(pdev->sbuf[i].urb);
		usb_free_urb(pdev->sbuf[i].urb);
		pdev->sbuf[i].urb = NULL;
	}
	pdev->iso_init = 0;
}

int pwc_try_video_mode(struct pwc_device *pdev, int width, int height, int new_fps, int new_compression, int new_snapshot)
{
	int ret;

	/* Stop isoc stuff */
	pwc_isoc_cleanup(pdev);
	/* Reset parameters */
	pwc_reset_buffers(pdev);
	/* Try to set video mode... */
	ret = pwc_set_video_mode(pdev, width, height, new_fps, new_compression, new_snapshot);
	if (ret) /* That failed... restore old mode (we know that worked) */
		ret = pwc_set_video_mode(pdev, pdev->view.x, pdev->view.y, pdev->vframes, pdev->vcompression, pdev->vsnapshot);
	else /* Set (new) alternate interface */
		ret = usb_set_interface(pdev->udev, 0, pdev->valternate);
	if (!ret)
		ret = pwc_isoc_init(pdev);
	pdev->drop_frames = 1; /* try to avoid garbage during switch */
	return ret;
}


static inline void set_mem_leak(void *ptr)
{
	down(&mem_lock);
	if (mem_leak != NULL)
		Err("Memleak: overwriting mem_leak pointer!\n");
	Trace(TRACE_MEMORY, "Setting mem_leak to 0x%p.\n", ptr);
	mem_leak = ptr;
	up(&mem_lock);
}

static inline void free_mem_leak(void)
{
	down(&mem_lock);
	if (mem_leak != NULL) {
		Trace(TRACE_MEMORY, "Freeing mem_leak ptr 0x%p.\n", mem_leak);
		kfree(mem_leak);
		mem_leak = NULL;
	}
	up(&mem_lock);
}


/***************************************************************************/
/* Video4Linux functions */

static int pwc_video_open(struct video_device *vdev, int mode)
{
	int i;
	struct pwc_device *pdev;

	Trace(TRACE_OPEN, "video_open called(0x%p, 0%o).\n", vdev, mode);
	
	if (vdev == NULL)
		BUG();
	pdev = (struct pwc_device *)vdev->priv;
	if (pdev == NULL)
		BUG();
	
	down(&pdev->modlock);
	if (!pdev->usb_init) {
		Trace(TRACE_OPEN, "Doing first time initialization.\n");
		/* Reset camera */
		if (usb_set_interface(pdev->udev, 0, 0))
			Info("Failed to set alternate interface to 0.\n");
		pdev->usb_init = 1;
	}

	/* Turn on camera */
	if (power_save) {
		i = pwc_camera_power(pdev, 1);
		if (i < 0)
			Info("Failed to restore power to the camera! (%d)\n", i);
	}
	/* Set LED on/off time */
	if (pwc_set_leds(pdev, led_on, led_off) < 0)
		Info("Failed to set LED on/off time.\n");

	/* Find our decompressor, if any */
	pdev->decompressor = pwc_find_decompressor(pdev->type);
#if PWC_DEBUG	
	Debug("Found decompressor for %d at 0x%p\n", pdev->type, pdev->decompressor);
#endif

	/* So far, so good. Allocate memory. */
	i = pwc_allocate_buffers(pdev);
	if (i < 0) {
		Trace(TRACE_OPEN, "Failed to allocate buffer memory.\n");
		up(&pdev->modlock);
		return i;
	}
	
	/* Reset buffers & parameters */
	pwc_reset_buffers(pdev);
	for (i = 0; i < default_mbufs; i++)
		pdev->image_used[i] = 0;
	pdev->vframe_count = 0;
	pdev->vframes_dumped = 0;
	pdev->vframes_error = 0;
	pdev->vpalette = default_palette;
#if PWC_DEBUG	
	pdev->sequence = 0;
#endif

	/* Set some defaults */
	pdev->vsnapshot = 0;
	if (pdev->type == 730 || pdev->type == 740 || pdev->type == 750)
		pdev->vsize = PSZ_QSIF;
	else
		pdev->vsize = PSZ_QCIF;
	pdev->vframes = 10;

	/* Start iso pipe for video; first try user-supplied size/fps, if
	   that fails try QCIF/10 or QSIF/10 (a reasonable default), 
	   then give up 
	 */
	i = pwc_set_video_mode(pdev, pwc_image_sizes[default_size].x, pwc_image_sizes[default_size].y, default_fps, pdev->vcompression, 0);
	if (i)	{
		Trace(TRACE_OPEN, "First attempt at set_video_mode failed.\n");
		if (pdev->type == 730 || pdev->type == 740 || pdev->type == 750)
			i = pwc_set_video_mode(pdev, pwc_image_sizes[PSZ_QSIF].x, pwc_image_sizes[PSZ_QSIF].y, 10, pdev->vcompression, 0);
		else
			i = pwc_set_video_mode(pdev, pwc_image_sizes[PSZ_QCIF].x, pwc_image_sizes[PSZ_QCIF].y, 10, pdev->vcompression, 0);
	}
	if (i) {
		Trace(TRACE_OPEN, "Second attempt at set_video_mode failed.\n");
		up(&pdev->modlock);
		return i;
	}
	
	i = usb_set_interface(pdev->udev, 0, pdev->valternate);
	if (i) {
		Trace(TRACE_OPEN, "Failed to set alternate interface = %d.\n", i);
		up(&pdev->modlock);
		return -EINVAL;
	}
	i = pwc_isoc_init(pdev);
	if (i) {
		Trace(TRACE_OPEN, "Failed to init ISOC stuff = %d.\n", i);
		MOD_DEC_USE_COUNT;
		up(&pdev->modlock);
		return i;
	}

	pdev->vopen++;
	/* lock decompressor; this has a small race condition, since we 
	   could in theory unload pwcx.o between pwc_find_decompressor()
	   above and this call. I doubt it's ever going to be a problem.
	 */
	if (pdev->decompressor != NULL)
		pdev->decompressor->lock();
	up(&pdev->modlock);
	Trace(TRACE_OPEN, "video_open() returning 0.\n");
	return 0;
}

/* Note that all cleanup is done in the reverse order as in _open */
static void pwc_video_close(struct video_device *vdev)
{
	struct pwc_device *pdev;
	int i;

	Trace(TRACE_OPEN, "video_close called(0x%p).\n", vdev);

	pdev = (struct pwc_device *)vdev->priv;
	if (pdev->vopen == 0)
		Info("video_close() called on closed device?\n");

	/* Free isoc URBs */
	pwc_isoc_cleanup(pdev);

	/* Dump statistics, but only if a reasonable amount of frames were
	   processed (to prevent endless log-entries in case of snap-shot
	   programs) 
	 */
	if (pdev->vframe_count > 20)
		Info("Closing video device: %d frames received, dumped %d frames, %d frames with errors.\n", pdev->vframe_count, pdev->vframes_dumped, pdev->vframes_error);

	if (pdev->unplugged) {
		/* The device was unplugged or some other error occured */
		/* We unregister the video_device */
		Trace(TRACE_OPEN, "Delayed video device unregistered.\n");
		video_unregister_device(pdev->vdev);
	}
	else {
		/* Normal close: stop isochronuous and interrupt endpoint */
		Trace(TRACE_OPEN, "Normal close(): setting interface to 0.\n");
		usb_set_interface(pdev->udev, 0, 0);

		/* Turn LEDs off */
		if (pwc_set_leds(pdev, 0, 0) < 0)
			Info("Failed to set LED on/off time..\n");
		/* Power down camere to save energy */
		if (power_save) {
			i = pwc_camera_power(pdev, 0);
			if (i < 0) 
				Err("Failed to power down camera (%d)\n", i);
		}
	}

	pdev->vopen = 0;
	if (pdev->decompressor != NULL) {
		pdev->decompressor->exit();
		pdev->decompressor->unlock();
	}
	pwc_free_buffers(pdev);

	/* wake up _disconnect() routine */
	if (pdev->unplugged)
		wake_up(&pdev->remove_ok);
}

/*
 *	FIXME: what about two parallel reads ????
 *      ANSWER: Not supported. You can't open the device more than once,
                despite what the V4L1 interface says. First, I don't see 
                the need, second there's no mechanism of alerting the 
                2nd/3rd/... process of events like changing image size.
                And I don't see the point of blocking that for the 
                2nd/3rd/... process.
                In multi-threaded environments reading parallel from any
                device is tricky anyhow.
 */
 
static long pwc_video_read(struct video_device *vdev, char *buf, unsigned long count, int noblock)
{
	struct pwc_device *pdev;
	DECLARE_WAITQUEUE(wait, current);

	Trace(TRACE_READ, "video_read(0x%p, %p, %ld, %d) called.\n", vdev, buf, count, noblock);
	if (vdev == NULL)
		return -EFAULT;
	pdev = vdev->priv;
	if (pdev == NULL)
		return -EFAULT;
	if (pdev->unplugged) {
		Info("pwc_video_read: Device got unplugged (1).\n");
		return -EPIPE; /* unplugged device! */
	}

	/* In case we're doing partial reads, we don't have to wait for a frame */
	if (pdev->image_read_pos == 0) {
		/* Do wait queueing according to the (doc)book */
		add_wait_queue(&pdev->frameq, &wait);
		while (pdev->full_frames == NULL) {
	                if (noblock) {
	                	remove_wait_queue(&pdev->frameq, &wait);
	                	set_current_state(TASK_RUNNING);
	                	return -EWOULDBLOCK;
	                }
	                if (signal_pending(current)) {
	                	remove_wait_queue(&pdev->frameq, &wait);
	                	set_current_state(TASK_RUNNING);
	                	return -ERESTARTSYS;
	                }
	                schedule();
	               	set_current_state(TASK_INTERRUPTIBLE);
		}
		remove_wait_queue(&pdev->frameq, &wait);
		set_current_state(TASK_RUNNING);
	                                                                                                                                                                                
		/* Decompress [, convert] and release frame */
		if (pwc_handle_frame(pdev))
			return -EFAULT;
	}

	Trace(TRACE_READ, "Copying data to user space.\n");
	/* copy bytes to user space; we allow for partial reads */
	if (count + pdev->image_read_pos > pdev->view.size)
		count = pdev->view.size - pdev->image_read_pos;
	if (copy_to_user(buf, pdev->image_ptr[pdev->fill_image] + pdev->image_read_pos, count))
		return -EFAULT;
	pdev->image_read_pos += count;
	if (pdev->image_read_pos >= pdev->view.size) { /* All data has been read */
		pdev->image_read_pos = 0;
		pwc_next_image(pdev);
	}
	return count;
}


static long pwc_video_write(struct video_device *vdev, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;   
}

static unsigned int pwc_video_poll(struct video_device *vdev, struct file *file, poll_table *wait)
{
	struct pwc_device *pdev;
	
	if (vdev == NULL)
		return -EFAULT;
	pdev = vdev->priv;
	if (pdev == NULL)
		return -EFAULT;
	
	poll_wait(file, &pdev->frameq, wait);
	if (pdev->unplugged) {
		Info("pwc_video_poll: Device got unplugged.\n");
		return POLLERR;
	}		
	if (pdev->full_frames != NULL) /* we have frames waiting */
		return (POLLIN | POLLRDNORM);

	return 0;
}
        
static int pwc_video_ioctl(struct video_device *vdev, unsigned int cmd, void *arg)
{
	struct pwc_device *pdev;
	DECLARE_WAITQUEUE(wait, current);
	
	if (vdev == NULL)
		return -EFAULT;
	pdev = vdev->priv;
	if (pdev == NULL)
		return -EFAULT;

	switch (cmd) {
		/* Query cabapilities */
		case VIDIOCGCAP: 
		{
			struct video_capability caps;

			strcpy(caps.name, vdev->name);
			caps.type = VID_TYPE_CAPTURE;
			caps.channels = 1;
			caps.audios = 1;
			caps.minwidth  = pdev->view_min.x;
			caps.minheight = pdev->view_min.y;
			caps.maxwidth  = pdev->view_max.x;
			caps.maxheight = pdev->view_max.y;
			if (copy_to_user(arg, &caps, sizeof(caps)))
				return -EFAULT;
			break;
		}

		/* Channel functions (simulate 1 channel) */
		case VIDIOCGCHAN:
		{
			struct video_channel v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if (v.channel != 0)
				return -EINVAL;

			v.flags = 0;
			v.tuners = 0;
			v.type = VIDEO_TYPE_CAMERA;
			strcpy(v.name, "Webcam");

			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;

			return 0;
		}

		case VIDIOCSCHAN:
		{	
			/* The spec says the argument is an integer, but
			   the bttv driver uses a video_channel arg, which
			   makes sense becasue it also has the norm flag.
			 */
			struct video_channel v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			if (v.channel != 0)
				return -EINVAL;

			return 0;
		}


		/* Picture functions; contrast etc. */
		case VIDIOCGPICT:
		{
			struct video_picture p;
			int val;

			p.colour = 0x8000;
			p.hue = 0x8000;
			val = pwc_get_brightness(pdev);
			if (val >= 0)
				p.brightness = val;
			else
				p.brightness = 0xffff;
			val = pwc_get_contrast(pdev);
			if (val >= 0)
				p.contrast = val;
			else
				p.contrast = 0xffff;
			/* Gamma, Whiteness, what's the difference? :) */
			val = pwc_get_gamma(pdev);
			if (val >= 0)
				p.whiteness = val;
			else
				p.whiteness = 0xffff;
			val = pwc_get_saturation(pdev);
			if (val >= 0)
				p.colour = val;
			else
				p.colour = 0xffff;
			p.depth = 24;
			p.palette = pdev->vpalette;
			p.hue = 0xFFFF; /* N/A */

			if (copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;
			break;
		}
		
		case VIDIOCSPICT:
		{
			struct video_picture p;

			if (copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;

			/*
			 *	FIXME:	Suppose we are mid read
			        ANSWER: No problem: the firmware of the camera
			                can handle brightness/contrast/etc
			                changes at _any_ time, and the palette
			                is used exactly once in the uncompress
			                routine.
			 */
			pwc_set_brightness(pdev, p.brightness);
			pwc_set_contrast(pdev, p.contrast);
			pwc_set_gamma(pdev, p.whiteness);
			pwc_set_saturation(pdev, p.colour);
			if (p.palette && p.palette != pdev->vpalette) {
				if (pwc_set_palette(pdev, p.palette) < 0)
					return -EINVAL;
			}
			break;
		}

		/* Window/size parameters */		
		case VIDIOCGWIN:
		{
			struct video_window vw;
			
			vw.x = 0;
			vw.y = 0;
			vw.width = pdev->view.x;
			vw.height = pdev->view.y;
			vw.chromakey = 0;
			vw.flags = (pdev->vframes << PWC_FPS_SHIFT) | 
			           (pdev->vsnapshot ? PWC_FPS_SNAPSHOT : 0);
			
			if (copy_to_user(arg, &vw, sizeof(vw)))
				return -EFAULT;			
			break;
		}
		
		case VIDIOCSWIN:
		{
			struct video_window vw;
			int fps, snapshot, ret;
		
			if (copy_from_user(&vw, arg, sizeof(vw)))
				return -EFAULT;

			fps = (vw.flags & PWC_FPS_FRMASK) >> PWC_FPS_SHIFT;
			snapshot = vw.flags & PWC_FPS_SNAPSHOT;
			if (fps == 0)
				fps = pdev->vframes;
			if (pdev->view.x == vw.width && pdev->view.y && fps == pdev->vframes && snapshot == pdev->vsnapshot)
				return 0;
			ret = pwc_try_video_mode(pdev, vw.width, vw.height, fps, pdev->vcompression, snapshot);
			if (ret)
				return ret;
			break;		
		}
		
		/* We don't have overlay support (yet) */
		case VIDIOCGFBUF:
		{
			struct video_buffer vb;
			
			vb.base = NULL;
			vb.height = 0;
			vb.width = 0;
			vb.depth = 0;
			vb.bytesperline = 0;
			
			if (copy_to_user((void *)arg, (void *)&vb, sizeof(vb)))
				return -EFAULT;
			break;
		}

		/* mmap() functions */
		case VIDIOCGMBUF:
		{
			/* Tell the user program how much memory is needed for a mmap() */
			struct video_mbuf vm;
			int i;

			memset(&vm, 0, sizeof(vm));
			vm.size = default_mbufs * pdev->len_per_image;
			vm.frames = default_mbufs; /* double buffering should be enough for most applications */
			for (i = 0; i < default_mbufs; i++)
				vm.offsets[i] = i * pdev->len_per_image;

			if (copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
				return -EFAULT;
			break;
		}

		case VIDIOCMCAPTURE:
		{
			/* Start capture into a given image buffer (called 'frame' in video_mmap structure) */
			struct video_mmap vm;

			if (copy_from_user((void *)&vm, (void *)arg, sizeof(vm)))
				return -EFAULT;
			Trace(TRACE_READ, "VIDIOCMCAPTURE: %dx%d, frame %d, format %d\n", vm.width, vm.height, vm.frame, vm.format);
			if (vm.frame < 0 || vm.frame >= default_mbufs)
				return -EINVAL;

			/* xawtv is nasty. It probes the available palettes
			   by setting a very small image size and trying
			   various palettes... The driver doesn't support
			   such small images, so I'm working around it.
			 */
			if (vm.format && vm.format != pdev->vpalette)
				if (pwc_set_palette(pdev, vm.format) < 0)
					return -EINVAL;
			 
			if ((vm.width != pdev->view.x || vm.height != pdev->view.y) &&
			    (vm.width >= pdev->view_min.x && vm.height >= pdev->view_min.y)) {
				int ret;
				
				Trace(TRACE_OPEN, "VIDIOCMCAPTURE: changing size to please xawtv :-(.\n");
				ret = pwc_try_video_mode(pdev, vm.width, vm.height, pdev->vframes, pdev->vcompression, pdev->vsnapshot);
				if (ret)
					return ret;
			} /* ... size mismatch */

			/* FIXME: should we lock here? */
			if (pdev->image_used[vm.frame])
				return -EBUSY;	/* buffer wasn't available. Bummer */
			pdev->image_used[vm.frame] = 1;

			/* Okay, we're done here. In the SYNC call we wait until a 
			   frame comes available, then expand image into the given 
			   buffer.
			   In contrast to the CPiA cam the Philips cams deliver a 
			   constant stream, almost like a grabber card. Also,
			   we have separate buffers for the rawdata and the image,
			   meaning we can nearly always expand into the requested buffer.
			 */
			Trace(TRACE_READ, "VIDIOCMCAPTURE done.\n");
			break;
		}

		case VIDIOCSYNC:
		{
			/* The doc says: "Whenever a buffer is used it should
			   call VIDIOCSYNC to free this frame up and continue."
			   
			   The only odd thing about this whole procedure is 
			   that MCAPTURE flags the buffer as "in use", and
			   SYNC immediately unmarks it, while it isn't 
			   after SYNC that you know that the buffer actually
			   got filled! So you better not start a CAPTURE in
			   the same frame immediately (use double buffering). 
			   This is not a problem for this cam, since it has 
			   extra intermediate buffers, but a hardware 
			   grabber card will then overwrite the buffer 
			   you're working on.
			 */
			int mbuf, ret;

			if (copy_from_user((void *)&mbuf, arg, sizeof(int)))
				return -EFAULT;

			Trace(TRACE_READ, "VIDIOCSYNC called (%d).\n", mbuf);

			/* bounds check */
			if (mbuf < 0 || mbuf >= default_mbufs)
				return -EINVAL;
			/* check if this buffer was requested anyway */
			if (pdev->image_used[mbuf] == 0)
				return -EINVAL;

			/* Add ourselves to the frame wait-queue.
			   
			   FIXME: needs auditing for safety.
			   QUSTION: In what respect? I think that using the
			            frameq is safe now.
			 */
			add_wait_queue(&pdev->frameq, &wait);
			while (pdev->full_frames == NULL) {
	                	if (signal_pending(current)) {
	                		remove_wait_queue(&pdev->frameq, &wait);
		                	set_current_state(TASK_RUNNING);
		                	return -ERESTARTSYS;
	        	        }
	                	schedule();
		                set_current_state(TASK_INTERRUPTIBLE);
			}
			remove_wait_queue(&pdev->frameq, &wait);
			set_current_state(TASK_RUNNING);
				
			/* The frame is ready. Expand in the image buffer 
			   requested by the user. I don't care if you 
			   mmap() 5 buffers and request data in this order: 
			   buffer 4 2 3 0 1 2 3 0 4 3 1 . . .
			   Grabber hardware may not be so forgiving.
			 */
			Trace(TRACE_READ, "VIDIOCSYNC: frame ready.\n");
			pdev->fill_image = mbuf; /* tell in which buffer we want the image to be expanded */
			/* Decompress, etc */
			ret = pwc_handle_frame(pdev);
			pdev->image_used[mbuf] = 0;
			if (ret)
				return -EFAULT;
			break;
		}
		
		case VIDIOCGAUDIO:
		{
			struct video_audio v;
			
			strcpy(v.name, "Microphone");
			v.audio = -1; /* unknown audio minor */
			v.flags = 0;
			v.mode = VIDEO_SOUND_MONO;
			v.volume = 0;
			v.bass = 0;
			v.treble = 0;
			v.balance = 0x8000;
			v.step = 1;
			
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			break;	
		}
		
		case VIDIOCSAUDIO:
		{
			struct video_audio v;
			
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			/* Dummy: nothing can be set */
			break;
		}
		
		case VIDIOCGUNIT:
		{
			struct video_unit vu;
			
			vu.video = pdev->vdev->minor & 0x3F;
			vu.audio = -1; /* not known yet */
			vu.vbi = -1;
			vu.radio = -1;
			vu.teletext = -1;
			if (copy_to_user(arg, &vu, sizeof(vu)))
				return -EFAULT;
			break;
		}
		default:
			return pwc_ioctl(pdev, cmd, arg);
	} /* ..switch */
	return 0;
}	

static int pwc_video_mmap(struct video_device *vdev, const char *adr, unsigned long size)
{
	struct pwc_device *pdev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;
	
	Trace(TRACE_MEMORY, "mmap(0x%p, 0x%p, %lu) called.\n", vdev, adr, size);
	pdev = vdev->priv;

	/* FIXME - audit mmap during a read */		
	pos = (unsigned long)pdev->image_data;
	while (size > 0) {
		page = kvirt_to_pa(pos);
		if (remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return 0;
}

/***************************************************************************/
/* USB functions */

/* This function gets called when a new device is plugged in or the usb core
 * is loaded.
 */

static void *usb_pwc_probe(struct usb_device *udev, unsigned int ifnum, const struct usb_device_id *id)
{
	struct pwc_device *pdev = NULL;
	struct video_device *vdev;
	int vendor_id, product_id, type_id;
	int i, hint;
	int video_nr = -1; /* default: use next available device */
	char serial_number[30];

	free_mem_leak();
	
	/* Check if we can handle this device */
	Trace(TRACE_PROBE, "probe() called [%04X %04X], if %d\n", udev->descriptor.idVendor, udev->descriptor.idProduct, ifnum);

	/* the interfaces are probed one by one. We are only interested in the
	   video interface (0) now.
	   Interface 1 is the Audio Control, and interface 2 Audio itself.
	 */
	if (ifnum > 0) 
		return NULL;

	vendor_id = udev->descriptor.idVendor;
	product_id = udev->descriptor.idProduct;

	if (vendor_id == 0x0471) {
		switch (product_id) {
		case 0x0302:
			Info("Philips PCA645VC USB webcam detected.\n");
			type_id = 645;
			break;
		case 0x0303:
			Info("Philips PCA646VC USB webcam detected.\n");
			type_id = 646;
			break;
		case 0x0304:
			Info("Askey VC010 type 2 USB webcam detected.\n");
			type_id = 646;
			break;
		case 0x0307:
			Info("Philips PCVC675K (Vesta) USB webcam detected.\n");
			type_id = 675;
			break;
		case 0x0308:
			Info("Philips PCVC680K (Vesta Pro) USB webcam detected.\n");
			type_id = 680;
			break;
		case 0x030C:
			Info("Philips PCVC690K (Vesta Pro Scan) USB webcam detected.\n");
			type_id = 690;
			break;
		case 0x0310:
			Info("Philips PCVC730K (ToUCam Fun) USB webcam detected.\n");
			type_id = 730;
			break;
		case 0x0311:
			Info("Philips PCVC740K (ToUCam Pro) USB webcam detected.\n");
			type_id = 740;
			break;
		case 0x0312:
			Info("Philips PCVC750K (ToUCam Pro Scan) USB webcam detected.\n");
			type_id = 750;
			break;
		default:
			return NULL;
			break;
		}
	}
	else if (vendor_id == 0x069A) {
		switch(product_id) {
		case 0x0001:
			Info("Askey VC010 type 1 USB webcam detected.\n");
			type_id = 645;
			break;
		default:
			return NULL;
			break;
		}
	}
	else if (vendor_id == 0x046d) {
		switch(product_id) {
		case 0x08b0:
			Info("Logitech QuickCam 3000 Pro detected.\n");
			type_id = 730;
        		break;
        	default:
        		return NULL;
        		break;
        	}
        }
	else if (vendor_id == 0x055d) {
		/* I don't know the difference between the C10 and the C30;
		   I suppose the difference is the sensor, but both cameras
		   work equally well with a type_id of 675
		 */
		switch(product_id) {
		case 0x9000:
			Info("Samsung MPC-C10 USB webcam detected.\n");
			type_id = 675;
			break;
		case 0x9001:
			Info("Samsung MPC-C30 USB webcam detected.\n");
			type_id = 675;
			break;
		default:
			return NULL;
			break;
		}
	}
	else return NULL; /* Not Philips, Askey, Logitech or Samsung, for sure. */

	memset(serial_number, 0, 30);
	usb_string(udev, udev->descriptor.iSerialNumber, serial_number, 29);
	Trace(TRACE_PROBE, "Device serial number is %s\n", serial_number);

	if (udev->descriptor.bNumConfigurations > 1)
		Info("Warning: more than 1 configuration available.\n");

	/* Allocate structure, initialize pointers, mutexes, etc. and link it to the usb_device */
	pdev = kmalloc(sizeof(struct pwc_device), GFP_KERNEL);
	if (pdev == NULL) {
		Err("Oops, could not allocate memory for pwc_device.\n");
		return NULL;
	}
	memset(pdev, 0, sizeof(struct pwc_device));
	pdev->type = type_id;
	pwc_construct(pdev);

	init_MUTEX(&pdev->modlock);
	pdev->ptrlock = SPIN_LOCK_UNLOCKED;

	pdev->udev = udev;
	init_waitqueue_head(&pdev->frameq);
	init_waitqueue_head(&pdev->remove_ok);
	pdev->vcompression = pwc_preferred_compression;

	/* Now hook it up to the video subsystem */
	vdev = kmalloc(sizeof(struct video_device), GFP_KERNEL);
	if (vdev == NULL) {
		Err("Oops, could not allocate memory for video_device.\n");
		return NULL;
	}
	memcpy(vdev, &pwc_template, sizeof(pwc_template));
	sprintf(vdev->name, "Philips %d webcam", pdev->type);
	SET_MODULE_OWNER(vdev);
	pdev->vdev = vdev;
	vdev->priv = pdev;

	pdev->release = udev->descriptor.bcdDevice;
	Trace(TRACE_PROBE, "Release: %04x\n", pdev->release);


	/* Now search device_hint[] table for a match, so we can hint a node number. */
	for (hint = 0; hint < MAX_DEV_HINTS; hint++) {
		if (((device_hint[hint].type == -1) || (device_hint[hint].type == pdev->type)) &&
		     (device_hint[hint].pdev == NULL)) {
			/* so far, so good... try serial number */
			if ((device_hint[hint].serial_number[0] == '*') || !strcmp(device_hint[hint].serial_number, serial_number)) {
			    	/* match! */
			    	video_nr = device_hint[hint].device_node;
			    	Trace(TRACE_PROBE, "Found hint, will try to register as /dev/video%d\n", video_nr);
			    	break;
			}
		}
	}

	i = video_register_device(vdev, VFL_TYPE_GRABBER, video_nr);
	if (i < 0) {
		Err("Failed to register as video device (%d).\n", i);
		return NULL;
	}
	else {
		Trace(TRACE_PROBE, "Registered video struct at 0x%p.\n", vdev);
		Info("Registered as /dev/video%d.\n", vdev->minor & 0x3F);
	}
	/* occupy slot */
	if (hint < MAX_DEV_HINTS) 
		device_hint[hint].pdev = pdev;

#if 0
	/* Shut down camera now (some people like the LED off) */
	if (power_save) {
		Trace(TRACE_PROBE, "Powering down camera");
		i = pwc_camera_power(pdev, 0);
		if (i < 0)
			Info("Failed to power-down the camera (%d)\n", i);
	}
#endif

	Trace(TRACE_PROBE, "probe() function returning struct at 0x%p.\n", pdev);
	return pdev;
}

/* The user janked out the cable... */
static void usb_pwc_disconnect(struct usb_device *udev, void *ptr)
{
	struct pwc_device *pdev;
	int hint;

	lock_kernel();
	free_mem_leak();

	pdev = (struct pwc_device *)ptr;
	if (pdev == NULL) {
		Err("pwc_disconnect() Called without private pointer.\n");
		return;
	}
	if (pdev->udev == NULL) {
		Err("pwc_disconnect() already called for %p\n", pdev);
		return;
	}
	if (pdev->udev != udev) {
		Err("pwc_disconnect() Woops: pointer mismatch udev/pdev.\n");
		return;
	}
#ifdef PWC_MAGIC	
	if (pdev->magic != PWC_MAGIC) {
		Err("pwc_disconnect() Magic number failed. Consult your scrolls and try again.\n");
		return;
	}
#endif	
	
	pdev->unplugged = 1;
	if (pdev->vdev != NULL) {
		if (pdev->vopen) {
			Info("Disconnected while device/video is open!\n");
			
			/* Wake up any processes that might be waiting for
			   a frame, let them return an error condition
			 */
			wake_up(&pdev->frameq);
			
			/* Wait until we get a 'go' from _close(). This
			   had a gigantic race condition, since we kfree()
			   stuff here, but we have to wait until close() 
			   is finished. */
			   
			Trace(TRACE_PROBE, "Sleeping on remove_ok.\n");
			sleep_on(&pdev->remove_ok);
			Trace(TRACE_PROBE, "Done sleeping.\n");
			set_mem_leak(pdev->vdev);
			pdev->vdev = NULL;
		}
		else {
			/* Normal disconnect; remove from available devices */
			Trace(TRACE_PROBE, "Unregistering video device normally.\n");
			video_unregister_device(pdev->vdev); 
			kfree(pdev->vdev);
			pdev->vdev = NULL;
		}
	}

	/* search device_hint[] table if we occupy a slot, by any chance */
	for (hint = 0; hint < MAX_DEV_HINTS; hint++)
		if (device_hint[hint].pdev == pdev)
			device_hint[hint].pdev = NULL;

	pdev->udev = NULL;
	unlock_kernel();
	kfree(pdev);
}


/* *grunt* We have to do atoi ourselves :-( */
static int pwc_atoi(char *s)
{
	int k = 0;
	
	k = 0;
	while (*s != '\0' && *s >= '0' && *s <= '9') {
		k = 10 * k + (*s - '0');
		s++;
	}
	return k;
}


/* 
 * Initialization code & module stuff 
 */

static char *size = NULL;
static int fps = 0;
static int fbufs = 0;
static int mbufs = 0;
static int trace = -1;
static int compression = -1;
static int leds[2] = { -1, -1 };
static char *dev_hint[10] = { };

MODULE_PARM(size, "s");
MODULE_PARM_DESC(size, "Initial image size. One of sqcif, qsif, qcif, sif, cif, vga");
MODULE_PARM(fps, "i");
MODULE_PARM_DESC(fps, "Initial frames per second. Varies with model, useful range 5-30");
MODULE_PARM(fbufs, "i");
MODULE_PARM_DESC(fbufs, "Number of internal frame buffers to reserve");
MODULE_PARM(mbufs, "i");
MODULE_PARM_DESC(mbufs, "Number of external (mmap()ed) image buffers");
MODULE_PARM(trace, "i");
MODULE_PARM_DESC(trace, "For debugging purposes");
MODULE_PARM(power_save, "i");
MODULE_PARM_DESC(power_save, "Turn power save feature in camera on or off");
MODULE_PARM(compression, "i");
MODULE_PARM_DESC(compression, "Preferred compression quality. Range 0 (uncompressed) to 3 (high compression)");
MODULE_PARM(leds, "2i");
MODULE_PARM_DESC(leds, "LED on,off time in milliseconds");
MODULE_PARM(dev_hint, "0-10s");
MODULE_PARM_DESC(dev_hint, "Device node hints");

MODULE_DESCRIPTION("Philips USB webcam driver");
MODULE_AUTHOR("Nemosoft Unv. <nemosoft@smcc.demon.nl>");
MODULE_LICENSE("GPL");

static int __init usb_pwc_init(void)
{
	int i, sz;
	char *sizenames[PSZ_MAX] = { "sqcif", "qsif", "qcif", "sif", "cif", "vga" };

	Info("Philips PCA645/646 + PCVC675/680/690 + PCVC730/740/750 webcam module version " PWC_VERSION " loaded.\n");
	Info("Also supports the Askey VC010, Logitech Quickcam 3000 Pro and the Samsung MPC-C10 and MPC-C30.\n");

	if (fps) {
		if (fps < 5 || fps > 30) {
			Err("Framerate out of bounds (5-30).\n");
			return -EINVAL;
		}
		default_fps = fps;
		Info("Default framerate set to %d.\n", default_fps);
	}
	
	if (size) {
		/* string; try matching with array */
		for (sz = 0; sz < PSZ_MAX; sz++) {
			if (!strcmp(sizenames[sz], size)) { /* Found! */
				default_size = sz;
				break;
			}
		}
		if (sz == PSZ_MAX) {
			Err("Size not recognized; try size=[sqcif | qsif | qcif | sif | cif | vga].\n");
			return -EINVAL;
		}
		Info("Default image size set to %s [%dx%d].\n", sizenames[default_size], pwc_image_sizes[default_size].x, pwc_image_sizes[default_size].y);
	}
	if (mbufs) {
		if (mbufs < 1 || mbufs > MAX_IMAGES) {
			Err("Illegal number of mmap() buffers; use a number between 1 and %d.\n", MAX_IMAGES);
			return -EINVAL;
		}
		default_mbufs = mbufs;
		Info("Number of image buffers set to %d.\n", default_mbufs);
	}
	if (fbufs) {
		if (fbufs < 2 || fbufs > MAX_FRAMES) {
			Err("Illegal number of frame buffers; use a number between 2 and %d.\n", MAX_FRAMES);
			return -EINVAL;
		}
		default_fbufs = fbufs;
		Info("Number of frame buffers set to %d.\n", default_fbufs);
	}
	if (trace >= 0) {
		Info("Trace options: 0x%04x\n", trace);
		pwc_trace = trace;
	}
	if (compression >= 0) {
		if (compression > 3) {
			Err("Invalid compression setting; use a number between 0 (uncompressed) and 3 (high).\n");
			return -EINVAL;
		}
		pwc_preferred_compression = compression;
		Info("Preferred compression set to %d.\n", pwc_preferred_compression);
	}
	if (power_save)
		Info("Enabling power save on open/close.\n");
	if (leds[0] >= 0)
		led_on = leds[0] / 100;
	if (leds[1] >= 0)
		led_off = leds[1] / 100;

	/* Big device node whoopla. Basicly, it allows you to assign a 
	   device node (/dev/videoX) to a camera, based on its type 
	   & serial number. The format is [type[.serialnumber]:]node.

           Any camera that isn't matched by these rules gets the next 
           available free device node.
	 */
	for (i = 0; i < MAX_DEV_HINTS; i++) {
		char *s, *colon, *dot;

		/* This loop also initializes the array */
		device_hint[i].pdev = NULL;
		s = dev_hint[i];
		if (s != NULL && *s != '\0') {
			device_hint[i].type = -1; /* wildcard */
			strcpy(device_hint[i].serial_number, "*");

			/* parse string: chop at ':' & '/' */
			colon = dot = s;
			while (*colon != '\0' && *colon != ':')
				colon++;
			while (*dot != '\0' && *dot != '.')
				dot++;
			/* Few sanity checks */
			if (*dot != '\0' && dot > colon) {
				Err("Malformed camera hint: the colon must be after the dot.\n");
				return -EINVAL;
			}

			if (*colon == '\0') {
				/* No colon */
				if (*dot != '\0') {
					Err("Malformed camera hint: no colon + device node given.\n");
					return -EINVAL;
				}
				else {
					/* No type or serial number specified, just a number. */
					device_hint[i].device_node = pwc_atoi(s);
				}
			}
			else {
				/* There's a colon, so we have at least a type and a device node */
				device_hint[i].type = pwc_atoi(s);
				device_hint[i].device_node = pwc_atoi(colon + 1);
				if (*dot != '\0') {
					/* There's a serial number as well */
					int k;
					
					dot++;
					k = 0;
					while (*dot != ':' && k < 29) {
						device_hint[i].serial_number[k++] = *dot;
						dot++;
					}
					device_hint[i].serial_number[k] = '\0';
				}
			}
#ifdef PWC_DEBUG		
			Debug("device_hint[%d]:\n", i);
			Debug("  type    : %d\n", device_hint[i].type);
			Debug("  serial# : %s\n", device_hint[i].serial_number);
			Debug("  node    : %d\n", device_hint[i].device_node);
#endif			
		}
		else
			device_hint[i].type = 0; /* not filled */
	} /* ..for MAX_DEV_HINTS */

	init_MUTEX(&mem_lock);
 	Trace(TRACE_PROBE, "Registering driver at address 0x%p.\n", &pwc_driver);
	return usb_register(&pwc_driver);
}

static void __exit usb_pwc_exit(void)
{
	free_mem_leak();
	Trace(TRACE_MODULE, "Deregistering driver.\n");
	usb_deregister(&pwc_driver);
	Info("Philips webcam module removed.\n");
}

module_init(usb_pwc_init);
module_exit(usb_pwc_exit);

