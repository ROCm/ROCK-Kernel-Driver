/*
 * USB IBM C-It Video Camera driver
 *
 * Supports IBM C-It Video Camera.
 *
 * This driver is based on earlier work of:
 *
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 *
 * 5/24/00 Removed optional (and unnecessary) locking of the driver while
 * the device remains plugged in. Corrected race conditions in ibmcam_open
 * and ibmcam_probe() routines using this as a guideline:
 *
 * (2) The big kernel lock is automatically released when a process sleeps
 *   in the kernel and is automatically reacquired on reschedule if the
 *   process had the lock originally.  Any code that can be compiled as
 *   a module and is entered with the big kernel lock held *MUST*
 *   increment the use count to activate the indirect module protection
 *   before doing anything that might sleep.
 *
 *   In practice, this means that all routines that live in modules and
 *   are invoked under the big kernel lock should do MOD_INC_USE_COUNT
 *   as their very first action.  And all failure paths from that
 *   routine must do MOD_DEC_USE_COUNT before returning.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/videodev.h>
#include <linux/vmalloc.h>
#include <linux/wrapper.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#include <asm/io.h>

#include "ibmcam.h"

#define	ENABLE_HEXDUMP	0	/* Enable if you need it */
static int debug = 0;

/* Completion states of the data parser */
typedef enum {
	scan_Continue,		/* Just parse next item */
	scan_NextFrame,		/* Frame done, send it to V4L */
	scan_Out,		/* Not enough data for frame */
	scan_EndParse		/* End parsing */
} scan_state_t;

/* Bit flags (options) */
#define FLAGS_RETRY_VIDIOCSYNC		(1 << 0)
#define	FLAGS_MONOCHROME		(1 << 1)
#define FLAGS_DISPLAY_HINTS		(1 << 2)
#define FLAGS_OVERLAY_STATS		(1 << 3)
#define FLAGS_FORCE_TESTPATTERN		(1 << 4)
#define FLAGS_SEPARATE_FRAMES		(1 << 5)
#define FLAGS_CLEAN_FRAMES		(1 << 6)

static int flags = 0; /* FLAGS_DISPLAY_HINTS | FLAGS_OVERLAY_STATS; */

/* This is the size of V4L frame that we provide */
static const int imgwidth = V4L_FRAME_WIDTH_USED;
static const int imgheight = V4L_FRAME_HEIGHT;
static const int min_imgwidth  = 8;
static const int min_imgheight = 4;

static int lighting = 1; /* Medium */

#define SHARPNESS_MIN	0
#define SHARPNESS_MAX	6
static int sharpness = 4; /* Low noise, good details */

#define FRAMERATE_MIN	0
#define FRAMERATE_MAX	6
static int framerate = 2; /* Lower, reliable frame rate (8-12 fps) */

enum {
	VIDEOSIZE_128x96 = 0,
	VIDEOSIZE_176x144,
	VIDEOSIZE_352x288,
	VIDEOSIZE_320x240,
	VIDEOSIZE_352x240,
};

static int videosize = VIDEOSIZE_352x288;

/*
 * The value of 'scratchbufsize' affects quality of the picture
 * in many ways. Shorter buffers may cause loss of data when client
 * is too slow. Larger buffers are memory-consuming and take longer
 * to work with. This setting can be adjusted, but the default value
 * should be OK for most desktop users.
 */
#define DEFAULT_SCRATCH_BUF_SIZE	(0x10000)	/* 64 KB */
static const int scratchbufsize = DEFAULT_SCRATCH_BUF_SIZE;

/*
 * Here we define several initialization variables. They may
 * be used to automatically set color, hue, brightness and
 * contrast to desired values. This is particularly useful in
 * case of webcams (which have no controls and no on-screen
 * output) and also when a client V4L software is used that
 * does not have some of those controls. In any case it's
 * good to have startup values as options.
 *
 * These values are all in [0..255] range. This simplifies
 * operation. Note that actual values of V4L variables may
 * be scaled up (as much as << 8). User can see that only
 * on overlay output, however, or through a V4L client.
 */
static int init_brightness = 128;
static int init_contrast = 192;
static int init_color = 128;
static int init_hue = 128;
static int hue_correction = 128;

/* Settings for camera model 2 */
static int init_model2_rg = -1;
static int init_model2_rg2 = -1;
static int init_model2_sat = -1;
static int init_model2_yb = -1;

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug level: 0-9 (default=0)");
MODULE_PARM(flags, "i");
MODULE_PARM_DESC(flags, "Bitfield: 0=VIDIOCSYNC, 1=B/W, 2=show hints, 3=show stats, 4=test pattern, 5=seperate frames, 6=clean frames");
MODULE_PARM(framerate, "i");
MODULE_PARM_DESC(framerate, "Framerate setting: 0=slowest, 6=fastest (default=2)");
MODULE_PARM(lighting, "i");
MODULE_PARM_DESC(lighting, "Photosensitivity: 0=bright, 1=medium (default), 2=low light");
MODULE_PARM(sharpness, "i");
MODULE_PARM_DESC(sharpness, "Model1 noise reduction: 0=smooth, 6=sharp (default=4)");
MODULE_PARM(videosize, "i");
MODULE_PARM_DESC(videosize, "Image size: 0=128x96, 1=176x144, 2=352x288, 3=320x240, 4=352x240 (default=1)");
MODULE_PARM(init_brightness, "i");
MODULE_PARM_DESC(init_brightness, "Brightness preconfiguration: 0-255 (default=128)");
MODULE_PARM(init_contrast, "i");
MODULE_PARM_DESC(init_contrast, "Contrast preconfiguration: 0-255 (default=192)");
MODULE_PARM(init_color, "i");
MODULE_PARM_DESC(init_color, "Dolor preconfiguration: 0-255 (default=128)");
MODULE_PARM(init_hue, "i");
MODULE_PARM_DESC(init_hue, "Hue preconfiguration: 0-255 (default=128)");
MODULE_PARM(hue_correction, "i");
MODULE_PARM_DESC(hue_correction, "YUV colorspace regulation: 0-255 (default=128)");

MODULE_PARM(init_model2_rg, "i");
MODULE_PARM_DESC(init_model2_rg, "Model2 preconfiguration: 0-255 (default=112)");
MODULE_PARM(init_model2_rg2, "i");
MODULE_PARM_DESC(init_model2_rg2, "Model2 preconfiguration: 0-255 (default=47)");
MODULE_PARM(init_model2_sat, "i");
MODULE_PARM_DESC(init_model2_sat, "Model2 preconfiguration: 0-255 (default=52)");
MODULE_PARM(init_model2_yb, "i");
MODULE_PARM_DESC(init_model2_yb, "Model2 preconfiguration: 0-255 (default=160)");

MODULE_AUTHOR ("module author");
MODULE_DESCRIPTION ("IBM/Xirlink C-it USB Camera Driver for Linux (c) 2000");

/* Still mysterious i2c commands */
static const unsigned short unknown_88 = 0x0088;
static const unsigned short unknown_89 = 0x0089;
static const unsigned short bright_3x[3] = { 0x0031, 0x0032, 0x0033 };
static const unsigned short contrast_14 = 0x0014;
static const unsigned short light_27 = 0x0027;
static const unsigned short sharp_13 = 0x0013;

/* i2c commands for Model 2 cameras */
static const unsigned short mod2_brightness = 0x001a;		/* $5b .. $ee; default=$5a */
static const unsigned short mod2_set_framerate = 0x001c;	/* 0 (fast).. $1F (slow) */
static const unsigned short mod2_color_balance_rg2 = 0x001e;	/* 0 (red) .. $7F (green) */
static const unsigned short mod2_saturation = 0x0020;		/* 0 (b/w) - $7F (full color) */
static const unsigned short mod2_color_balance_yb = 0x0022;	/* 0..$7F, $50 is about right */
static const unsigned short mod2_color_balance_rg = 0x0024;	/* 0..$7F, $70 is about right */
static const unsigned short mod2_sensitivity = 0x0028;		/* 0 (min) .. $1F (max) */

#define MAX_IBMCAM	4

struct usb_ibmcam cams[MAX_IBMCAM];

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

static struct usb_driver ibmcam_driver;
static void usb_ibmcam_release(struct usb_ibmcam *ibmcam);

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
			if (pte_present(pte)) {
				ret = (unsigned long) page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
			}
		}
	}
	MDEBUG(printk("uv2kva(%lx-->%lx)", adr, ret));
	return ret;
}

static inline unsigned long uvirt_to_bus(unsigned long adr)
{
	unsigned long kva, ret;

	kva = uvirt_to_kva(pgd_offset(current->mm, adr), adr);
	ret = virt_to_bus((void *)kva);
	MDEBUG(printk("uv2b(%lx-->%lx)", adr, ret));
	return ret;
}

static inline unsigned long kvirt_to_bus(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = virt_to_bus((void *)kva);
	MDEBUG(printk("kv2b(%lx-->%lx)", adr, ret));
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
	MDEBUG(printk("kv2pa(%lx-->%lx)", adr, ret));
	return ret;
}

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr, page;

	/* Round it off to PAGE_SIZE */
	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		page = kvirt_to_pa(adr);
		mem_map_reserve(virt_to_page(__va(page)));
		adr += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr, page;

	if (!mem)
		return;

	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	adr=(unsigned long) mem;
	while (size > 0) {
		page = kvirt_to_pa(adr);
		mem_map_unreserve(virt_to_page(__va(page)));
		adr += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	vfree(mem);
}

#if ENABLE_HEXDUMP
static void ibmcam_hexdump(const unsigned char *data, int len)
{
	char tmp[80];
	int i, k;

	for (i=k=0; len > 0; i++, len--) {
		if (i > 0 && (i%16 == 0)) {
			printk("%s\n", tmp);
			k=0;
		}
		k += sprintf(&tmp[k], "%02x ", data[i]);
	}
	if (k > 0)
		printk("%s\n", tmp);
}
#endif

/*
 * usb_ibmcam_overlaychar()
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_overlaychar(
	struct usb_ibmcam *ibmcam,
	struct ibmcam_frame *frame,
	int x, int y, int ch)
{
	static const unsigned short digits[16] = {
		0xF6DE, /* 0 */
		0x2492, /* 1 */
		0xE7CE, /* 2 */
		0xE79E, /* 3 */
		0xB792, /* 4 */
		0xF39E, /* 5 */
		0xF3DE, /* 6 */
		0xF492, /* 7 */
		0xF7DE, /* 8 */
		0xF79E, /* 9 */
		0x77DA, /* a */
		0xD75C, /* b */
		0xF24E, /* c */
		0xD6DC, /* d */
		0xF34E, /* e */
		0xF348  /* f */
	};
	unsigned short digit;
	int ix, iy;

	if ((ibmcam == NULL) || (frame == NULL))
		return;

	if (ch >= '0' && ch <= '9')
		ch -= '0';
	else if (ch >= 'A' && ch <= 'F')
		ch = 10 + (ch - 'A');
	else if (ch >= 'a' && ch <= 'f')
		ch = 10 + (ch - 'a');
	else
		return;
	digit = digits[ch];

	for (iy=0; iy < 5; iy++) {
		for (ix=0; ix < 3; ix++) {
			if (digit & 0x8000) {
				IBMCAM_PUTPIXEL(frame, x+ix, y+iy, 0xFF, 0xFF, 0xFF);
			}
			digit = digit << 1;
		}
	}
}

/*
 * usb_ibmcam_overlaystring()
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_overlaystring(
	struct usb_ibmcam *ibmcam,
	struct ibmcam_frame *frame,
	int x, int y, const char *str)
{
	while (*str) {
		usb_ibmcam_overlaychar(ibmcam, frame, x, y, *str);
		str++;
		x += 4; /* 3 pixels character + 1 space */
	}
}

/*
 * usb_ibmcam_overlaystats()
 *
 * Overlays important debugging information.
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_overlaystats(struct usb_ibmcam *ibmcam, struct ibmcam_frame *frame)
{
	const int y_diff = 8;
	char tmp[16];
	int x = 10;
	int y = 10;

	sprintf(tmp, "%8x", ibmcam->frame_num);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->urb_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->urb_length);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->data_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->header_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->scratch_ovf_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->iso_skip_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->iso_err_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.colour);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.hue);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.brightness >> 8);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.contrast >> 12);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8d", ibmcam->vpic.whiteness >> 8);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;
}

/*
 * usb_ibmcam_testpattern()
 *
 * Procedure forms a test pattern (yellow grid on blue background).
 *
 * Parameters:
 * fullframe:   if TRUE then entire frame is filled, otherwise the procedure
 *	      continues from the current scanline.
 * pmode	0: fill the frame with solid blue color (like on VCR or TV)
 *	      1: Draw a colored grid
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_testpattern(struct usb_ibmcam *ibmcam, int fullframe, int pmode)
{
	static const char proc[] = "usb_ibmcam_testpattern";
	struct ibmcam_frame *frame;
	unsigned char *f;
	int num_cell = 0;
	int scan_length = 0;
	static int num_pass = 0;

	if (ibmcam == NULL) {
		printk(KERN_ERR "%s: ibmcam == NULL\n", proc);
		return;
	}
	if ((ibmcam->curframe < 0) || (ibmcam->curframe >= IBMCAM_NUMFRAMES)) {
		printk(KERN_ERR "%s: ibmcam->curframe=%d.\n", proc, ibmcam->curframe);
		return;
	}

	/* Grab the current frame */
	frame = &ibmcam->frame[ibmcam->curframe];

	/* Optionally start at the beginning */
	if (fullframe) {
		frame->curline = 0;
		frame->scanlength = 0;
	}

	/* Form every scan line */
	for (; frame->curline < imgheight; frame->curline++) {
		int i;

		f = frame->data + (imgwidth * 3 * frame->curline);
		for (i=0; i < imgwidth; i++) {
			unsigned char cb=0x80;
			unsigned char cg = 0;
			unsigned char cr = 0;

			if (pmode == 1) {
				if (frame->curline % 32 == 0)
					cb = 0, cg = cr = 0xFF;
				else if (i % 32 == 0) {
					if (frame->curline % 32 == 1)
						num_cell++;
					cb = 0, cg = cr = 0xFF;
				} else {
					cb = ((num_cell*7) + num_pass) & 0xFF;
					cg = ((num_cell*5) + num_pass*2) & 0xFF;
					cr = ((num_cell*3) + num_pass*3) & 0xFF;
				}
			} else {
				/* Just the blue screen */
			}
				
			*f++ = cb;
			*f++ = cg;
			*f++ = cr;
			scan_length += 3;
		}
	}

	frame->grabstate = FRAME_DONE;
	frame->scanlength += scan_length;
	++num_pass;

	/* We do this unconditionally, regardless of FLAGS_OVERLAY_STATS */
	usb_ibmcam_overlaystats(ibmcam, frame);
}

static unsigned char *ibmcam_model1_find_header(unsigned char hdr_sig, unsigned char *data, int len)
{
	while (len >= 4)
	{
		if ((data[0] == 0x00) && (data[1] == 0xFF) && (data[2] == 0x00))
		{
#if 0
			/* This code helps to detect new frame markers */
			printk(KERN_DEBUG "Header sig: 00 FF 00 %02X\n", data[3]);
#endif
			if (data[3] == hdr_sig) {
				if (debug > 2)
					printk(KERN_DEBUG "Header found.\n");
				return data+4;
			}
		}
		++data;
		--len;
	}
	return NULL;
}

static unsigned char *ibmcam_model2_find_header(unsigned char hdr_sig, unsigned char *data, int len)
{
	int marker_len = 0;

	switch (videosize) {
	case VIDEOSIZE_176x144:
		marker_len = 10;
		break;
	default:
		marker_len = 2;
		break;
	}
	while (len >= marker_len)
	{
		if ((data[0] == 0x00) && (data[1] == 0xFF))
		{
#if 0
			/* This code helps to detect new frame markers */
			static int pass = 0;
			if (pass++ == 0)
				ibmcam_hexdump(data, (len > 16) ? 16 : len);
#endif
			if (debug > 2)
				printk(KERN_DEBUG "Header found.\n");
			return data+marker_len;
		}
		++data;
		--len;
	}
	return NULL;
}

/* How much data is left in the scratch buf? */
#define scratch_left(x)	(ibmcam->scratchlen - (int)((char *)x - (char *)ibmcam->scratch))

/* Grab the remaining */
static void usb_ibmcam_align_scratch(struct usb_ibmcam *ibmcam, unsigned char *data)
{
	unsigned long left;

	left = scratch_left(data);
	memmove(ibmcam->scratch, data, left);
	ibmcam->scratchlen = left;
}

/*
 * usb_ibmcam_find_header()
 *
 * Locate one of supported header markers in the scratch buffer.
 * Once found, remove all preceding bytes AND the marker (4 bytes)
 * from the scratch buffer. Whatever follows must be video lines.
 *
 * History:
 * 1/21/00  Created.
 */
static scan_state_t usb_ibmcam_find_header(struct usb_ibmcam *ibmcam)
{
	struct ibmcam_frame *frame;
	unsigned char *data, *tmp;

	data = ibmcam->scratch;
	frame = &ibmcam->frame[ibmcam->curframe];

	if (ibmcam->camera_model == IBMCAM_MODEL_1)
		tmp = ibmcam_model1_find_header(frame->hdr_sig, data, scratch_left(data));
	else if (ibmcam->camera_model == IBMCAM_MODEL_2)
		tmp = ibmcam_model2_find_header(frame->hdr_sig, data, scratch_left(data));
	else
		tmp = NULL;

	if (tmp == NULL) {
		/* No header - entire scratch buffer is useless! */
		if (debug > 2)
			printk(KERN_DEBUG "Skipping frame, no header\n");
		ibmcam->scratchlen = 0;
		return scan_EndParse;
	}
	/* Header found */
	data = tmp;

	ibmcam->has_hdr = 1;
	ibmcam->header_count++;
	frame->scanstate = STATE_LINES;
	frame->curline = 0;

	if (flags & FLAGS_FORCE_TESTPATTERN) {
		usb_ibmcam_testpattern(ibmcam, 1, 1);
		return scan_NextFrame;
	}
	usb_ibmcam_align_scratch(ibmcam, data);
	return scan_Continue;
}

/*
 * usb_ibmcam_parse_lines()
 *
 * Parse one line (TODO: more than one!) from the scratch buffer, put
 * decoded RGB value into the current frame buffer and add the written
 * number of bytes (RGB) to the *pcopylen.
 *
 * History:
 * 1/21/00  Created.
 */
static scan_state_t usb_ibmcam_parse_lines(struct usb_ibmcam *ibmcam, long *pcopylen)
{
	struct ibmcam_frame *frame;
	unsigned char *data, *f, *chromaLine;
	unsigned int len;
	const int v4l_linesize = imgwidth * V4L_BYTES_PER_PIXEL;	/* V4L line offset */
	const int hue_corr  = (ibmcam->vpic.hue - 0x8000) >> 10;	/* -32..+31 */
	const int hue2_corr = (hue_correction - 128) / 4;		/* -32..+31 */
	const int ccm = 128; /* Color correction median - see below */
	int y, u, v, i, frame_done=0, mono_plane, color_corr;

	color_corr = (ibmcam->vpic.colour - 0x8000) >> 8; /* -128..+127 = -ccm..+(ccm-1)*/
	RESTRICT_TO_RANGE(color_corr, -ccm, ccm+1);
	data = ibmcam->scratch;
	frame = &ibmcam->frame[ibmcam->curframe];

	len = frame->frmwidth * 3; /* 1 line of mono + 1 line of color */
	/*printk(KERN_DEBUG "len=%d. left=%d.\n",len,scratch_left(data));*/

	mono_plane = ((frame->curline & 1) == 0);

	/*
	 * Lines are organized this way (or are they?)
	 *
	 * I420:
	 * ~~~~
	 * ___________________________________
	 * |-----Y-----|---UVUVUV...UVUV-----| \
	 * |-----------+---------------------|  \
	 * |<-- 176 -->|<------ 176*2 ------>|  Total 72. pairs of lines
	 * |...	   ...	     ...|  /
	 * |___________|_____________________| /
	 *  - odd line- ------- even line ---
	 *
	 * another format:
	 * ~~~~~~~~~~~~~~
	 * ___________________________________
	 * |-----Y-----|---UVUVUV...UVUV-----| \
	 * |-----------+---------------------|  \
	 * |<-- 352 -->|<------ 352*2 ------>|  Total 144. pairs of lines
	 * |...	   ...	     ...|  /
	 * |___________|_____________________| /
	 *  - odd line- ------- even line ---
	 */

	/* Make sure there's enough data for the entire line */
	if (scratch_left(data) < (len+1024)) {
		/*printk(KERN_DEBUG "out of data, need %u.\n", len);*/
		return scan_Out;
	}

#if 0
	{	/* This code prints beginning of the source frame */
		static int pass = 0;
		if ((pass++ % 3000) == 0)
			ibmcam_hexdump(data, 16);
	}
#endif

#if 0
	if (frame->curline == 10 || frame->curline == 11) {
		/* This code prints beginning of 10th (mono), 11th (chroma) line */
		static int pass = 0;
		if ((pass % 100) == 0)
			ibmcam_hexdump(data, 16);
		if (frame->curline == 11)
			pass++;
	}
#endif
	/*
	 * Make sure that our writing into output buffer
	 * will not exceed the buffer. Mind that we may write
	 * not into current output scanline but in several after
	 * it as well (if we enlarge image vertically.)
	 */
	if ((frame->curline + 1) >= V4L_FRAME_HEIGHT)
		return scan_NextFrame;

	/*
	 * Now we are sure that entire line (representing all 'frame->frmwidth'
	 * pixels from the camera) is available in the scratch buffer. We
	 * start copying the line left-aligned to the V4L buffer (which
	 * might be larger - not smaller, hopefully). If the camera
	 * line is shorter then we should pad the V4L buffer with something
	 * (black in this case) to complete the line.
	 */
	f = frame->data + (v4l_linesize * frame->curline);

	/*
	 * chromaLine points to 1st pixel of the line with chrominance.
	 * If current line is monochrome then chromaLine points to next
	 * line after monochrome one. If current line has chrominance
	 * then chromaLine points to this very line. Such method allows
	 * to access chrominance data uniformly.
	 *
	 * To obtain chrominance data from the 'chromaLine' use this:
	 *   v = chromaLine[0]; // 0-1:[0], 2-3:[4], 4-5:[8]...
	 *   u = chromaLine[2]; // 0-1:[2], 2-3:[6], 4-5:[10]...
	 *
	 * Indices must be calculated this way:
	 * v_index = (i >> 1) << 2;
	 * u_index = (i >> 1) << 2 + 2;
	 *
	 * where 'i' is the column number [0..frame->frmwidth-1]
	 */
	chromaLine = data;
	if (mono_plane)
		chromaLine += frame->frmwidth;

	for (i = 0; i < frame->frmwidth; i++, data += (mono_plane ? 1 : 2))
	{
		unsigned char rv, gv, bv;	/* RGB components */

		/*
		 * Search for potential Start-Of-Frame marker. It should
		 * not be here, of course, but if your formats don't match
		 * you might exceed the frame. We must match the marker to
		 * each byte of multi-byte data element if it is multi-byte.
		 */
#if 1
		if ((ibmcam->camera_model == IBMCAM_MODEL_1) && (scratch_left(data) >= (4+2))) {
			unsigned char *dp;
			int j;

			for (j=0, dp=data; j < 2; j++, dp++) {
				if ((dp[0] == 0x00) && (dp[1] == 0xFF) &&
				    (dp[2] == 0x00) && (dp[3] == frame->hdr_sig)) {
					ibmcam->has_hdr = 2;
					frame_done++;
					break;
				}
			}
		}
#endif

		/* Check for various visual debugging hints (colorized pixels) */
		if ((flags & FLAGS_DISPLAY_HINTS) && (ibmcam->has_hdr)) {
			/*
			 * This is bad and should not happen. This means that
			 * we somehow overshoot the line and encountered new
			 * frame! Obviously our camera/V4L frame size is out
			 * of whack. This cyan dot will help you to figure
			 * out where exactly the new frame arrived.
			 */
			if (ibmcam->has_hdr == 1) {
				bv = 0; /* Yellow marker */
				gv = 0xFF;
				rv = 0xFF;
			} else {
				bv = 0xFF; /* Cyan marker */
				gv = 0xFF;
				rv = 0;
			}
			ibmcam->has_hdr = 0;
			goto make_pixel;
		}

		if (mono_plane || frame->order_yc)
			y = data[0];
		else
			y = data[1];

		if (flags & FLAGS_MONOCHROME) /* Use monochrome for debugging */
			rv = gv = bv = y;
		else {
			int off_0, off_2;

			off_0 = (i >> 1) << 2;
			off_2 = off_0 + 2;

			if (frame->order_yc) {
				off_0++;
				off_2++;
			}
			if (!frame->order_uv) {
				off_0 += 2;
				off_2 -= 2;
			}
			u = chromaLine[off_0] + hue_corr;
			v = chromaLine[off_2] + hue2_corr;

			/* Apply color correction */
			if (color_corr != 0) {
				/* Magnify up to 2 times, reduce down to zero saturation */
				u = 128 + ((ccm + color_corr) * (u - 128)) / ccm;
				v = 128 + ((ccm + color_corr) * (v - 128)) / ccm;
			}
			YUV_TO_RGB_BY_THE_BOOK(y, u, v, rv, gv, bv);
		}

	make_pixel:
		/*
		 * The purpose of creating the pixel here, in one,
		 * dedicated place is that we may need to make the
		 * pixel wider and taller than it actually is. This
		 * may be used if camera generates small frames for
		 * sake of frame rate (or any other reason.)
		 *
		 * The output data consists of B, G, R bytes
		 * (in this order).
		 */
#if USES_IBMCAM_PUTPIXEL
		IBMCAM_PUTPIXEL(frame, i, frame->curline, rv, gv, bv);
#else
		*f++ = bv;
		*f++ = gv;
		*f++ = rv;
#endif
		/*
		 * Typically we do not decide within a legitimate frame
		 * that we want to end the frame. However debugging code
		 * may detect marker of new frame within the data. Then
		 * this condition activates. The 'data' pointer is already
		 * pointing at the new marker, so we'd better leave it as is.
		 */
		if (frame_done)
			break;	/* End scanning of lines */
	}
	/*
	 * Account for number of bytes that we wrote into output V4L frame.
	 * We do it here, after we are done with the scanline, because we
	 * may fill more than one output scanline if we do vertical
	 * enlargement.
	 */
	frame->curline++;
	*pcopylen += v4l_linesize;
	usb_ibmcam_align_scratch(ibmcam, data);

	if (frame_done || (frame->curline >= frame->frmheight))
		return scan_NextFrame;
	else
		return scan_Continue;
}

/*
 * usb_ibmcam_model2_parse_lines()
 *
 * This procedure deals with a weird RGB format that is produced by IBM
 * camera model 2 in modes 320x240 and above; 'x' below is 159 or 175,
 * depending on horizontal size of the picture:
 *
 * <--- 160 or 176 pairs of RA,RB bytes ----->
 * *-----------------------------------------* \
 * | RA0 | RB0 | RA1 | RB1 | ... | RAx | RBx |  \
 * |-----+-----+-----+-----+ ... +-----+-----|   *- This is pair of horizontal lines,
 * | B0  | G0  | B1  | G1  | ... | Bx  | Gx  |  /   total 240 or 288 lines (120 or 144
 * |=====+=====+=====+=====+ ... +=====+=====| /    such pairs).
 *
 * Each group of FOUR bytes (RAi, RBi, Bi, Gi) where i=0..frame_width/2-1
 * defines ONE pixel. Therefore this format yields 176x144 "decoded"
 * resolution at best. I do not know why camera sends such format - the
 * previous model just used I420 and everyone was happy.
 *
 * I do not know what is the difference between RAi and RBi bytes. Both
 * seemingly represent R component, but slightly vary in value (so that
 * the picture looks a bit colored if one or another is used). I use
 * them both as R component in attempt to at least partially recover the
 * lost resolution.
 */
static scan_state_t usb_ibmcam_model2_parse_lines(struct usb_ibmcam *ibmcam, long *pcopylen)
{
	struct ibmcam_frame *frame;
	unsigned char *data, *f, *la, *lb;
	unsigned int len;
	const int v4l_linesize = imgwidth * V4L_BYTES_PER_PIXEL;	/* V4L line offset */
	int i, j, frame_done=0, color_corr;

	color_corr = (ibmcam->vpic.colour) >> 8; /* 0..+255 */

	data = ibmcam->scratch;
	frame = &ibmcam->frame[ibmcam->curframe];

	/* Here we deal with pairs of horizontal lines */

	len = frame->frmwidth * 2; /* 2 lines */
	/*printk(KERN_DEBUG "len=%d. left=%d.\n",len,scratch_left(data));*/

	/* Make sure there's enough data for the entire line */
	if (scratch_left(data) < (len+32)) {
		/*printk(KERN_DEBUG "out of data, need %u.\n", len);*/
		return scan_Out;
	}

	/*
	 * Make sure that our writing into output buffer
	 * will not exceed the buffer. Mind that we may write
	 * not into current output scanline but in several after
	 * it as well (if we enlarge image vertically.)
	 */
	if ((frame->curline + 1) >= V4L_FRAME_HEIGHT)
		return scan_NextFrame;

	if ((frame->curline & 1) == 0) {
		la = data;
		lb = data + frame->frmwidth;
	} else {
		la = data + frame->frmwidth;
		lb = data;
	}

	/*
	 * Now we are sure that entire line (representing all 'frame->frmwidth'
	 * pixels from the camera) is available in the scratch buffer. We
	 * start copying the line left-aligned to the V4L buffer (which
	 * might be larger - not smaller, hopefully). If the camera
	 * line is shorter then we should pad the V4L buffer with something
	 * (black in this case) to complete the line.
	 */
	f = frame->data + (v4l_linesize * frame->curline);

	/* Fill the 2-line strip */
	for (i = 0; i < frame->frmwidth; i++) {
		int y, rv, gv, bv;	/* RGB components */

		j = i & (~1);

		/* Check for various visual debugging hints (colorized pixels) */
		if ((flags & FLAGS_DISPLAY_HINTS) && (ibmcam->has_hdr)) {
			if (ibmcam->has_hdr == 1) {
				bv = 0; /* Yellow marker */
				gv = 0xFF;
				rv = 0xFF;
			} else {
				bv = 0xFF; /* Cyan marker */
				gv = 0xFF;
				rv = 0;
			}
			ibmcam->has_hdr = 0;
			goto make_pixel;
		}

		/*
		 * Here I use RA and RB components, one per physical pixel.
		 * This causes fine vertical grid on the picture but may improve
		 * horizontal resolution. If you prefer replicating, use this:
		 *   rv = la[j + 0];   ... or ... rv = la[j + 1];
		 * then the pixel will be replicated.
		 */
		rv = la[i];
		gv = lb[j + 1];
		bv = lb[j + 0];

		y = (rv + gv + bv) / 3; /* Brightness (badly calculated) */

		if (flags & FLAGS_MONOCHROME) /* Use monochrome for debugging */
			rv = gv = bv = y;
		else if (color_corr != 128) {

			/* Calculate difference between color and brightness */
			rv -= y;
			gv -= y;
			bv -= y;

			/* Scale differences */
			rv = (rv * color_corr) / 128;
			gv = (gv * color_corr) / 128;
			bv = (bv * color_corr) / 128;

			/* Reapply brightness */
			rv += y;
			gv += y;
			bv += y;

			/* Watch for overflows */
			RESTRICT_TO_RANGE(rv, 0, 255);
			RESTRICT_TO_RANGE(gv, 0, 255);
			RESTRICT_TO_RANGE(bv, 0, 255);
		}

	make_pixel:
		IBMCAM_PUTPIXEL(frame, i, frame->curline, rv, gv, bv);
		IBMCAM_PUTPIXEL(frame, i, frame->curline+1, rv, gv, bv);
	}
	/*
	 * Account for number of bytes that we wrote into output V4L frame.
	 * We do it here, after we are done with the scanline, because we
	 * may fill more than one output scanline if we do vertical
	 * enlargement.
	 */
	frame->curline += 2;
	*pcopylen += v4l_linesize * 2;
	data += frame->frmwidth * 2;
	usb_ibmcam_align_scratch(ibmcam, data);

	if (frame_done || (frame->curline >= frame->frmheight))
		return scan_NextFrame;
	else
		return scan_Continue;
}

/*
 * ibmcam_parse_data()
 *
 * Generic routine to parse the scratch buffer. It employs either
 * usb_ibmcam_find_header() or usb_ibmcam_parse_lines() to do most
 * of work.
 *
 * History:
 * 1/21/00  Created.
 */
static void ibmcam_parse_data(struct usb_ibmcam *ibmcam)
{
	struct ibmcam_frame *frame;
	unsigned char *data = ibmcam->scratch;
	scan_state_t newstate;
	long copylen = 0;

	/* Grab the current frame and the previous frame */
	frame = &ibmcam->frame[ibmcam->curframe];

	/* printk(KERN_DEBUG "parsing %u.\n", ibmcam->scratchlen); */

	while (1) {

		newstate = scan_Out;
		if (scratch_left(data)) {
			if (frame->scanstate == STATE_SCANNING)
				newstate = usb_ibmcam_find_header(ibmcam);
			else if (frame->scanstate == STATE_LINES) {
				if ((ibmcam->camera_model == IBMCAM_MODEL_2) &&
				    (videosize >= VIDEOSIZE_352x288)) {
					newstate = usb_ibmcam_model2_parse_lines(ibmcam, &copylen);
				}
				else {
					newstate = usb_ibmcam_parse_lines(ibmcam, &copylen);
				}
			}
		}
		if (newstate == scan_Continue)
			continue;
		else if ((newstate == scan_NextFrame) || (newstate == scan_Out))
			break;
		else
			return; /* scan_EndParse */
	}

	if (newstate == scan_NextFrame) {
		frame->grabstate = FRAME_DONE;
		ibmcam->curframe = -1;
		ibmcam->frame_num++;

		/* Optionally display statistics on the screen */
		if (flags & FLAGS_OVERLAY_STATS)
			usb_ibmcam_overlaystats(ibmcam, frame);

		/* This will cause the process to request another frame. */
		if (waitqueue_active(&frame->wq))
			wake_up_interruptible(&frame->wq);
	}

	/* Update the frame's uncompressed length. */
	frame->scanlength += copylen;
}

/*
 * Make all of the blocks of data contiguous
 */
static int ibmcam_compress_isochronous(struct usb_ibmcam *ibmcam, urb_t *urb)
{
	unsigned char *cdata, *data, *data0;
	int i, totlen = 0;

	data = data0 = ibmcam->scratch + ibmcam->scratchlen;
	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;
		
		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (st < 0) {
			if (debug >= 1) {
				printk(KERN_ERR "ibmcam data error: [%d] len=%d, status=%X\n",
				       i, n, st);
			}
			ibmcam->iso_err_count++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (n <= 0) {
			ibmcam->iso_skip_count++;
			continue;
		}

		/*
		 * If camera continues to feed us with data but there is no
		 * consumption (if, for example, V4L client fell asleep) we
		 * may overflow the buffer. We have to move old data over to
		 * free room for new data. This is bad for old data. If we
		 * just drop new data then it's bad for new data... choose
		 * your favorite evil here.
		 */
		if ((ibmcam->scratchlen + n) > scratchbufsize) {
#if 0
			ibmcam->scratch_ovf_count++;
			if (debug >= 3)
				printk(KERN_ERR "ibmcam: scratch buf overflow! "
				       "scr_len: %d, n: %d\n", ibmcam->scratchlen, n );
			return totlen;
#else
			int mv;

			ibmcam->scratch_ovf_count++;
			if (debug >= 3) {
				printk(KERN_ERR "ibmcam: scratch buf overflow! "
				       "scr_len: %d, n: %d\n", ibmcam->scratchlen, n );
			}
			mv  = (ibmcam->scratchlen + n) - scratchbufsize;
			if (ibmcam->scratchlen >= mv) {
				int newslen = ibmcam->scratchlen - mv;
				memmove(ibmcam->scratch, ibmcam->scratch + mv, newslen);
				ibmcam->scratchlen = newslen;
				data = data0 = ibmcam->scratch + ibmcam->scratchlen;
			} else {
				printk(KERN_ERR "ibmcam: scratch buf too small\n");
				return totlen;
			}
#endif
		}

		/* Now we know that there is enough room in scratch buffer */
		memmove(data, cdata, n);
		data += n;
		totlen += n;
		ibmcam->scratchlen += n;
	}
#if 0
	if (totlen > 0) {
		static int foo=0;
		if (foo < 1) {
			printk(KERN_DEBUG "+%d.\n", totlen);
			ibmcam_hexdump(data0, (totlen > 64) ? 64:totlen);
			++foo;
		}
	}
#endif
	return totlen;
}

static void ibmcam_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_ibmcam *ibmcam = urb->context;
	struct ibmcam_sbuf *sbuf;
	int i;

	/* We don't want to do anything if we are about to be removed! */
	if (!IBMCAM_IS_OPERATIONAL(ibmcam))
		return;

#if 0
	if (urb->actual_length > 0) {
		printk(KERN_DEBUG "ibmcam_isoc_irq: %p status %d, "
		       " errcount = %d, length = %d\n", urb, urb->status,
		       urb->error_count, urb->actual_length);
	} else {
		static int c = 0;
		if (c++ % 100 == 0)
			printk(KERN_DEBUG "ibmcam_isoc_irq: no data\n");
	}
#endif

	if (!ibmcam->streaming) {
		if (debug >= 1)
			printk(KERN_DEBUG "ibmcam: oops, not streaming, but interrupt\n");
		return;
	}
	
	sbuf = &ibmcam->sbuf[ibmcam->cursbuf];

	/* Copy the data received into our scratch buffer */
	len = ibmcam_compress_isochronous(ibmcam, urb);

	ibmcam->urb_count++;
	ibmcam->urb_length = len;
	ibmcam->data_count += len;

#if 0   /* This code prints few initial bytes of ISO data: used to decode markers */
	if (ibmcam->urb_count % 64 == 1) {
		if (ibmcam->urb_count == 1) {
			ibmcam_hexdump(ibmcam->scratch,
				       (ibmcam->scratchlen > 32) ? 32 : ibmcam->scratchlen);
		}
	}
#endif

	/* If we collected enough data let's parse! */
	if (ibmcam->scratchlen) {
		/* If we don't have a frame we're current working on, complain */
		if (ibmcam->curframe >= 0)
			ibmcam_parse_data(ibmcam);
		else {
			if (debug >= 1)
				printk(KERN_DEBUG "ibmcam: received data, but no frame available\n");
		}
	}

	for (i = 0; i < FRAMES_PER_DESC; i++) {
		sbuf->urb->iso_frame_desc[i].status = 0;
		sbuf->urb->iso_frame_desc[i].actual_length = 0;
	}

	/* Move to the next sbuf */
	ibmcam->cursbuf = (ibmcam->cursbuf + 1) % IBMCAM_NUMSBUF;

	return;
}

/*
 * usb_ibmcam_veio()
 *
 * History:
 * 1/27/00  Added check for dev == NULL; this happens if camera is unplugged.
 */
static int usb_ibmcam_veio(
	struct usb_ibmcam *ibmcam,
	unsigned char req,
	unsigned short value,
	unsigned short index)
{
	static const char proc[] = "usb_ibmcam_veio";
	unsigned char cp[8] /* = { 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef } */;
	int i;

	if (!IBMCAM_IS_OPERATIONAL(ibmcam))
		return 0;

	if (req == 1) {
		i = usb_control_msg(
			ibmcam->dev,
			usb_rcvctrlpipe(ibmcam->dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			value,
			index,
			cp,
			sizeof(cp),
			HZ);
#if 0
		printk(KERN_DEBUG "USB => %02x%02x%02x%02x%02x%02x%02x%02x "
		       "(req=$%02x val=$%04x ind=$%04x)\n",
		       cp[0],cp[1],cp[2],cp[3],cp[4],cp[5],cp[6],cp[7],
		       req, value, index);
#endif
	} else {
		i = usb_control_msg(
			ibmcam->dev,
			usb_sndctrlpipe(ibmcam->dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			value,
			index,
			NULL,
			0,
			HZ);
	}
	if (i < 0) {
		printk(KERN_ERR "%s: ERROR=%d. Camera stopped - "
		       "reconnect or reload driver.\n", proc, i);
		ibmcam->last_error = i;
	}
	return i;
}

/*
 * usb_ibmcam_calculate_fps()
 *
 * This procedure roughly calculates the real frame rate based
 * on FPS code (framerate=NNN option). Actual FPS differs
 * slightly depending on lighting conditions, so that actual frame
 * rate is determined by the camera. Since I don't know how to ask
 * the camera what FPS is now I have to use the FPS code instead.
 *
 * The FPS code is in range [0..6], 0 is slowest, 6 is fastest.
 * Corresponding real FPS should be in range [3..30] frames per second.
 * The conversion formula is obvious:
 *
 * real_fps = 3 + (fps_code * 4.5)
 *
 * History:
 * 1/18/00  Created.
 */
static int usb_ibmcam_calculate_fps(void)
{
	return 3 + framerate*4 + framerate/2;
}

/*
 * usb_ibmcam_send_FF_04_02()
 *
 * This procedure sends magic 3-command prefix to the camera.
 * The purpose of this prefix is not known.
 *
 * History:
 * 1/2/00   Created.
 */
static void usb_ibmcam_send_FF_04_02(struct usb_ibmcam *ibmcam)
{
	usb_ibmcam_veio(ibmcam, 0, 0x00FF, 0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0004, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0002, 0x0124);
}

static void usb_ibmcam_send_00_04_06(struct usb_ibmcam *ibmcam)
{
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0004, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0006, 0x0124);
}

static void usb_ibmcam_send_x_00(struct usb_ibmcam *ibmcam, unsigned short x)
{
	usb_ibmcam_veio(ibmcam, 0, x,      0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0124);
}

static void usb_ibmcam_send_x_00_05(struct usb_ibmcam *ibmcam, unsigned short x)
{
	usb_ibmcam_send_x_00(ibmcam, x);
	usb_ibmcam_veio(ibmcam, 0, 0x0005, 0x0124);
}

static void usb_ibmcam_send_x_00_05_02(struct usb_ibmcam *ibmcam, unsigned short x)
{
	usb_ibmcam_veio(ibmcam, 0, x,      0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0005, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0002, 0x0124);
}

static void usb_ibmcam_send_x_01_00_05(struct usb_ibmcam *ibmcam, unsigned short x)
{
	usb_ibmcam_veio(ibmcam, 0, x,      0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0001, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0005, 0x0124);
}

static void usb_ibmcam_send_x_00_05_02_01(struct usb_ibmcam *ibmcam, unsigned short x)
{
	usb_ibmcam_veio(ibmcam, 0, x,      0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0005, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0002, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0001, 0x0124);
}

static void usb_ibmcam_send_x_00_05_02_08_01(struct usb_ibmcam *ibmcam, unsigned short x)
{
	usb_ibmcam_veio(ibmcam, 0, x,      0x0127);
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0005, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0002, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0008, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, 0x0001, 0x0124);
}

static void usb_ibmcam_Packet_Format1(struct usb_ibmcam *ibmcam, unsigned char fkey, unsigned char val)
{
	usb_ibmcam_send_x_01_00_05	(ibmcam, unknown_88);
	usb_ibmcam_send_x_00_05		(ibmcam, fkey);
	usb_ibmcam_send_x_00_05_02_08_01(ibmcam, val);
	usb_ibmcam_send_x_00_05		(ibmcam, unknown_88);
	usb_ibmcam_send_x_00_05_02_01	(ibmcam, fkey);
	usb_ibmcam_send_x_00_05		(ibmcam, unknown_89);
	usb_ibmcam_send_x_00		(ibmcam, fkey);
	usb_ibmcam_send_00_04_06	(ibmcam);
	usb_ibmcam_veio			(ibmcam, 1, 0x0000, 0x0126);
	usb_ibmcam_send_FF_04_02	(ibmcam);
}

static void usb_ibmcam_PacketFormat2(struct usb_ibmcam *ibmcam, unsigned char fkey, unsigned char val)
{
	usb_ibmcam_send_x_01_00_05	(ibmcam, unknown_88);
	usb_ibmcam_send_x_00_05		(ibmcam, fkey);
	usb_ibmcam_send_x_00_05_02	(ibmcam, val);
}

static void usb_ibmcam_model2_Packet2(struct usb_ibmcam *ibmcam)
{
	usb_ibmcam_veio(ibmcam, 0, 0x00ff, 0x012d);
	usb_ibmcam_veio(ibmcam, 0, 0xfea3, 0x0124);
}

static void usb_ibmcam_model2_Packet1(struct usb_ibmcam *ibmcam, unsigned short v1, unsigned short v2)
{
	usb_ibmcam_veio(ibmcam, 0, 0x00aa, 0x012d);
	usb_ibmcam_veio(ibmcam, 0, 0x00ff, 0x012e);
	usb_ibmcam_veio(ibmcam, 0, v1, 	   0x012f);
	usb_ibmcam_veio(ibmcam, 0, 0x00ff, 0x0130);
	usb_ibmcam_veio(ibmcam, 0, 0xc719, 0x0124);
	usb_ibmcam_veio(ibmcam, 0, v2,     0x0127);

	usb_ibmcam_model2_Packet2(ibmcam);
}

/*
 * usb_ibmcam_adjust_contrast()
 *
 * The contrast value changes from 0 (high contrast) to 15 (low contrast).
 * This is in reverse to usual order of things (such as TV controls), so
 * we reverse it again here.
 *
 * TODO: we probably don't need to send the setup 5 times...
 *
 * History:
 * 1/2/00   Created.
 */
static void usb_ibmcam_adjust_contrast(struct usb_ibmcam *ibmcam)
{
	unsigned char new_contrast = ibmcam->vpic.contrast >> 12;
	const int ntries = 5;

	if (new_contrast >= 16)
		new_contrast = 15;
	new_contrast = 15 - new_contrast;
	if (new_contrast != ibmcam->vpic_old.contrast) {
		ibmcam->vpic_old.contrast = new_contrast;
		if (ibmcam->camera_model == IBMCAM_MODEL_1) {
			int i;
			for (i=0; i < ntries; i++) {
				usb_ibmcam_Packet_Format1(ibmcam, contrast_14, new_contrast);
				usb_ibmcam_send_FF_04_02(ibmcam);
			}
		} else {
			/* Camera model 2 does not have this control; implemented in software. */
		}
	}
}

/*
 * usb_ibmcam_change_lighting_conditions()
 *
 * Camera model 1:
 * We have 3 levels of lighting conditions: 0=Bright, 1=Medium, 2=Low.
 *
 * Camera model 2:
 * We have 16 levels of lighting, 0 for bright light and up to 15 for
 * low light. But values above 5 or so are useless because camera is
 * not really capable to produce anything worth viewing at such light.
 * This setting may be altered only in certain camera state.
 *
 * Low lighting forces slower FPS. Lighting is set as a module parameter.
 *
 * History:
 * 1/5/00   Created.
 * 2/20/00  Added support for Model 2 cameras.
 */
static void usb_ibmcam_change_lighting_conditions(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "usb_ibmcam_change_lighting_conditions";

	if (debug > 0)
		printk(KERN_INFO "%s: Set lighting to %hu.\n", proc, lighting);

	if (ibmcam->camera_model == IBMCAM_MODEL_1) {
		const int ntries = 5;
		int i;
		for (i=0; i < ntries; i++)
			usb_ibmcam_Packet_Format1(ibmcam, light_27, (unsigned short) lighting);
	} else {
		/*
		 * This command apparently requires camera to be stopped. My
		 * experiments showed that it -is- possible to alter the lighting
		 * conditions setting "on the fly", but why bother? This setting does
		 * not work reliably in all cases, so I decided simply to leave the
		 * setting where Xirlink put it - in the camera setup phase. This code
		 * is commented out because it does not work at -any- moment, so its
		 * presence makes no sense. You may use it for experiments.
		 */
#if 0
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x010c);	/* Stop camera */
		usb_ibmcam_model2_Packet1(ibmcam, mod2_sensitivity, lighting);
		usb_ibmcam_veio(ibmcam, 0, 0x00c0, 0x010c);	/* Start camera */
#endif
	}
}

/*
 * usb_ibmcam_set_sharpness()
 *
 * Cameras model 1 have internal smoothing feature. It is controlled by value in
 * range [0..6], where 0 is most smooth and 6 is most sharp (raw image, I guess).
 * Recommended value is 4. Cameras model 2 do not have this feature at all.
 */
static void usb_ibmcam_set_sharpness(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "usb_ibmcam_set_sharpness";

	if (ibmcam->camera_model == IBMCAM_MODEL_1) {
		static const unsigned short sa[] = { 0x11, 0x13, 0x16, 0x18, 0x1a, 0x8, 0x0a };
		unsigned short i, sv;

		RESTRICT_TO_RANGE(sharpness, SHARPNESS_MIN, SHARPNESS_MAX);
		if (debug > 0)
			printk(KERN_INFO "%s: Set sharpness to %hu.\n", proc, sharpness);

		sv = sa[sharpness - SHARPNESS_MIN];
		for (i=0; i < 2; i++) {
			usb_ibmcam_send_x_01_00_05	(ibmcam, unknown_88);
			usb_ibmcam_send_x_00_05		(ibmcam, sharp_13);
			usb_ibmcam_send_x_00_05_02	(ibmcam, sv);
		}
	} else {
		/* Camera model 2 does not have this control */
	}
}

/*
 * usb_ibmcam_set_brightness()
 *
 * This procedure changes brightness of the picture.
 */
static void usb_ibmcam_set_brightness(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "usb_ibmcam_set_brightness";
	static const unsigned short n = 1;
	unsigned short i, j, bv[3];

	bv[0] = bv[1] = bv[2] = ibmcam->vpic.brightness >> 10;
	if (bv[0] == (ibmcam->vpic_old.brightness >> 10))
		return;
	ibmcam->vpic_old.brightness = ibmcam->vpic.brightness;

	if (debug > 0)
		printk(KERN_INFO "%s: Set brightness to (%hu,%hu,%hu)\n",
		       proc, bv[0], bv[1], bv[2]);

	if (ibmcam->camera_model == IBMCAM_MODEL_1) {
		for (j=0; j < 3; j++)
			for (i=0; i < n; i++)
				usb_ibmcam_Packet_Format1(ibmcam, bright_3x[j], bv[j]);
	} else {
		i = ibmcam->vpic.brightness >> 12;	/* 0 .. 15 */
		j = 0x60 + i * ((0xee - 0x60) / 16);	/* 0x60 .. 0xee or so */
		usb_ibmcam_model2_Packet1(ibmcam, mod2_brightness, j);
	}
}

static void usb_ibmcam_model2_set_hue(struct usb_ibmcam *ibmcam)
{
	unsigned short hue = ibmcam->vpic.hue >> 9; /* 0 .. 7F */

	usb_ibmcam_model2_Packet1(ibmcam, mod2_color_balance_rg, hue);
	/* usb_ibmcam_model2_Packet1(ibmcam, mod2_saturation, sat); */
}

/*
 * usb_ibmcam_adjust_picture()
 *
 * This procedure gets called from V4L interface to update picture settings.
 * Here we change brightness and contrast.
 */
static void usb_ibmcam_adjust_picture(struct usb_ibmcam *ibmcam)
{
	usb_ibmcam_adjust_contrast(ibmcam);
	usb_ibmcam_set_brightness(ibmcam);
	if (ibmcam->camera_model == IBMCAM_MODEL_2) {
		usb_ibmcam_model2_set_hue(ibmcam);
	}
}

static int usb_ibmcam_model1_setup(struct usb_ibmcam *ibmcam)
{
	const int ntries = 5;
	int i;

	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0128);
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0100);
	usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0100);	/* LED On  */
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0100);
	usb_ibmcam_veio(ibmcam, 0, 0x81, 0x0100);	/* LED Off */
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0100);
	usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0100);	/* LED On  */
	usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0108);

	usb_ibmcam_veio(ibmcam, 0, 0x03, 0x0112);
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0115);
	usb_ibmcam_veio(ibmcam, 0, 0x06, 0x0115);
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0116);
	usb_ibmcam_veio(ibmcam, 0, 0x44, 0x0116);
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0116);
	usb_ibmcam_veio(ibmcam, 0, 0x40, 0x0116);
	usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0115);
	usb_ibmcam_veio(ibmcam, 0, 0x0e, 0x0115);
	usb_ibmcam_veio(ibmcam, 0, 0x19, 0x012c);

	usb_ibmcam_Packet_Format1(ibmcam, 0x00, 0x1e);
	usb_ibmcam_Packet_Format1(ibmcam, 0x39, 0x0d);
	usb_ibmcam_Packet_Format1(ibmcam, 0x39, 0x09);
	usb_ibmcam_Packet_Format1(ibmcam, 0x3b, 0x00);
	usb_ibmcam_Packet_Format1(ibmcam, 0x28, 0x22);
	usb_ibmcam_Packet_Format1(ibmcam, light_27, 0);
	usb_ibmcam_Packet_Format1(ibmcam, 0x2b, 0x1f);
	usb_ibmcam_Packet_Format1(ibmcam, 0x39, 0x08);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x2c, 0x00);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x30, 0x14);

	usb_ibmcam_PacketFormat2(ibmcam, 0x39, 0x02);
	usb_ibmcam_PacketFormat2(ibmcam, 0x01, 0xe1);
	usb_ibmcam_PacketFormat2(ibmcam, 0x02, 0xcd);
	usb_ibmcam_PacketFormat2(ibmcam, 0x03, 0xcd);
	usb_ibmcam_PacketFormat2(ibmcam, 0x04, 0xfa);
	usb_ibmcam_PacketFormat2(ibmcam, 0x3f, 0xff);
	usb_ibmcam_PacketFormat2(ibmcam, 0x39, 0x00);

	usb_ibmcam_PacketFormat2(ibmcam, 0x39, 0x02);
	usb_ibmcam_PacketFormat2(ibmcam, 0x0a, 0x37);
	usb_ibmcam_PacketFormat2(ibmcam, 0x0b, 0xb8);
	usb_ibmcam_PacketFormat2(ibmcam, 0x0c, 0xf3);
	usb_ibmcam_PacketFormat2(ibmcam, 0x0d, 0xe3);
	usb_ibmcam_PacketFormat2(ibmcam, 0x0e, 0x0d);
	usb_ibmcam_PacketFormat2(ibmcam, 0x0f, 0xf2);
	usb_ibmcam_PacketFormat2(ibmcam, 0x10, 0xd5);
	usb_ibmcam_PacketFormat2(ibmcam, 0x11, 0xba);
	usb_ibmcam_PacketFormat2(ibmcam, 0x12, 0x53);
	usb_ibmcam_PacketFormat2(ibmcam, 0x3f, 0xff);
	usb_ibmcam_PacketFormat2(ibmcam, 0x39, 0x00);

	usb_ibmcam_PacketFormat2(ibmcam, 0x39, 0x02);
	usb_ibmcam_PacketFormat2(ibmcam, 0x16, 0x00);
	usb_ibmcam_PacketFormat2(ibmcam, 0x17, 0x28);
	usb_ibmcam_PacketFormat2(ibmcam, 0x18, 0x7d);
	usb_ibmcam_PacketFormat2(ibmcam, 0x19, 0xbe);
	usb_ibmcam_PacketFormat2(ibmcam, 0x3f, 0xff);
	usb_ibmcam_PacketFormat2(ibmcam, 0x39, 0x00);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x00, 0x18);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x13, 0x18);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x14, 0x06);

	/* This is default brightness */
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x31, 0x37);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x32, 0x46);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x33, 0x55);

	usb_ibmcam_Packet_Format1(ibmcam, 0x2e, 0x04);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x2d, 0x04);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x29, 0x80);
	usb_ibmcam_Packet_Format1(ibmcam, 0x2c, 0x01);
	usb_ibmcam_Packet_Format1(ibmcam, 0x30, 0x17);
	usb_ibmcam_Packet_Format1(ibmcam, 0x39, 0x08);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x34, 0x00);

	usb_ibmcam_veio(ibmcam, 0, 0x00, 0x0101);
	usb_ibmcam_veio(ibmcam, 0, 0x00, 0x010a);

	switch (videosize) {
	case VIDEOSIZE_128x96:
		usb_ibmcam_veio(ibmcam, 0, 0x80, 0x0103);
		usb_ibmcam_veio(ibmcam, 0, 0x60, 0x0105);
		usb_ibmcam_veio(ibmcam, 0, 0x0c, 0x010b);
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x011b);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x0b, 0x011d);
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x011e);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x0129);
		break;
	case VIDEOSIZE_176x144:
		usb_ibmcam_veio(ibmcam, 0, 0xb0, 0x0103);
		usb_ibmcam_veio(ibmcam, 0, 0x8f, 0x0105);
		usb_ibmcam_veio(ibmcam, 0, 0x06, 0x010b);
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x011b);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x0d, 0x011d);
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x011e);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x03, 0x0129);
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_veio(ibmcam, 0, 0xb0, 0x0103);
		usb_ibmcam_veio(ibmcam, 0, 0x90, 0x0105);
		usb_ibmcam_veio(ibmcam, 0, 0x02, 0x010b);
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x011b);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x05, 0x011d);
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x011e);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x0129);
		break;
	}

	usb_ibmcam_veio(ibmcam, 0, 0xff, 0x012b);

	/* This is another brightness - don't know why */
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x31, 0xc3);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x32, 0xd2);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, 0x33, 0xe1);

	/* Default contrast */
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(ibmcam, contrast_14, 0x0a);

	/* Default sharpness */
	for (i=0; i < 2; i++)
		usb_ibmcam_PacketFormat2(ibmcam, sharp_13, 0x1a);	/* Level 4 FIXME */

	/* Default lighting conditions */
	usb_ibmcam_Packet_Format1(ibmcam, light_27, lighting); /* 0=Bright 2=Low */

	/* Assorted init */

	switch (videosize) {
	case VIDEOSIZE_128x96:
		usb_ibmcam_Packet_Format1(ibmcam, 0x2b, 0x1e);
		usb_ibmcam_veio(ibmcam, 0, 0xc9, 0x0119);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x80, 0x0109);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x36, 0x0102);
		usb_ibmcam_veio(ibmcam, 0, 0x1a, 0x0104);
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x011a);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x2b, 0x011c);
		usb_ibmcam_veio(ibmcam, 0, 0x23, 0x012a);	/* Same everywhere */
#if 0
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x0106);
		usb_ibmcam_veio(ibmcam, 0, 0x38, 0x0107);
#else
		usb_ibmcam_veio(ibmcam, 0, 0x02, 0x0106);
		usb_ibmcam_veio(ibmcam, 0, 0x2a, 0x0107);
#endif
		break;
	case VIDEOSIZE_176x144:
		usb_ibmcam_Packet_Format1(ibmcam, 0x2b, 0x1e);
		usb_ibmcam_veio(ibmcam, 0, 0xc9, 0x0119);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x80, 0x0109);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x0102);
		usb_ibmcam_veio(ibmcam, 0, 0x02, 0x0104);
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x011a);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x2b, 0x011c);
		usb_ibmcam_veio(ibmcam, 0, 0x23, 0x012a);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0106);
		usb_ibmcam_veio(ibmcam, 0, 0xca, 0x0107);
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_Packet_Format1(ibmcam, 0x2b, 0x1f);
		usb_ibmcam_veio(ibmcam, 0, 0xc9, 0x0119);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x80, 0x0109);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x08, 0x0102);
		usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0104);
		usb_ibmcam_veio(ibmcam, 0, 0x04, 0x011a);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x2f, 0x011c);
		usb_ibmcam_veio(ibmcam, 0, 0x23, 0x012a);	/* Same everywhere */
		usb_ibmcam_veio(ibmcam, 0, 0x03, 0x0106);
		usb_ibmcam_veio(ibmcam, 0, 0xf6, 0x0107);
		break;
	}
	return IBMCAM_IS_OPERATIONAL(ibmcam);
}

static int usb_ibmcam_model2_setup(struct usb_ibmcam *ibmcam)
{
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0100);	/* LED on */
	usb_ibmcam_veio(ibmcam, 1, 0x0000, 0x0116);
	usb_ibmcam_veio(ibmcam, 0, 0x0060, 0x0116);
	usb_ibmcam_veio(ibmcam, 0, 0x0002, 0x0112);
	usb_ibmcam_veio(ibmcam, 0, 0x00bc, 0x012c);
	usb_ibmcam_veio(ibmcam, 0, 0x0008, 0x012b);
	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0108);
	usb_ibmcam_veio(ibmcam, 0, 0x0001, 0x0133);
	usb_ibmcam_veio(ibmcam, 0, 0x0001, 0x0102);
	switch (videosize) {
	case VIDEOSIZE_176x144:
		usb_ibmcam_veio(ibmcam, 0, 0x002c, 0x0103);	/* All except 320x240 */
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0104);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x0024, 0x0105);	/* 176x144, 352x288 */
		usb_ibmcam_veio(ibmcam, 0, 0x00b9, 0x010a);	/* Unique to this mode */
		usb_ibmcam_veio(ibmcam, 0, 0x0038, 0x0119);	/* Unique to this mode */
		usb_ibmcam_veio(ibmcam, 0, 0x0003, 0x0106);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x0090, 0x0107);	/* Unique to every mode*/
		break;
	case VIDEOSIZE_320x240:
		usb_ibmcam_veio(ibmcam, 0, 0x0028, 0x0103);	/* Unique to this mode */
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0104);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x001e, 0x0105);	/* 320x240, 352x240 */
		usb_ibmcam_veio(ibmcam, 0, 0x0039, 0x010a);	/* All except 176x144 */
		usb_ibmcam_veio(ibmcam, 0, 0x0070, 0x0119);	/* All except 176x144 */
		usb_ibmcam_veio(ibmcam, 0, 0x0003, 0x0106);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x0098, 0x0107);	/* Unique to every mode*/
		break;
	case VIDEOSIZE_352x240:
		usb_ibmcam_veio(ibmcam, 0, 0x002c, 0x0103);	/* All except 320x240 */
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0104);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x001e, 0x0105);	/* 320x240, 352x240 */
		usb_ibmcam_veio(ibmcam, 0, 0x0039, 0x010a);	/* All except 176x144 */
		usb_ibmcam_veio(ibmcam, 0, 0x0070, 0x0119);	/* All except 176x144 */
		usb_ibmcam_veio(ibmcam, 0, 0x0003, 0x0106);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x00da, 0x0107);	/* Unique to every mode*/
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_veio(ibmcam, 0, 0x002c, 0x0103);	/* All except 320x240 */
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0104);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x0024, 0x0105);	/* 176x144, 352x288 */
		usb_ibmcam_veio(ibmcam, 0, 0x0039, 0x010a);	/* All except 176x144 */
		usb_ibmcam_veio(ibmcam, 0, 0x0070, 0x0119);	/* All except 176x144 */
		usb_ibmcam_veio(ibmcam, 0, 0x0003, 0x0106);	/* Same */
		usb_ibmcam_veio(ibmcam, 0, 0x00fe, 0x0107);	/* Unique to every mode*/
		break;
	}
	return IBMCAM_IS_OPERATIONAL(ibmcam);
}

/*
 * usb_ibmcam_model1_setup_after_video_if()
 *
 * This code adds finishing touches to the video data interface.
 * Here we configure the frame rate and turn on the LED.
 */
static void usb_ibmcam_model1_setup_after_video_if(struct usb_ibmcam *ibmcam)
{
	unsigned short internal_frame_rate;

	RESTRICT_TO_RANGE(framerate, FRAMERATE_MIN, FRAMERATE_MAX);
	internal_frame_rate = FRAMERATE_MAX - framerate; /* 0=Fast 6=Slow */
	usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0100);	/* LED On  */
	usb_ibmcam_veio(ibmcam, 0, internal_frame_rate, 0x0111);
	usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0114);
	usb_ibmcam_veio(ibmcam, 0, 0xc0, 0x010c);
}

static void usb_ibmcam_model2_setup_after_video_if(struct usb_ibmcam *ibmcam)
{
	unsigned short setup_model2_rg, setup_model2_rg2, setup_model2_sat, setup_model2_yb;

	usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0100);	/* LED on */

	switch (videosize) {
	case VIDEOSIZE_176x144:
		usb_ibmcam_veio(ibmcam, 0, 0x0050, 0x0111);
		usb_ibmcam_veio(ibmcam, 0, 0x00d0, 0x0111);
		break;
	case VIDEOSIZE_320x240:
	case VIDEOSIZE_352x240:
	case VIDEOSIZE_352x288:
		usb_ibmcam_veio(ibmcam, 0, 0x0040, 0x0111);
		usb_ibmcam_veio(ibmcam, 0, 0x00c0, 0x0111);
		break;
	}
	usb_ibmcam_veio(ibmcam, 0, 0x009b, 0x010f);
	usb_ibmcam_veio(ibmcam, 0, 0x00bb, 0x010f);

	/*
	 * Hardware settings, may affect CMOS sensor; not user controls!
	 * -------------------------------------------------------------
	 * 0x0004: no effect
	 * 0x0006: hardware effect
	 * 0x0008: no effect
	 * 0x000a: stops video stream, probably important h/w setting
	 * 0x000c: changes color in hardware manner (not user setting)
	 * 0x0012: changes number of colors (does not affect speed)
	 * 0x002a: no effect
	 * 0x002c: hardware setting (related to scan lines)
	 * 0x002e: stops video stream, probably important h/w setting
	 */
	usb_ibmcam_model2_Packet1(ibmcam, 0x000a, 0x005c);
	usb_ibmcam_model2_Packet1(ibmcam, 0x0004, 0x0000);
	usb_ibmcam_model2_Packet1(ibmcam, 0x0006, 0x00fb);
	usb_ibmcam_model2_Packet1(ibmcam, 0x0008, 0x0000);
	usb_ibmcam_model2_Packet1(ibmcam, 0x000c, 0x0009);
	usb_ibmcam_model2_Packet1(ibmcam, 0x0012, 0x000a);
	usb_ibmcam_model2_Packet1(ibmcam, 0x002a, 0x0000);
	usb_ibmcam_model2_Packet1(ibmcam, 0x002c, 0x0000);
	usb_ibmcam_model2_Packet1(ibmcam, 0x002e, 0x0008);

	/*
	 * Function 0x0030 pops up all over the place. Apparently
	 * it is a hardware control register, with every bit assigned to
	 * do something.
	 */
	usb_ibmcam_model2_Packet1(ibmcam, 0x0030, 0x0000);

	/*
	 * Magic control of CMOS sensor. Only lower values like
	 * 0-3 work, and picture shifts left or right. Don't change.
	 */
	switch (videosize) {
	case VIDEOSIZE_176x144:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0014, 0x0002);
		usb_ibmcam_model2_Packet1(ibmcam, 0x0016, 0x0002); /* Horizontal shift */
		usb_ibmcam_model2_Packet1(ibmcam, 0x0018, 0x004a); /* Another hardware setting */
		break;
	case VIDEOSIZE_320x240:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0014, 0x0009);
		usb_ibmcam_model2_Packet1(ibmcam, 0x0016, 0x0005); /* Horizontal shift */
		usb_ibmcam_model2_Packet1(ibmcam, 0x0018, 0x0044); /* Another hardware setting */
		break;
	case VIDEOSIZE_352x240:
		/* This mode doesn't work as Windows programs it; changed to work */
		usb_ibmcam_model2_Packet1(ibmcam, 0x0014, 0x0009); /* Windows sets this to 8 */
		usb_ibmcam_model2_Packet1(ibmcam, 0x0016, 0x0003); /* Horizontal shift */
		usb_ibmcam_model2_Packet1(ibmcam, 0x0018, 0x0044); /* Windows sets this to 0x0045 */
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0014, 0x0003);
		usb_ibmcam_model2_Packet1(ibmcam, 0x0016, 0x0002); /* Horizontal shift */
		usb_ibmcam_model2_Packet1(ibmcam, 0x0018, 0x004a); /* Another hardware setting */
		break;
	}

	usb_ibmcam_model2_Packet1(ibmcam, mod2_brightness, 0x005a);

	/*
	 * We have our own frame rate setting varying from 0 (slowest) to 6 (fastest).
	 * The camera model 2 allows frame rate in range [0..0x1F] where 0 is also the
	 * slowest setting. However for all practical reasons high settings make no
	 * sense because USB is not fast enough to support high FPS. Be aware that
	 * the picture datastream will be severely disrupted if you ask for
	 * frame rate faster than allowed for the video size - see below:
	 *
	 * Allowable ranges (obtained experimentally on OHCI, K6-3, 450 MHz):
	 * -----------------------------------------------------------------
	 * 176x144: [6..31]
	 * 320x240: [8..31]
	 * 352x240: [10..31]
	 * 352x288: [16..31] I have to raise lower threshold for stability...
	 *
	 * As usual, slower FPS provides better sensitivity.
	 */
	{
		short hw_fps=31, i_framerate;

		RESTRICT_TO_RANGE(framerate, FRAMERATE_MIN, FRAMERATE_MAX);
		i_framerate = FRAMERATE_MAX - framerate + FRAMERATE_MIN;
		switch (videosize) {
		case VIDEOSIZE_176x144:
			hw_fps = 6 + i_framerate*4;
			break;
		case VIDEOSIZE_320x240:
			hw_fps = 8 + i_framerate*3;
			break;
		case VIDEOSIZE_352x240:
			hw_fps = 10 + i_framerate*2;
			break;
		case VIDEOSIZE_352x288:
			hw_fps = 28 + i_framerate/2;
			break;
		}
		if (debug > 0)
			printk(KERN_DEBUG "Framerate (hardware): %hd.\n", hw_fps);
		RESTRICT_TO_RANGE(hw_fps, 0, 31);
		usb_ibmcam_model2_Packet1(ibmcam, mod2_set_framerate, hw_fps);
	}

	/*
	 * This setting does not visibly affect pictures; left it here
	 * because it was present in Windows USB data stream. This function
	 * does not allow arbitrary values and apparently is a bit mask, to
	 * be activated only at appropriate time. Don't change it randomly!
	 */
	switch (videosize) {
	case VIDEOSIZE_176x144:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0026, 0x00c2);
		break;
	case VIDEOSIZE_320x240:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0026, 0x0044);
		break;
	case VIDEOSIZE_352x240:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0026, 0x0046);
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_model2_Packet1(ibmcam, 0x0026, 0x0048);
		break;
	}

	usb_ibmcam_model2_Packet1(ibmcam, mod2_sensitivity, lighting);

	if (init_model2_rg >= 0) {
		RESTRICT_TO_RANGE(init_model2_rg, 0, 255);
		setup_model2_rg = init_model2_rg;
	} else
		setup_model2_rg = 0x0070;

	if (init_model2_rg2 >= 0) {
		RESTRICT_TO_RANGE(init_model2_rg2, 0, 255);
		setup_model2_rg2 = init_model2_rg2;
	} else
		setup_model2_rg2 = 0x002f;

	if (init_model2_sat >= 0) {
		RESTRICT_TO_RANGE(init_model2_sat, 0, 255);
		setup_model2_sat = init_model2_sat;
	} else
		setup_model2_sat = 0x0034;

	if (init_model2_yb >= 0) {
		RESTRICT_TO_RANGE(init_model2_yb, 0, 255);
		setup_model2_yb = init_model2_yb;
	} else
		setup_model2_yb = 0x00a0;

	usb_ibmcam_model2_Packet1(ibmcam, mod2_color_balance_rg2, setup_model2_rg2);
	usb_ibmcam_model2_Packet1(ibmcam, mod2_saturation, setup_model2_sat);
	usb_ibmcam_model2_Packet1(ibmcam, mod2_color_balance_yb, setup_model2_yb);
	usb_ibmcam_model2_Packet1(ibmcam, mod2_color_balance_rg, setup_model2_rg);

	/* Hardware control command */
	usb_ibmcam_model2_Packet1(ibmcam, 0x0030, 0x0004);

	usb_ibmcam_veio(ibmcam, 0, 0x00c0, 0x010c);	/* Go camera, go! */
	usb_clear_halt(ibmcam->dev, ibmcam->video_endp);
}

/*
 * usb_ibmcam_setup_video_stop()
 *
 * This code tells camera to stop streaming. The interface remains
 * configured and bandwidth - claimed.
 */
static void usb_ibmcam_setup_video_stop(struct usb_ibmcam *ibmcam)
{
	if (ibmcam->camera_model == IBMCAM_MODEL_1) {
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x010c);
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x010c);
		usb_ibmcam_veio(ibmcam, 0, 0x01, 0x0114);
		usb_ibmcam_veio(ibmcam, 0, 0xc0, 0x010c);
		usb_ibmcam_veio(ibmcam, 0, 0x00, 0x010c);
		usb_ibmcam_send_FF_04_02(ibmcam);
		usb_ibmcam_veio(ibmcam, 1, 0x00, 0x0100);
		usb_ibmcam_veio(ibmcam, 0, 0x81, 0x0100);	/* LED Off */
	} else if (ibmcam->camera_model == IBMCAM_MODEL_2) {
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x010c);	/* Stop the camera */

		usb_ibmcam_model2_Packet1(ibmcam, 0x0030, 0x0004);

		usb_ibmcam_veio(ibmcam, 0, 0x0080, 0x0100);	/* LED Off */
		usb_ibmcam_veio(ibmcam, 0, 0x0020, 0x0111);
		usb_ibmcam_veio(ibmcam, 0, 0x00a0, 0x0111);

		usb_ibmcam_model2_Packet1(ibmcam, 0x0030, 0x0002);

		usb_ibmcam_veio(ibmcam, 0, 0x0020, 0x0111);
		usb_ibmcam_veio(ibmcam, 0, 0x0000, 0x0112);
	}
}

/*
 * usb_ibmcam_reinit_iso()
 *
 * This procedure sends couple of commands to the camera and then
 * resets the video pipe. This sequence was observed to reinit the
 * camera or, at least, to initiate ISO data stream.
 *
 * History:
 * 1/2/00   Created.
 */
static void usb_ibmcam_reinit_iso(struct usb_ibmcam *ibmcam, int do_stop)
{
	if (ibmcam->camera_model == IBMCAM_MODEL_1) {
		if (do_stop)
			usb_ibmcam_setup_video_stop(ibmcam);
		usb_ibmcam_veio(ibmcam, 0, 0x0001, 0x0114);
		usb_ibmcam_veio(ibmcam, 0, 0x00c0, 0x010c);
		usb_clear_halt(ibmcam->dev, ibmcam->video_endp);
		usb_ibmcam_model1_setup_after_video_if(ibmcam);
	} else if (ibmcam->camera_model == IBMCAM_MODEL_2) {
		usb_ibmcam_model2_setup_after_video_if(ibmcam);
	}
}

/*
 * ibmcam_init_isoc()
 *
 * History:
 * 1/27/00  Used ibmcam->iface, ibmcam->ifaceAltActive instead of hardcoded values.
 *          Simplified by using for loop, allowed any number of URBs.
 */
static int ibmcam_init_isoc(struct usb_ibmcam *ibmcam)
{
	struct usb_device *dev = ibmcam->dev;
	int i, err;

	if (!IBMCAM_IS_OPERATIONAL(ibmcam))
		return -EFAULT;

	ibmcam->compress = 0;
	ibmcam->curframe = -1;
	ibmcam->cursbuf = 0;
	ibmcam->scratchlen = 0;

	/* Alternate interface 1 is is the biggest frame size */
	i = usb_set_interface(dev, ibmcam->iface, ibmcam->ifaceAltActive);
	if (i < 0) {
		printk(KERN_ERR "usb_set_interface error\n");
		ibmcam->last_error = i;
		return -EBUSY;
	}
	usb_ibmcam_change_lighting_conditions(ibmcam);
	usb_ibmcam_set_sharpness(ibmcam);
	usb_ibmcam_reinit_iso(ibmcam, 0);

	/* We double buffer the Iso lists */

	for (i=0; i < IBMCAM_NUMSBUF; i++) {
		int j, k;
		urb_t *urb;

		urb = usb_alloc_urb(FRAMES_PER_DESC);
		if (urb == NULL) {
			printk(KERN_ERR "ibmcam_init_isoc: usb_init_isoc() failed.\n");
			return -ENOMEM;
		}
		ibmcam->sbuf[i].urb = urb;
		urb->dev = dev;
		urb->context = ibmcam;
		urb->pipe = usb_rcvisocpipe(dev, ibmcam->video_endp);
		urb->transfer_flags = USB_ISO_ASAP;
		urb->transfer_buffer = ibmcam->sbuf[i].data;
		urb->complete = ibmcam_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = ibmcam->iso_packet_len * FRAMES_PER_DESC;
		for (j=k=0; j < FRAMES_PER_DESC; j++, k += ibmcam->iso_packet_len) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = ibmcam->iso_packet_len;
		}
	}

	/* Link URBs into a ring so that they invoke each other infinitely */
	for (i=0; i < IBMCAM_NUMSBUF; i++) {
		if ((i+1) < IBMCAM_NUMSBUF)
			ibmcam->sbuf[i].urb->next = ibmcam->sbuf[i+1].urb;
		else
			ibmcam->sbuf[i].urb->next = ibmcam->sbuf[0].urb;
	}

	/* Submit all URBs */
	for (i=0; i < IBMCAM_NUMSBUF; i++) {
		err = usb_submit_urb(ibmcam->sbuf[i].urb);
		if (err)
			printk(KERN_ERR "ibmcam_init_isoc: usb_run_isoc(%d) ret %d\n",
			       i, err);
	}

	ibmcam->streaming = 1;
	/* printk(KERN_DEBUG "streaming=1 ibmcam->video_endp=$%02x\n", ibmcam->video_endp); */
	return 0;
}

/*
 * ibmcam_stop_isoc()
 *
 * This procedure stops streaming and deallocates URBs. Then it
 * activates zero-bandwidth alt. setting of the video interface.
 *
 * History:
 * 1/22/00  Corrected order of actions to work after surprise removal.
 * 1/27/00  Used ibmcam->iface, ibmcam->ifaceAltInactive instead of hardcoded values.
 */
static void ibmcam_stop_isoc(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "ibmcam_stop_isoc";
	int i, j;

	if (!ibmcam->streaming || (ibmcam->dev == NULL))
		return;

	/* Unschedule all of the iso td's */
	for (i=0; i < IBMCAM_NUMSBUF; i++) {
		j = usb_unlink_urb(ibmcam->sbuf[i].urb);
		if (j < 0)
			printk(KERN_ERR "%s: usb_unlink_urb() error %d.\n", proc, j);
	}
	/* printk(KERN_DEBUG "streaming=0\n"); */
	ibmcam->streaming = 0;

	/* Delete them all */
	for (i=0; i < IBMCAM_NUMSBUF; i++)
		usb_free_urb(ibmcam->sbuf[i].urb);

	if (!ibmcam->remove_pending) {
		usb_ibmcam_setup_video_stop(ibmcam);

		/* Set packet size to 0 */
		j = usb_set_interface(ibmcam->dev, ibmcam->iface, ibmcam->ifaceAltInactive);
		if (j < 0) {
			printk(KERN_ERR "%s: usb_set_interface() error %d.\n", proc, j);
			ibmcam->last_error = j;
		}
	}
}

/*
 * ibmcam_new_frame()
 *
 * History:
 * 29-Mar-00 Added copying of previous frame into the current one.
 */
static int ibmcam_new_frame(struct usb_ibmcam *ibmcam, int framenum)
{
	struct ibmcam_frame *frame;
	int n, width, height;

	/* If we're not grabbing a frame right now and the other frame is */
	/*  ready to be grabbed into, then use it instead */
	if (ibmcam->curframe != -1)
		return 0;

	n = (framenum - 1 + IBMCAM_NUMFRAMES) % IBMCAM_NUMFRAMES;
	if (ibmcam->frame[n].grabstate == FRAME_READY)
		framenum = n;

	frame = &ibmcam->frame[framenum];

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;
	frame->scanlength = 0;		/* Accumulated in ibmcam_parse_data() */
	ibmcam->curframe = framenum;

	/*
	 * Normally we would want to copy previous frame into the current one
	 * before we even start filling it with data; this allows us to stop
	 * filling at any moment; top portion of the frame will be new and
	 * bottom portion will stay as it was in previous frame. If we don't
	 * do that then missing chunks of video stream will result in flickering
	 * portions of old data whatever it was before.
	 *
	 * If we choose not to copy previous frame (to, for example, save few
	 * bus cycles - the frame can be pretty large!) then we have an option
	 * to clear the frame before using. If we experience losses in this
	 * mode then missing picture will be black (no flickering).
	 *
	 * Finally, if user chooses not to clean the current frame before
	 * filling it with data then the old data will be visible if we fail
	 * to refill entire frame with new data.
	 */
	if (!(flags & FLAGS_SEPARATE_FRAMES)) {
		/* This copies previous frame into this one to mask losses */
		memmove(frame->data, ibmcam->frame[1-framenum].data,  MAX_FRAME_SIZE);
	} else {
		if (flags & FLAGS_CLEAN_FRAMES) {
			/* This provides a "clean" frame but slows things down */
			memset(frame->data, 0, MAX_FRAME_SIZE);
		}
	}
	switch (videosize) {
	case VIDEOSIZE_128x96:
		frame->frmwidth = 128;
		frame->frmheight = 96;
		frame->order_uv = 1;	/* U Y V Y ... */
		frame->hdr_sig = 0x06;	/* 00 FF 00 06 */
		break;
	case VIDEOSIZE_176x144:
		frame->frmwidth = 176;
		frame->frmheight = 144;
		frame->order_uv = 1;	/* U Y V Y ... */
		frame->hdr_sig = 0x0E;	/* 00 FF 00 0E */
		break;
	case VIDEOSIZE_320x240:		/* For model 2 only */
		frame->frmwidth = 320;
		frame->frmheight = 240;
		break;
	case VIDEOSIZE_352x240:		/* For model 2 only */
		frame->frmwidth = 352;
		frame->frmheight = 240;
		break;
	case VIDEOSIZE_352x288:
		frame->frmwidth = 352;
		frame->frmheight = 288;
		frame->order_uv = 0;	/* V Y U Y ... */
		frame->hdr_sig = 0x00;	/* 00 FF 00 00 */
		break;
	}
	frame->order_yc = (ibmcam->camera_model == IBMCAM_MODEL_2);

	width = frame->width;
	RESTRICT_TO_RANGE(width, min_imgwidth, imgwidth);
	width &= ~7;		/* Multiple of 8 */

	height = frame->height;
	RESTRICT_TO_RANGE(height, min_imgheight, imgheight);
	height &= ~3;		/* Multiple of 4 */

	return 0;
}

/*
 * ibmcam_open()
 *
 * This is part of Video 4 Linux API. The driver can be opened by one
 * client only (checks internal counter 'ibmcam->user'). The procedure
 * then allocates buffers needed for video processing.
 *
 * History:
 * 1/22/00  Rewrote, moved scratch buffer allocation here. Now the
 *          camera is also initialized here (once per connect), at
 *          expense of V4L client (it waits on open() call).
 * 1/27/00  Used IBMCAM_NUMSBUF as number of URB buffers.
 * 5/24/00  Corrected to prevent race condition (MOD_xxx_USE_COUNT).
 */
static int ibmcam_open(struct video_device *dev, int flags)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;
	const int sb_size = FRAMES_PER_DESC * ibmcam->iso_packet_len;
	int i, err = 0;

	MOD_INC_USE_COUNT;
	down(&ibmcam->lock);

	if (ibmcam->user)
		err = -EBUSY;
	else {
		/* Clean pointers so we know if we allocated something */
		for (i=0; i < IBMCAM_NUMSBUF; i++)
			ibmcam->sbuf[i].data = NULL;

		/* Allocate memory for the frame buffers */
		ibmcam->fbuf_size = IBMCAM_NUMFRAMES * MAX_FRAME_SIZE;
		ibmcam->fbuf = rvmalloc(ibmcam->fbuf_size);
		ibmcam->scratch = kmalloc(scratchbufsize, GFP_KERNEL);
		ibmcam->scratchlen = 0;
		if ((ibmcam->fbuf == NULL) || (ibmcam->scratch == NULL))
			err = -ENOMEM;
		else {
			/* Allocate all buffers */
			for (i=0; i < IBMCAM_NUMFRAMES; i++) {
				ibmcam->frame[i].grabstate = FRAME_UNUSED;
				ibmcam->frame[i].data = ibmcam->fbuf + i*MAX_FRAME_SIZE;
				/*
				 * Set default sizes in case IOCTL (VIDIOCMCAPTURE)
				 * is not used (using read() instead).
				 */
				ibmcam->frame[i].width = imgwidth;
				ibmcam->frame[i].height = imgheight;
				ibmcam->frame[i].bytes_read = 0;
			}
			for (i=0; i < IBMCAM_NUMSBUF; i++) {
				ibmcam->sbuf[i].data = kmalloc(sb_size, GFP_KERNEL);
				if (ibmcam->sbuf[i].data == NULL) {
					err = -ENOMEM;
					break;
				}
			}
		}
		if (err) {
			/* Have to free all that memory */
			if (ibmcam->fbuf != NULL) {
				rvfree(ibmcam->fbuf, ibmcam->fbuf_size);
				ibmcam->fbuf = NULL;
			}
			if (ibmcam->scratch != NULL) {
				kfree(ibmcam->scratch);
				ibmcam->scratch = NULL;
			}
			for (i=0; i < IBMCAM_NUMSBUF; i++) {
				if (ibmcam->sbuf[i].data != NULL) {
					kfree (ibmcam->sbuf[i].data);
					ibmcam->sbuf[i].data = NULL;
				}
			}
		}
	}

	/* If so far no errors then we shall start the camera */
	if (!err) {
		err = ibmcam_init_isoc(ibmcam);
		if (!err) {
			/* Send init sequence only once, it's large! */
			if (!ibmcam->initialized) {
				int setup_ok = 0;
				if (ibmcam->camera_model == IBMCAM_MODEL_1)
					setup_ok = usb_ibmcam_model1_setup(ibmcam);
				else if (ibmcam->camera_model == IBMCAM_MODEL_2)
					setup_ok = usb_ibmcam_model2_setup(ibmcam);
				if (setup_ok)
					ibmcam->initialized = 1;
				else
					err = -EBUSY;
			}
			if (!err)
				ibmcam->user++;
		}
	}
	up(&ibmcam->lock);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

/*
 * ibmcam_close()
 *
 * This is part of Video 4 Linux API. The procedure
 * stops streaming and deallocates all buffers that were earlier
 * allocated in ibmcam_open().
 *
 * History:
 * 1/22/00  Moved scratch buffer deallocation here.
 * 1/27/00  Used IBMCAM_NUMSBUF as number of URB buffers.
 * 5/24/00  Moved MOD_DEC_USE_COUNT outside of code that can sleep.
 */
static void ibmcam_close(struct video_device *dev)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;
	int i;

	down(&ibmcam->lock);	

	ibmcam_stop_isoc(ibmcam);

	rvfree(ibmcam->fbuf, ibmcam->fbuf_size);
	kfree(ibmcam->scratch);
	for (i=0; i < IBMCAM_NUMSBUF; i++)
		kfree(ibmcam->sbuf[i].data);

	ibmcam->user--;

	if (ibmcam->remove_pending) {
		printk(KERN_INFO "ibmcam_close: Final disconnect.\n");
		usb_ibmcam_release(ibmcam);
	}
	up(&ibmcam->lock);
	MOD_DEC_USE_COUNT;
}

static long ibmcam_write(struct video_device *dev, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;
}

/*
 * ibmcam_ioctl()
 *
 * This is part of Video 4 Linux API. The procedure handles ioctl() calls.
 *
 * History:
 * 1/22/00  Corrected VIDIOCSPICT to reject unsupported settings.
 */
static int ibmcam_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;

	if (!IBMCAM_IS_OPERATIONAL(ibmcam))
		return -EFAULT;

	switch (cmd) {
		case VIDIOCGCAP:
		{
			if (copy_to_user(arg, &ibmcam->vcap, sizeof(ibmcam->vcap)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			if (copy_to_user(arg, &ibmcam->vchan, sizeof(ibmcam->vchan)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSCHAN:
		{
			int v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if ((v < 0) || (v >= 3)) /* 3 grades of lighting conditions */
				return -EINVAL;
			if (v != ibmcam->vchan.channel) {
				ibmcam->vchan.channel = v;
				usb_ibmcam_change_lighting_conditions(ibmcam);
			}
			return 0;
		}
		case VIDIOCGPICT:
		{
			if (copy_to_user(arg, &ibmcam->vpic, sizeof(ibmcam->vpic)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture tmp;
			/*
			 * Use temporary 'video_picture' structure to preserve our
			 * own settings (such as color depth, palette) that we
			 * aren't allowing everyone (V4L client) to change.
			 */
			if (copy_from_user(&tmp, arg, sizeof(tmp)))
				return -EFAULT;
			ibmcam->vpic.brightness = tmp.brightness;
			ibmcam->vpic.hue = tmp.hue;
			ibmcam->vpic.colour = tmp.colour;
			ibmcam->vpic.contrast = tmp.contrast;
			usb_ibmcam_adjust_picture(ibmcam);
			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;

			if (copy_from_user(&vw, arg, sizeof(vw)))
				return -EFAULT;
			if (vw.flags)
				return -EINVAL;
			if (vw.clipcount)
				return -EINVAL;
			if (vw.height != imgheight)
				return -EINVAL;
			if (vw.width != imgwidth)
				return -EINVAL;

			ibmcam->compress = 0;

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

			vw.x = 0;
			vw.y = 0;
			vw.width = imgwidth;
			vw.height = imgheight;
			vw.chromakey = 0;
			vw.flags = usb_ibmcam_calculate_fps();

			if (copy_to_user(arg, &vw, sizeof(vw)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCGMBUF:
		{
			struct video_mbuf vm;

			memset(&vm, 0, sizeof(vm));
			vm.size = MAX_FRAME_SIZE * 2;
			vm.frames = 2;
			vm.offsets[0] = 0;
			vm.offsets[1] = MAX_FRAME_SIZE;

			if (copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCMCAPTURE:
		{
			struct video_mmap vm;

			if (copy_from_user((void *)&vm, (void *)arg, sizeof(vm)))
				return -EFAULT;

			if (debug >= 1)
				printk(KERN_DEBUG "frame: %d, size: %dx%d, format: %d\n",
					vm.frame, vm.width, vm.height, vm.format);

			if (vm.format != VIDEO_PALETTE_RGB24)
				return -EINVAL;

			if ((vm.frame != 0) && (vm.frame != 1))
				return -EINVAL;

			if (ibmcam->frame[vm.frame].grabstate == FRAME_GRABBING)
				return -EBUSY;

			/* Don't compress if the size changed */
			if ((ibmcam->frame[vm.frame].width != vm.width) ||
			    (ibmcam->frame[vm.frame].height != vm.height))
				ibmcam->compress = 0;

			ibmcam->frame[vm.frame].width = vm.width;
			ibmcam->frame[vm.frame].height = vm.height;

			/* Mark it as ready */
			ibmcam->frame[vm.frame].grabstate = FRAME_READY;

			return ibmcam_new_frame(ibmcam, vm.frame);
		}
		case VIDIOCSYNC:
		{
			int frame;

			if (copy_from_user((void *)&frame, arg, sizeof(int)))
				return -EFAULT;

			if (debug >= 1)
				printk(KERN_DEBUG "ibmcam: syncing to frame %d\n", frame);

			switch (ibmcam->frame[frame].grabstate) {
			case FRAME_UNUSED:
				return -EINVAL;
			case FRAME_READY:
			case FRAME_GRABBING:
			case FRAME_ERROR:
			{
				int ntries;
		redo:
				if (!IBMCAM_IS_OPERATIONAL(ibmcam))
					return -EIO;
				ntries = 0; 
				do {
					interruptible_sleep_on(&ibmcam->frame[frame].wq);
					if (signal_pending(current)) {
						if (flags & FLAGS_RETRY_VIDIOCSYNC) {
							/* Polling apps will destroy frames with that! */
							ibmcam_new_frame(ibmcam, frame);
							usb_ibmcam_testpattern(ibmcam, 1, 0);
							ibmcam->curframe = -1;
							ibmcam->frame_num++;

							/* This will request another frame. */
							if (waitqueue_active(&ibmcam->frame[frame].wq))
								wake_up_interruptible(&ibmcam->frame[frame].wq);
							return 0;
						} else {
							/* Standard answer: not ready yet! */
							return -EINTR;
						}
					}
				} while (ibmcam->frame[frame].grabstate == FRAME_GRABBING);

				if (ibmcam->frame[frame].grabstate == FRAME_ERROR) {
					int ret = ibmcam_new_frame(ibmcam, frame);
					if (ret < 0)
						return ret;
					goto redo;
				}
			}
			case FRAME_DONE:
				ibmcam->frame[frame].grabstate = FRAME_UNUSED;
				break;
			}

			ibmcam->frame[frame].grabstate = FRAME_UNUSED;

			return 0;
		}
		case VIDIOCGFBUF:
		{
			struct video_buffer vb;

			memset(&vb, 0, sizeof(vb));
			vb.base = NULL;	/* frame buffer not supported, not used */

			if (copy_to_user((void *)arg, (void *)&vb, sizeof(vb)))
				return -EFAULT;

 			return 0;
 		}
		case VIDIOCKEY:
			return 0;

		case VIDIOCCAPTURE:
			return -EINVAL;

		case VIDIOCSFBUF:

		case VIDIOCGTUNER:
		case VIDIOCSTUNER:

		case VIDIOCGFREQ:
		case VIDIOCSFREQ:

		case VIDIOCGAUDIO:
		case VIDIOCSAUDIO:
			return -EINVAL;

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static long ibmcam_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;
	int frmx = -1;
	volatile struct ibmcam_frame *frame;

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_read: %ld bytes, noblock=%d\n", count, noblock);

	if (!IBMCAM_IS_OPERATIONAL(ibmcam) || (buf == NULL))
		return -EFAULT;

	/* See if a frame is completed, then use it. */
	if (ibmcam->frame[0].grabstate >= FRAME_DONE)	/* _DONE or _ERROR */
		frmx = 0;
	else if (ibmcam->frame[1].grabstate >= FRAME_DONE)/* _DONE or _ERROR */
		frmx = 1;

	if (noblock && (frmx == -1))
		return -EAGAIN;

	/* If no FRAME_DONE, look for a FRAME_GRABBING state. */
	/* See if a frame is in process (grabbing), then use it. */
	if (frmx == -1) {
		if (ibmcam->frame[0].grabstate == FRAME_GRABBING)
			frmx = 0;
		else if (ibmcam->frame[1].grabstate == FRAME_GRABBING)
			frmx = 1;
	}

	/* If no frame is active, start one. */
	if (frmx == -1)
		ibmcam_new_frame(ibmcam, frmx = 0);

	frame = &ibmcam->frame[frmx];

restart:
	if (!IBMCAM_IS_OPERATIONAL(ibmcam))
		return -EIO;
	while (frame->grabstate == FRAME_GRABBING) {
		interruptible_sleep_on((void *)&frame->wq);
		if (signal_pending(current))
			return -EINTR;
	}

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
		if (ibmcam_new_frame(ibmcam, frmx))
			printk(KERN_ERR "ibmcam_read: ibmcam_new_frame error\n");
		goto restart;
	}

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_read: frmx=%d, bytes_read=%ld, scanlength=%ld\n",
			frmx, frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count))
		return -EFAULT;

	frame->bytes_read += count;
	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_read: {copy} count used=%ld, new bytes_read=%ld\n",
			count, frame->bytes_read);

	if (frame->bytes_read >= frame->scanlength) { /* All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		ibmcam->frame[frmx].grabstate = FRAME_UNUSED;
		if (ibmcam_new_frame(ibmcam, frmx ? 0 : 1))
			printk(KERN_ERR "ibmcam_read: ibmcam_new_frame returned error\n");
	}

	return count;
}

static int ibmcam_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	if (!IBMCAM_IS_OPERATIONAL(ibmcam))
		return -EFAULT;

	if (size > (((2 * MAX_FRAME_SIZE) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

	pos = (unsigned long)ibmcam->fbuf;
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

static struct video_device ibmcam_template = {
	name:		"CPiA USB Camera",
	type:		VID_TYPE_CAPTURE,
	hardware:	VID_HARDWARE_CPIA,
	open:		ibmcam_open,
	close:		ibmcam_close,
	read:		ibmcam_read,
	write:		ibmcam_write,
	ioctl:		ibmcam_ioctl,
	mmap:		ibmcam_mmap,
};

static void usb_ibmcam_configure_video(struct usb_ibmcam *ibmcam)
{
	if (ibmcam == NULL)
		return;

	RESTRICT_TO_RANGE(init_brightness, 0, 255);
	RESTRICT_TO_RANGE(init_contrast, 0, 255);
	RESTRICT_TO_RANGE(init_color, 0, 255);
	RESTRICT_TO_RANGE(init_hue, 0, 255);
	RESTRICT_TO_RANGE(hue_correction, 0, 255);

	memset(&ibmcam->vpic, 0, sizeof(ibmcam->vpic));
	memset(&ibmcam->vpic_old, 0x55, sizeof(ibmcam->vpic_old));

	ibmcam->vpic.colour = init_color << 8;
	ibmcam->vpic.hue = init_hue << 8;
	ibmcam->vpic.brightness = init_brightness << 8;
	ibmcam->vpic.contrast = init_contrast << 8;
	ibmcam->vpic.whiteness = 105 << 8; /* This one isn't used */
	ibmcam->vpic.depth = 24;
	ibmcam->vpic.palette = VIDEO_PALETTE_RGB24;

	memset(&ibmcam->vcap, 0, sizeof(ibmcam->vcap));
	strcpy(ibmcam->vcap.name, "IBM USB Camera");
	ibmcam->vcap.type = VID_TYPE_CAPTURE;
	ibmcam->vcap.channels = 1;
	ibmcam->vcap.audios = 0;
	ibmcam->vcap.maxwidth = imgwidth;
	ibmcam->vcap.maxheight = imgheight;
	ibmcam->vcap.minwidth = min_imgwidth;
	ibmcam->vcap.minheight = min_imgheight;

	memset(&ibmcam->vchan, 0, sizeof(ibmcam->vchan));
	ibmcam->vchan.flags = 0;
	ibmcam->vchan.tuners = 0;
	ibmcam->vchan.channel = 0;
	ibmcam->vchan.type = VIDEO_TYPE_CAMERA;
	strcpy(ibmcam->vchan.name, "Camera");
}

/*
 * ibmcam_find_struct()
 *
 * This code searches the array of preallocated (static) structures
 * and returns index of the first one that isn't in use. Returns -1
 * if there are no free structures.
 *
 * History:
 * 1/27/00  Created.
 */
static int ibmcam_find_struct(void)
{
	int i, u;

	for (u = 0; u < MAX_IBMCAM; u++) {
		struct usb_ibmcam *ibmcam = &cams[u];
		if (!ibmcam->ibmcam_used) /* This one is free */
		{
			ibmcam->ibmcam_used = 1;	/* In use now */
			for (i=0; i < IBMCAM_NUMFRAMES; i++)
				init_waitqueue_head(&ibmcam->frame[i].wq);
			init_MUTEX(&ibmcam->lock);	/* to 1 == available */
			ibmcam->dev = NULL;
			memcpy(&ibmcam->vdev, &ibmcam_template, sizeof(ibmcam_template));
			return u;
		}
	}
	return -1;
}

/*
 * usb_ibmcam_probe()
 *
 * This procedure queries device descriptor and accepts the interface
 * if it looks like IBM C-it camera.
 *
 * History:
 * 1/22/00  Moved camera init code to ibmcam_open()
 * 1/27/00  Changed to use static structures, added locking.
 * 5/24/00  Corrected to prevent race condition (MOD_xxx_USE_COUNT).
 * 7/3/00   Fixed endianness bug.
 */
static void *usb_ibmcam_probe(struct usb_device *dev, unsigned int ifnum,
			 const struct usb_device_id *id)
{
	struct usb_ibmcam *ibmcam = NULL;
	const struct usb_interface_descriptor *interface;
	const struct usb_endpoint_descriptor *endpoint;
	int devnum, model=0;

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_probe(%p,%u.)\n", dev, ifnum);

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	/* Check the version/revision */
	switch (dev->descriptor.bcdDevice) {
	case 0x0002:
		if (ifnum != 2)
			return NULL;
		printk(KERN_INFO "IBM USB camera found (model 1, rev. 0x%04x).\n",
			dev->descriptor.bcdDevice);
		model = IBMCAM_MODEL_1;
		break;
	case 0x030A:
		if (ifnum != 0)
			return NULL;
		printk(KERN_INFO "IBM USB camera found (model 2, rev. 0x%04x).\n",
			dev->descriptor.bcdDevice);
		model = IBMCAM_MODEL_2;
		break;

	/* ibmcam_table contents prevents any other values from ever
	   being passed to us, so no need for "default" case. */
	}

	/* Validate found interface: must have one ISO endpoint */
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	if (interface->bNumEndpoints != 1) {
		printk(KERN_ERR "IBM camera: interface %d. has %u. endpoints!\n",
		       ifnum, (unsigned)(interface->bNumEndpoints));
		return NULL;
	}
	endpoint = &interface->endpoint[0];
	if ((endpoint->bmAttributes & 0x03) != 0x01) {
		printk(KERN_ERR "IBM camera: interface %d. has non-ISO endpoint!\n", ifnum);
		return NULL;
	}
	if ((endpoint->bEndpointAddress & 0x80) == 0) {
		printk(KERN_ERR "IBM camera: interface %d. has ISO OUT endpoint!\n", ifnum);
		return NULL;
	}

	/* Validate options */
	if (model == IBMCAM_MODEL_1) {
		RESTRICT_TO_RANGE(lighting, 0, 2);
		RESTRICT_TO_RANGE(videosize, VIDEOSIZE_128x96, VIDEOSIZE_352x288);
	} else {
		RESTRICT_TO_RANGE(lighting, 0, 15);
		RESTRICT_TO_RANGE(videosize, VIDEOSIZE_176x144, VIDEOSIZE_352x240);
	}

	/* Code below may sleep, need to lock module while we are here */
	MOD_INC_USE_COUNT;

	devnum = ibmcam_find_struct();
	if (devnum == -1) {
		printk(KERN_INFO "IBM USB camera driver: Too many devices!\n");
		ibmcam = NULL; /* Do not free, it's preallocated */
		goto probe_done;
	}
	ibmcam = &cams[devnum];

	down(&ibmcam->lock);
	ibmcam->camera_model = model;
	ibmcam->remove_pending = 0;
	ibmcam->last_error = 0;
	ibmcam->dev = dev;
	ibmcam->iface = ifnum;
	ibmcam->ifaceAltInactive = 0;
	ibmcam->ifaceAltActive = 1;
	ibmcam->video_endp = endpoint->bEndpointAddress;
	ibmcam->iso_packet_len = 1014;
	ibmcam->compress = 0;
	ibmcam->user=0; 

	usb_ibmcam_configure_video(ibmcam);
	up (&ibmcam->lock);

	if (video_register_device(&ibmcam->vdev, VFL_TYPE_GRABBER) == -1) {
		printk(KERN_ERR "video_register_device failed\n");
		ibmcam = NULL; /* Do not free, it's preallocated */
	}
	if (debug > 1)
		printk(KERN_DEBUG "video_register_device() successful\n");
probe_done:
	MOD_DEC_USE_COUNT;
	return ibmcam;
}

/*
 * usb_ibmcam_release()
 *
 * This code does final release of struct usb_ibmcam. This happens
 * after the device is disconnected -and- all clients closed their files.
 *
 * History:
 * 1/27/00  Created.
 */
static void usb_ibmcam_release(struct usb_ibmcam *ibmcam)
{
	video_unregister_device(&ibmcam->vdev);
	if (debug > 0)
		printk(KERN_DEBUG "usb_ibmcam_release: Video unregistered.\n");
	ibmcam->ibmcam_used = 0;
	ibmcam->initialized = 0;
}

/*
 * usb_ibmcam_disconnect()
 *
 * This procedure stops all driver activity, deallocates interface-private
 * structure (pointed by 'ptr') and after that driver should be removable
 * with no ill consequences.
 *
 * This code handles surprise removal. The ibmcam->user is a counter which
 * increments on open() and decrements on close(). If we see here that
 * this counter is not 0 then we have a client who still has us opened.
 * We set ibmcam->remove_pending flag as early as possible, and after that
 * all access to the camera will gracefully fail. These failures should
 * prompt client to (eventually) close the video device, and then - in
 * ibmcam_close() - we decrement ibmcam->ibmcam_used and usage counter.
 *
 * History:
 * 1/22/00  Added polling of MOD_IN_USE to delay removal until all users gone.
 * 1/27/00  Reworked to allow pending disconnects; see ibmcam_close()
 * 5/24/00  Corrected to prevent race condition (MOD_xxx_USE_COUNT).
 */
static void usb_ibmcam_disconnect(struct usb_device *dev, void *ptr)
{
	static const char proc[] = "usb_ibmcam_disconnect";
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *) ptr;

	MOD_INC_USE_COUNT;

	if (debug > 0)
		printk(KERN_DEBUG "%s(%p,%p.)\n", proc, dev, ptr);

	down(&ibmcam->lock);
	ibmcam->remove_pending = 1; /* Now all ISO data will be ignored */

	/* At this time we ask to cancel outstanding URBs */
	ibmcam_stop_isoc(ibmcam);

	ibmcam->dev = NULL;    	    /* USB device is no more */

	if (ibmcam->user)
		printk(KERN_INFO "%s: In use, disconnect pending.\n", proc);
	else
		usb_ibmcam_release(ibmcam);
	up(&ibmcam->lock);
	printk(KERN_INFO "IBM USB camera disconnected.\n");

	MOD_DEC_USE_COUNT;
}

static struct usb_device_id ibmcam_table [] = {
	{ USB_DEVICE_VER(0x0545, 0x8080, 0x0002, 0x0002) },
	{ USB_DEVICE_VER(0x0545, 0x8080, 0x030a, 0x030a) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, ibmcam_table);

static struct usb_driver ibmcam_driver = {
	name:		"ibmcam",
	probe:		usb_ibmcam_probe,
	disconnect:	usb_ibmcam_disconnect,
	id_table:	ibmcam_table,
};

/*
 * usb_ibmcam_init()
 *
 * This code is run to initialize the driver.
 *
 * History:
 * 1/27/00  Reworked to use statically allocated usb_ibmcam structures.
 */
static int __init usb_ibmcam_init(void)
{
	unsigned u;

	/* Initialize struct */
	for (u = 0; u < MAX_IBMCAM; u++) {
		struct usb_ibmcam *ibmcam = &cams[u];
		memset (ibmcam, 0, sizeof(struct usb_ibmcam));
	}
	return usb_register(&ibmcam_driver);
}

static void __exit usb_ibmcam_cleanup(void)
{
	usb_deregister(&ibmcam_driver);
}

module_init(usb_ibmcam_init);
module_exit(usb_ibmcam_cleanup);


