/*
    bttv - Bt848 frame grabber driver
    
    Copyright (C) 1996,97,98 Ralph  Metzler <rjkm@thp.uni-koeln.de>
                           & Marcus Metzler <mocm@thp.uni-koeln.de>
    (c) 1999-2002 Gerd Knorr <kraxel@bytesex.org>
    
    some v4l2 code lines are taken from Justin's bttv2 driver which is
    (c) 2000 Justin Schoeman <justin@suntiger.ee.up.ac.za>
    
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
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>

#include <asm/io.h>

#include "bttvp.h"
#include "tuner.h"

/* 2.4 / 2.5 driver compatibility stuff */

int bttv_num;			/* number of Bt848s in use */
struct bttv bttvs[BTTV_MAX];

unsigned int bttv_debug = 0;
unsigned int bttv_verbose = 1;
unsigned int bttv_gpio = 0;

/* config variables */
#if defined(__sparc__) || defined(__powerpc__) || defined(__hppa__)
static unsigned int bigendian=1;
#else
static unsigned int bigendian=0;
#endif
static unsigned int radio[4];
static unsigned int irq_debug = 0;
static unsigned int gbuffers = 8;
static unsigned int gbufsize = 0x208000;
static unsigned int fdsr = 0;
static unsigned int latency = -1;

static int video_nr = -1;
static int radio_nr = -1;
static int vbi_nr = -1;

/* options */
static unsigned int combfilter = 0;
static unsigned int lumafilter = 0;
static unsigned int automute   = 1;
static unsigned int chroma_agc = 0;
static unsigned int adc_crush  = 1;

/* API features (turn on/off stuff for testing) */
static unsigned int sloppy     = 0;
static unsigned int mmap       = 1;
#ifdef HAVE_V4L2
static unsigned int v4l2       = 1;
#endif


/* insmod args */
MODULE_PARM(radio,"1-" __MODULE_STRING(BTTV_MAX) "i");
MODULE_PARM_DESC(radio,"The TV card supports radio, default is 0 (no)");
MODULE_PARM(bigendian,"i");
MODULE_PARM_DESC(bigendian,"byte order of the framebuffer, default is native endian");
MODULE_PARM(bttv_verbose,"i");
MODULE_PARM_DESC(bttv_verbose,"verbose startup messages, default is 1 (yes)");
MODULE_PARM(bttv_gpio,"i");
MODULE_PARM_DESC(bttv_gpio,"log gpio changes, default is 0 (no)");
MODULE_PARM(bttv_debug,"i");
MODULE_PARM_DESC(bttv_debug,"debug messages, default is 0 (no)");
MODULE_PARM(irq_debug,"i");
MODULE_PARM_DESC(irq_debug,"irq handler debug messages, default is 0 (no)");
MODULE_PARM(gbuffers,"i");
MODULE_PARM_DESC(gbuffers,"number of capture buffers. range 2-32, default 8");
MODULE_PARM(gbufsize,"i");
MODULE_PARM_DESC(gbufsize,"size of the capture buffers, default is 0x208000");
MODULE_PARM(fdsr,"i");
MODULE_PARM(latency,"i");
MODULE_PARM_DESC(latency,"pci latency timer");

MODULE_PARM(video_nr,"i");
MODULE_PARM(radio_nr,"i");
MODULE_PARM(vbi_nr,"i");

MODULE_PARM(combfilter,"i");
MODULE_PARM(lumafilter,"i");
MODULE_PARM(automute,"i");
MODULE_PARM_DESC(automute,"mute audio on bad/missing video signal, default is 1 (yes)");
MODULE_PARM(chroma_agc,"i");
MODULE_PARM_DESC(chroma_agc,"enables the AGC of chroma signal, default is 0 (no)");
MODULE_PARM(adc_crush,"i");
MODULE_PARM_DESC(adc_crush,"enables the luminance ADC crush, default is 1 (yes)");

MODULE_PARM(sloppy,"i");
MODULE_PARM(mmap,"i");
#ifdef HAVE_V4L2
MODULE_PARM(v4l2,"i");
#endif

MODULE_DESCRIPTION("bttv - v4l/v4l2 driver module for bt848/878 based cards");
MODULE_AUTHOR("Ralph Metzler & Marcus Metzler & Gerd Knorr");
MODULE_LICENSE("GPL");

/* kernel args */
#ifndef MODULE
static int __init p_radio(char *str) { return bttv_parse(str,BTTV_MAX,radio); }
__setup("bttv.radio=", p_radio);
#endif

#ifndef HAVE_V4L2
/* some dummy defines to avoid cluttering up the source code with
   a huge number of ifdef's for V4L2 */
# define V4L2_STD_PAL          -1
# define V4L2_STD_NTSC         -1
# define V4L2_STD_SECAM        -1
# define V4L2_STD_PAL_60       -1
# define V4L2_STD_PAL_M        -1
# define V4L2_STD_PAL_N        -1
# define V4L2_STD_NTSC_N       -1
# define V4L2_PIX_FMT_GREY     -1
# define V4L2_PIX_FMT_HI240    -1
# define V4L2_PIX_FMT_RGB555   -1
# define V4L2_PIX_FMT_RGB555X  -1
# define V4L2_PIX_FMT_RGB565   -1
# define V4L2_PIX_FMT_RGB565X  -1
# define V4L2_PIX_FMT_BGR24    -1
# define V4L2_PIX_FMT_BGR32    -1
# define V4L2_PIX_FMT_RGB32    -1
# define V4L2_PIX_FMT_YUYV     -1
# define V4L2_PIX_FMT_UYVY     -1
# define V4L2_PIX_FMT_YUV422P  -1
# define V4L2_PIX_FMT_YUV420   -1
# define V4L2_PIX_FMT_YVU420   -1
# define V4L2_PIX_FMT_YUV411P  -1
# define V4L2_PIX_FMT_YUV410   -1
# define V4L2_PIX_FMT_YVU410   -1
# define V4L2_BUF_TYPE_CAPTURE -1
# define V4L2_BUF_TYPE_VBI     -1
# define BTTV_APIS "[v4l]"
#else
# define BTTV_APIS "[v4l/v4l2]"
#endif

/* ----------------------------------------------------------------------- */
/* static data                                                             */

const struct bttv_tvnorm bttv_tvnorms[] = {
	/* PAL-BDGHI */
        { V4L2_STD_PAL, 35468950,
          922, 576, 1135, 0x7f, 0x72, (BT848_IFORM_PAL_BDGHI|BT848_IFORM_XT1),
          1135, 186, 922, 0x20, 255},
	/* NTSC */
	{ V4L2_STD_NTSC, 28636363,
          754, 480,  910, 0x70, 0x5d, (BT848_IFORM_NTSC|BT848_IFORM_XT0),
          910, 135, 754, 0x1a, 144},
	/* SECAM */
        { V4L2_STD_SECAM, 35468950,
          922, 576, 1135, 0x7f, 0xa0, (BT848_IFORM_SECAM|BT848_IFORM_XT1),
          1135, 186, 922, 0x20, 255},

	/* these ones are bttv specific (for v4l1) */
        /* PAL-NC */
        { V4L2_STD_PAL_60, 28636363,
          640, 576,  910, 0x7f, 0x72, (BT848_IFORM_PAL_NC|BT848_IFORM_XT0),
          780, 130, 734, 0x1a, 144},
	/* PAL-M */
	{ V4L2_STD_PAL_M, 28636363, 
          640, 480, 910, 0x70, 0x5d, (BT848_IFORM_PAL_M|BT848_IFORM_XT0),
	  780, 135, 754, 0x1a, 144},
	/* PAL-N */
	{ V4L2_STD_PAL_N, 35468950, 
          768, 576, 1135, 0x7f, 0x72, (BT848_IFORM_PAL_N|BT848_IFORM_XT0),
	  944, 186, 922, 0x20, 144},
	/* NTSC-Japan */
	{ V4L2_STD_NTSC_N, 28636363,
          640, 480,  910, 0x70, 0x5d, (BT848_IFORM_NTSC_J|BT848_IFORM_XT0),
	  780, 135, 754, 0x16, 144},
};
const int BTTV_TVNORMS = (sizeof(bttv_tvnorms)/sizeof(struct bttv_tvnorm));

/* ----------------------------------------------------------------------- */
/* bttv format list
   packed pixel formats must come first */
const struct bttv_format bttv_formats[] = {
	{
		name:     "8 bpp, gray",
		palette:  VIDEO_PALETTE_GREY,
		fourcc:   V4L2_PIX_FMT_GREY,
		btformat: BT848_COLOR_FMT_Y8,
		depth:    8,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "8 bpp, dithered color",
		palette:  VIDEO_PALETTE_HI240,
		fourcc:   V4L2_PIX_FMT_HI240,
		btformat: BT848_COLOR_FMT_RGB8,
		depth:    8,
		flags:    FORMAT_FLAGS_PACKED | FORMAT_FLAGS_DITHER,
	},{
		name:     "15 bpp RGB, le",
		palette:  VIDEO_PALETTE_RGB555,
		fourcc:   V4L2_PIX_FMT_RGB555,
		btformat: BT848_COLOR_FMT_RGB15,
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "15 bpp RGB, be",
		palette:  -1,
		fourcc:   V4L2_PIX_FMT_RGB555X,
		btformat: BT848_COLOR_FMT_RGB15,
		btswap:   0x03, /* byteswap */
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "16 bpp RGB, le",
		palette:  VIDEO_PALETTE_RGB565,
		fourcc:   V4L2_PIX_FMT_RGB565,
		btformat: BT848_COLOR_FMT_RGB16,
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "16 bpp RGB, be",
		palette:  -1,
		fourcc:   V4L2_PIX_FMT_RGB565X,
		btformat: BT848_COLOR_FMT_RGB16,
		btswap:   0x03, /* byteswap */
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "24 bpp RGB, le",
		palette:  VIDEO_PALETTE_RGB24,
		fourcc:   V4L2_PIX_FMT_BGR24,
		btformat: BT848_COLOR_FMT_RGB24,
		depth:    24,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "32 bpp RGB, le",
		palette:  VIDEO_PALETTE_RGB32,
		fourcc:   V4L2_PIX_FMT_BGR32,
		btformat: BT848_COLOR_FMT_RGB32,
		depth:    32,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "32 bpp RGB, be",
		palette:  -1,
		fourcc:   V4L2_PIX_FMT_RGB32,
		btformat: BT848_COLOR_FMT_RGB32,
		btswap:   0x0f, /* byte+word swap */
		depth:    32,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "4:2:2, packed, YUYV",
		palette:  VIDEO_PALETTE_YUV422,
		fourcc:   V4L2_PIX_FMT_YUYV,
		btformat: BT848_COLOR_FMT_YUY2,
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "4:2:2, packed, YUYV",
		palette:  VIDEO_PALETTE_YUYV,
		fourcc:   V4L2_PIX_FMT_YUYV,
		btformat: BT848_COLOR_FMT_YUY2,
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "4:2:2, packed, UYVY",
		palette:  VIDEO_PALETTE_UYVY,
		fourcc:   V4L2_PIX_FMT_UYVY,
		btformat: BT848_COLOR_FMT_YUY2,
		btswap:   0x03, /* byteswap */
		depth:    16,
		flags:    FORMAT_FLAGS_PACKED,
	},{
		name:     "4:2:2, planar, Y-Cb-Cr",
		palette:  VIDEO_PALETTE_YUV422P,
		fourcc:   V4L2_PIX_FMT_YUV422P,
		btformat: BT848_COLOR_FMT_YCrCb422,
		depth:    16,
		flags:    FORMAT_FLAGS_PLANAR,
		hshift:   1,
		vshift:   0,
	},{
		name:     "4:2:0, planar, Y-Cb-Cr",
		palette:  VIDEO_PALETTE_YUV420P,
		fourcc:   V4L2_PIX_FMT_YUV420,
		btformat: BT848_COLOR_FMT_YCrCb422,
		depth:    12,
		flags:    FORMAT_FLAGS_PLANAR,
		hshift:   1,
		vshift:   1,
	},{
		name:     "4:2:0, planar, Y-Cr-Cb",
		palette:  -1,
		fourcc:   V4L2_PIX_FMT_YVU420,
		btformat: BT848_COLOR_FMT_YCrCb422,
		depth:    12,
		flags:    FORMAT_FLAGS_PLANAR | FORMAT_FLAGS_CrCb,
		hshift:   1,
		vshift:   1,
	},{
		name:     "4:1:1, planar, Y-Cb-Cr",
		palette:  VIDEO_PALETTE_YUV411P,
		fourcc:   V4L2_PIX_FMT_YUV411P,
		btformat: BT848_COLOR_FMT_YCrCb411,
		depth:    12,
		flags:    FORMAT_FLAGS_PLANAR,
		hshift:   2,
		vshift:   0,
	},{
		name:     "4:1:0, planar, Y-Cb-Cr",
		palette:  VIDEO_PALETTE_YUV410P,
		fourcc:   V4L2_PIX_FMT_YUV410,
		btformat: BT848_COLOR_FMT_YCrCb411,
		depth:    9,
		flags:    FORMAT_FLAGS_PLANAR,
		hshift:   2,
		vshift:   2,
	},{
		name:     "4:1:0, planar, Y-Cr-Cb",
		palette:  -1,
		fourcc:   V4L2_PIX_FMT_YVU410,
		btformat: BT848_COLOR_FMT_YCrCb411,
		depth:    9,
		flags:    FORMAT_FLAGS_PLANAR | FORMAT_FLAGS_CrCb,
		hshift:   2,
		vshift:   2,
	},{
		name:     "raw scanlines",
		palette:  VIDEO_PALETTE_RAW,
		fourcc:   -1,
		btformat: BT848_COLOR_FMT_RAW,
		depth:    8,
		flags:    FORMAT_FLAGS_RAW,
	}
};
const int BTTV_FORMATS = (sizeof(bttv_formats)/sizeof(struct bttv_format));

/* ----------------------------------------------------------------------- */

#ifdef HAVE_V4L2
#define V4L2_CID_PRIVATE_CHROMA_AGC  (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_COMBFILTER  (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PRIVATE_AUTOMUTE    (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PRIVATE_LUMAFILTER  (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PRIVATE_AGC_CRUSH   (V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 5)

static const struct v4l2_queryctrl no_ctl = {
	name:  "42",
	flags: V4L2_CTRL_FLAG_DISABLED,
};
static const struct v4l2_queryctrl bttv_ctls[] = {
	/* --- video --- */
	{
		id:            V4L2_CID_BRIGHTNESS,
		name:          "Brightness",
		minimum:       0,
		maximum:       65535,
		step:          256,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_VIDEO,
		group:         "Video",
	},{
		id:            V4L2_CID_CONTRAST,
		name:          "Contrast",
		minimum:       0,
		maximum:       65535,
		step:          128,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_VIDEO,
		group:         "Video",
	},{
		id:            V4L2_CID_SATURATION,
		name:          "Saturation",
		minimum:       0,
		maximum:       65535,
		step:          128,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_VIDEO,
		group:         "Video",
	},{
		id:            V4L2_CID_HUE,
		name:          "Hue",
		minimum:       0,
		maximum:       65535,
		step:          256,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_VIDEO,
		group:         "Video",
	},
	/* --- audio --- */
	{
		id:            V4L2_CID_AUDIO_MUTE,
		name:          "Mute",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
		category:      V4L2_CTRL_CAT_AUDIO,
		group:         "Audio",
	},{
		id:            V4L2_CID_AUDIO_VOLUME,
		name:          "Volume",
		minimum:       0,
		maximum:       65535,
		step:          65535/100,
		default_value: 65535,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_AUDIO,
		group:         "Audio",
	},{
		id:            V4L2_CID_AUDIO_BALANCE,
		name:          "Balance",
		minimum:       0,
		maximum:       65535,
		step:          65535/100,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_AUDIO,
		group:         "Audio",
	},{
		id:            V4L2_CID_AUDIO_BASS,
		name:          "Bass",
		minimum:       0,
		maximum:       65535,
		step:          65535/100,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_AUDIO,
		group:         "Audio",
	},{
		id:            V4L2_CID_AUDIO_TREBLE,
		name:          "Treble",
		minimum:       0,
		maximum:       65535,
		step:          65535/100,
		default_value: 32768,
		type:          V4L2_CTRL_TYPE_INTEGER,
		category:      V4L2_CTRL_CAT_AUDIO,
		group:         "Audio",
	},
	/* --- private --- */
	{
		id:            V4L2_CID_PRIVATE_CHROMA_AGC,
		name:          "chroma agc",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
		group:         "Private",
	},{
		id:            V4L2_CID_PRIVATE_COMBFILTER,
		name:          "combfilter",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
		group:         "Private",
	},{
		id:            V4L2_CID_PRIVATE_AUTOMUTE,
		name:          "automute",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
		group:         "Private",
	},{
		id:            V4L2_CID_PRIVATE_LUMAFILTER,
		name:          "luma decimation filter",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
		group:         "Private",
	},{
		id:            V4L2_CID_PRIVATE_AGC_CRUSH,
		name:          "agc crush",
		minimum:       0,
		maximum:       1,
		type:          V4L2_CTRL_TYPE_BOOLEAN,
		group:         "Private",
	}
};
const int BTTV_CTLS = (sizeof(bttv_ctls)/sizeof(struct v4l2_queryctrl));
#endif /* HAVE_V4L2 */

/* ----------------------------------------------------------------------- */
/* resource management                                                     */

static
int check_alloc_btres(struct bttv *btv, struct bttv_fh *fh, int bit)
{
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	down(&btv->reslock);
	if (btv->resources & bit) {
		/* no, someone else uses it */
		up(&btv->reslock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	btv->resources |= bit;
	up(&btv->reslock);
	return 1;
}

static
int check_btres(struct bttv_fh *fh, int bit)
{
	return (fh->resources & bit);
}

static
int locked_btres(struct bttv *btv, int bit)
{
	return (btv->resources & bit);
}

static
void free_btres(struct bttv *btv, struct bttv_fh *fh, int bits)
{
#if 1 /* DEBUG */
	if ((fh->resources & bits) != bits) {
		/* trying to free ressources not allocated by us ... */
		printk("bttv: BUG! (btres)\n");
	}
#endif
	down(&btv->reslock);
	fh->resources  &= ~bits;
	btv->resources &= ~bits;
	up(&btv->reslock);
}

/* ----------------------------------------------------------------------- */
/*
 * sanity check for video framebuffer address ranges (overlay).
 * let's see if that address range actually belongs to some
 * pci display adapter.
 *
 * FIXME: stuff isn't portable.  It's also a v4l API bug, pass a
 * physical address in VIDIOCSFBUF isn't portable too ...
 */

static int
find_videomem(unsigned long from, unsigned long to)
{
#if PCI_DMA_BUS_IS_PHYS
	struct pci_dev *dev = NULL;
	int i,match,found;

	found = 0;
	dprintk(KERN_DEBUG "bttv: checking video framebuffer address"
		" (%lx-%lx)\n",from,to);
        pci_for_each_dev(dev) {
		if (dev->class != PCI_CLASS_NOT_DEFINED_VGA &&
		    dev->class >> 16 != PCI_BASE_CLASS_DISPLAY)
			continue;
		dprintk(KERN_DEBUG
			"  pci display adapter %04x:%04x at %02x:%02x.%x\n",
			dev->vendor,dev->device,dev->bus->number,
			PCI_SLOT(dev->devfn),PCI_FUNC(dev->devfn));
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			if (!(dev->resource[i].flags & IORESOURCE_MEM))
				continue;
			if (dev->resource[i].flags & IORESOURCE_READONLY)
				continue;
			match = (from >= dev->resource[i].start) &&
				(to-1 <= dev->resource[i].end);
			if (match)
				found = 1;
			dprintk(KERN_DEBUG "    memory at %08lx-%08lx%s\n",
				dev->resource[i].start,
				dev->resource[i].end,
				match ? "  (check passed)" : "");
		}
	}
	return found;
#else
	/* Hmm, the physical address passed to us is probably bogous */
	dprintk(KERN_DEBUG "bttv: no overlay for this arch, sorry\n");
	return 0;
#endif
}

/* ----------------------------------------------------------------------- */
/* If Bt848a or Bt849, use PLL for PAL/SECAM and crystal for NTSC          */

/* Frequency = (F_input / PLL_X) * PLL_I.PLL_F/PLL_C 
   PLL_X = Reference pre-divider (0=1, 1=2) 
   PLL_C = Post divider (0=6, 1=4)
   PLL_I = Integer input 
   PLL_F = Fractional input 
   
   F_input = 28.636363 MHz: 
   PAL (CLKx2 = 35.46895 MHz): PLL_X = 1, PLL_I = 0x0E, PLL_F = 0xDCF9, PLL_C = 0
*/

static void set_pll_freq(struct bttv *btv, unsigned int fin, unsigned int fout)
{
        unsigned char fl, fh, fi;
        
        /* prevent overflows */
        fin/=4;
        fout/=4;

        fout*=12;
        fi=fout/fin;

        fout=(fout%fin)*256;
        fh=fout/fin;

        fout=(fout%fin)*256;
        fl=fout/fin;

        btwrite(fl, BT848_PLL_F_LO);
        btwrite(fh, BT848_PLL_F_HI);
        btwrite(fi|BT848_PLL_X, BT848_PLL_XCI);
}

static int set_pll(struct bttv *btv)
{
        int i;

        if (!btv->pll.pll_crystal)
                return 0;

        if (btv->pll.pll_ifreq == btv->pll.pll_ofreq) {
                /* no PLL needed */
                if (btv->pll.pll_current == 0)
                        return 0;
		vprintk("bttv%d: PLL: switching off\n",btv->nr);
                btwrite(0x00,BT848_TGCTRL);
                btwrite(0x00,BT848_PLL_XCI);
                btv->pll.pll_current = 0;
                return 0;
        }

        if (btv->pll.pll_ofreq == btv->pll.pll_current)
                return 0;

	vprintk("bttv%d: PLL: %d => %d ",btv->nr,
		btv->pll.pll_ifreq, btv->pll.pll_ofreq);
	set_pll_freq(btv, btv->pll.pll_ifreq, btv->pll.pll_ofreq);

        for (i=0; i<10; i++) {
		/*  Let other people run while the PLL stabilizes */
		vprintk(".");
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/10);
		
                if (btread(BT848_DSTATUS) & BT848_DSTATUS_PLOCK) {
			btwrite(0,BT848_DSTATUS);
                } else {
                        btwrite(0x08,BT848_TGCTRL);
                        btv->pll.pll_current = btv->pll.pll_ofreq;
			vprintk(" ok\n");
                        return 0;
                }
        }
        btv->pll.pll_current = -1;
	vprintk("failed\n");
        return -1;
}

/* ----------------------------------------------------------------------- */

static void bt848_bright(struct bttv *btv, int bright)
{
	int value;

	btv->bright = bright;

	/* We want -128 to 127 we get 0-65535 */
	value = (bright >> 8) - 128;
	btwrite(value & 0xff, BT848_BRIGHT);
}

static void bt848_hue(struct bttv *btv, int hue)
{
	int value;
	
	btv->hue = hue;

	/* -128 to 127 */
	value = (hue >> 8) - 128;
        btwrite(value & 0xff, BT848_HUE);
}

static void bt848_contrast(struct bttv *btv, int cont)
{
	int value,hibit;
	
	btv->contrast = cont;
	
	/* 0-511 */
	value = (cont  >> 7);
	hibit = (value >> 6) & 4;
        btwrite(value & 0xff, BT848_CONTRAST_LO);
        btaor(hibit, ~4, BT848_E_CONTROL);
        btaor(hibit, ~4, BT848_O_CONTROL);
}

static void bt848_sat(struct bttv *btv, int color)
{
	int val_u,val_v,hibits;
	
	btv->saturation = color;

	/* 0-511 for the color */
	val_u   = color >> 7;
	val_v   = ((color>>7)*180L)/254;
        hibits  = (val_u >> 7) & 2;
	hibits |= (val_v >> 8) & 1;
        btwrite(val_u & 0xff, BT848_SAT_U_LO);
        btwrite(val_v & 0xff, BT848_SAT_V_LO);
        btaor(hibits, ~3, BT848_E_CONTROL);
        btaor(hibits, ~3, BT848_O_CONTROL);
}

/* ----------------------------------------------------------------------- */

static int
video_mux(struct bttv *btv, unsigned int input)
{
	int mux,mask2;

	if (input >= bttv_tvcards[btv->type].video_inputs)
		return -EINVAL;

        /* needed by RemoteVideo MX */
	mask2 = bttv_tvcards[btv->type].gpiomask2;
	if (mask2)
		btaor(mask2,~mask2,BT848_GPIO_OUT_EN);

#if 0
	/* This seems to get rid of some synchronization problems */
	btand(~(3<<5), BT848_IFORM);
	schedule_timeout(HZ/10);
#endif

	if (input==bttv_tvcards[btv->type].svhs)  {
		btor(BT848_CONTROL_COMP, BT848_E_CONTROL);
		btor(BT848_CONTROL_COMP, BT848_O_CONTROL);
	} else {
		btand(~BT848_CONTROL_COMP, BT848_E_CONTROL);
		btand(~BT848_CONTROL_COMP, BT848_O_CONTROL);
	}
	mux = bttv_tvcards[btv->type].muxsel[input] & 3;
	btaor(mux<<5, ~(3<<5), BT848_IFORM);
	dprintk(KERN_DEBUG "bttv%d: video mux: input=%d mux=%d\n",
		btv->nr,input,mux);

	/* card specific hook */
	if(bttv_tvcards[btv->type].muxsel_hook)
		bttv_tvcards[btv->type].muxsel_hook (btv, input);
	return 0;
}

static char *audio_modes[] = {
	"audio: tuner", "audio: radio", "audio: extern",
	"audio: intern", "audio: off"
};

static int
audio_mux(struct bttv *btv, int mode)
{
	int val,mux,i2c_mux,signal;
	
	btaor(bttv_tvcards[btv->type].gpiomask,
	      ~bttv_tvcards[btv->type].gpiomask,BT848_GPIO_OUT_EN);
	signal = btread(BT848_DSTATUS) & BT848_DSTATUS_HLOC;

	switch (mode) {
	case AUDIO_MUTE:
		btv->audio |= AUDIO_MUTE;
		break;
	case AUDIO_UNMUTE:
		btv->audio &= ~AUDIO_MUTE;
		break;
	case AUDIO_TUNER:
	case AUDIO_RADIO:
	case AUDIO_EXTERN:
	case AUDIO_INTERN:
		btv->audio &= AUDIO_MUTE;
		btv->audio |= mode;
	}
	i2c_mux = mux = (btv->audio & AUDIO_MUTE) ? AUDIO_OFF : btv->audio;
	if (btv->opt_automute && !signal && !btv->radio_user)
		mux = AUDIO_OFF;
	printk("bttv%d: amux: mode=%d audio=%d signal=%s mux=%d/%d irq=%s\n",
	       btv->nr, mode, btv->audio, signal ? "yes" : "no",
	       mux, i2c_mux, in_interrupt() ? "yes" : "no");

	val = bttv_tvcards[btv->type].audiomux[mux];
	btaor(val,~bttv_tvcards[btv->type].gpiomask, BT848_GPIO_DATA);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,audio_modes[mux]);
	if (!in_interrupt())
		bttv_call_i2c_clients(btv,AUDC_SET_INPUT,&(i2c_mux));
	return 0;
}

static void
i2c_vidiocschan(struct bttv *btv)
{
	struct video_channel c;

	memset(&c,0,sizeof(c));
	c.norm    = btv->tvnorm;
	c.channel = btv->input;
	bttv_call_i2c_clients(btv,VIDIOCSCHAN,&c);
	if (btv->type == BTTV_VOODOOTV_FM)
		bttv_tda9880_setnorm(btv,c.norm);
}

static int
set_tvnorm(struct bttv *btv, unsigned int norm)
{
	const struct bttv_tvnorm *tvnorm;

	if (norm < 0 || norm >= BTTV_TVNORMS)
		return -EINVAL;

	btv->tvnorm = norm;
	tvnorm = &bttv_tvnorms[norm];

	btwrite(tvnorm->adelay, BT848_ADELAY);
	btwrite(tvnorm->bdelay, BT848_BDELAY);
	btaor(tvnorm->iform,~(BT848_IFORM_NORM|BT848_IFORM_XTBOTH),
	      BT848_IFORM);
	btwrite(tvnorm->vbipack, BT848_VBI_PACK_SIZE);
	btwrite(1, BT848_VBI_PACK_DEL);
	
	btv->pll.pll_ofreq = tvnorm->Fsc;
	set_pll(btv);
	return 0;
}

static void
set_input(struct bttv *btv, unsigned int input)
{
	btv->input = input;
	video_mux(btv,input);
	audio_mux(btv,(input == bttv_tvcards[btv->type].tuner ?
		       AUDIO_TUNER : AUDIO_EXTERN));
	set_tvnorm(btv,btv->tvnorm);
}

static void init_bt848(struct bttv *btv)
{
	int val;
	
	btwrite(0, BT848_SRESET);
	btwrite(0x00, BT848_CAP_CTL);
	btwrite(BT848_COLOR_CTL_GAMMA, BT848_COLOR_CTL);
	btwrite(BT848_IFORM_XTAUTO | BT848_IFORM_AUTO, BT848_IFORM);

        /* set planar and packed mode trigger points and         */
        /* set rising edge of inverted GPINTR pin as irq trigger */
        btwrite(BT848_GPIO_DMA_CTL_PKTP_32|
                BT848_GPIO_DMA_CTL_PLTP1_16|
                BT848_GPIO_DMA_CTL_PLTP23_16|
                BT848_GPIO_DMA_CTL_GPINTC|
                BT848_GPIO_DMA_CTL_GPINTI, 
                BT848_GPIO_DMA_CTL);

	val = btv->opt_chroma_agc ? BT848_SCLOOP_CAGC : 0;
        btwrite(val, BT848_E_SCLOOP);
        btwrite(val, BT848_O_SCLOOP);

        btwrite(0x20, BT848_E_VSCALE_HI);
        btwrite(0x20, BT848_O_VSCALE_HI);
        btwrite(BT848_ADC_RESERVED | (btv->opt_adc_crush ? BT848_ADC_CRUSH : 0),
		BT848_ADC);

	if (btv->opt_lumafilter) {
		btwrite(0, BT848_E_CONTROL);
		btwrite(0, BT848_O_CONTROL);
	} else {
		btwrite(BT848_CONTROL_LDEC, BT848_E_CONTROL);
		btwrite(BT848_CONTROL_LDEC, BT848_O_CONTROL);
	}
}

extern void bttv_reinit_bt848(struct bttv *btv)
{
	unsigned long flags;

	if (bttv_verbose)
		printk(KERN_INFO "bttv%d: reset, reinitialize\n",btv->nr);
	spin_lock_irqsave(&btv->s_lock,flags);
	btv->errors=0;
	bttv_set_dma(btv,0,0);
	spin_unlock_irqrestore(&btv->s_lock,flags);

	init_bt848(btv);
        btv->pll.pll_current = -1;
	set_input(btv,btv->input);
}

#ifdef HAVE_V4L2
static int get_control(struct bttv *btv, struct v4l2_control *c)
{
	struct video_audio va;
	int i;
	
	for (i = 0; i < BTTV_CTLS; i++)
		if (bttv_ctls[i].id == c->id)
			break;
	if (i == BTTV_CTLS)
		return -EINVAL;
	if (bttv_ctls[i].category == V4L2_CTRL_CAT_AUDIO) {
		memset(&va,0,sizeof(va));
		bttv_call_i2c_clients(btv, VIDIOCGAUDIO, &va);
		if (btv->audio_hook)
			btv->audio_hook(btv,&va,0);
	}
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->value = btv->bright;
		break;
	case V4L2_CID_HUE:
		c->value = btv->hue;
		break;
	case V4L2_CID_CONTRAST:
		c->value = btv->contrast;
		break;
	case V4L2_CID_SATURATION:
		c->value = btv->saturation;
		break;

	case V4L2_CID_AUDIO_MUTE:
		c->value =  (VIDEO_AUDIO_MUTE == va.flags) ? 1 : 0;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		c->value = va.volume;
		break;
	case V4L2_CID_AUDIO_BALANCE:
		c->value = va.balance;
		break;
	case V4L2_CID_AUDIO_BASS:
		c->value = va.bass;
		break;
	case V4L2_CID_AUDIO_TREBLE:
		c->value = va.treble;
		break;

	case V4L2_CID_PRIVATE_CHROMA_AGC:
		c->value = btv->opt_chroma_agc;
		break;
	case V4L2_CID_PRIVATE_COMBFILTER:
		c->value = btv->opt_combfilter;
		break;
	case V4L2_CID_PRIVATE_LUMAFILTER:
		c->value = btv->opt_lumafilter;
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
		c->value = btv->opt_automute;
		break;
	case V4L2_CID_PRIVATE_AGC_CRUSH:
		c->value = btv->opt_adc_crush;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int set_control(struct bttv *btv, struct v4l2_control *c)
{
	struct video_audio va;
	int i,val;

	for (i = 0; i < BTTV_CTLS; i++)
		if (bttv_ctls[i].id == c->id)
			break;
	if (i == BTTV_CTLS)
		return -EINVAL;
	if (bttv_ctls[i].category == V4L2_CTRL_CAT_AUDIO) {
		memset(&va,0,sizeof(va));
		bttv_call_i2c_clients(btv, VIDIOCGAUDIO, &va);
		if (btv->audio_hook)
			btv->audio_hook(btv,&va,0);
	}
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		bt848_bright(btv,c->value);
		break;
	case V4L2_CID_HUE:
		bt848_hue(btv,c->value);
		break;
	case V4L2_CID_CONTRAST:
		bt848_contrast(btv,c->value);
		break;
	case V4L2_CID_SATURATION:
		bt848_sat(btv,c->value);
		break;
	case V4L2_CID_AUDIO_MUTE:
		if (c->value) {
			va.flags |= VIDEO_AUDIO_MUTE;
			audio_mux(btv, AUDIO_MUTE);
		} else {
			va.flags &= ~VIDEO_AUDIO_MUTE;
			audio_mux(btv, AUDIO_UNMUTE);
		}
		break;

	case V4L2_CID_AUDIO_VOLUME:
		va.volume = c->value;
		break;
	case V4L2_CID_AUDIO_BALANCE:
		va.balance = c->value;
		break;
	case V4L2_CID_AUDIO_BASS:
		va.bass = c->value;
		break;
	case V4L2_CID_AUDIO_TREBLE:
		va.treble = c->value;
		break;

	case V4L2_CID_PRIVATE_CHROMA_AGC:
		btv->opt_chroma_agc = c->value;
		val = btv->opt_chroma_agc ? BT848_SCLOOP_CAGC : 0;
		btwrite(val, BT848_E_SCLOOP);
		btwrite(val, BT848_O_SCLOOP);
		break;
	case V4L2_CID_PRIVATE_COMBFILTER:
		btv->opt_combfilter = c->value;
		break;
	case V4L2_CID_PRIVATE_LUMAFILTER:
		btv->opt_lumafilter = c->value;
		if (btv->opt_lumafilter) {
			btand(~BT848_CONTROL_LDEC, BT848_E_CONTROL);
			btand(~BT848_CONTROL_LDEC, BT848_O_CONTROL);
		} else {
			btor(BT848_CONTROL_LDEC, BT848_E_CONTROL);
			btor(BT848_CONTROL_LDEC, BT848_O_CONTROL);
		}
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
		btv->opt_automute = c->value;
		break;
	case V4L2_CID_PRIVATE_AGC_CRUSH:
		btv->opt_adc_crush = c->value;
		btwrite(BT848_ADC_RESERVED | (btv->opt_adc_crush ? BT848_ADC_CRUSH : 0),
			BT848_ADC);
		break;
	default:
		return -EINVAL;
	}
	if (bttv_ctls[i].category == V4L2_CTRL_CAT_AUDIO) {
		bttv_call_i2c_clients(btv, VIDIOCSAUDIO, &va);
		if (btv->audio_hook)
			btv->audio_hook(btv,&va,1);
	}
	return 0;
}
#endif /* HAVE_V4L2 */

/* ----------------------------------------------------------------------- */

void bttv_gpio_tracking(struct bttv *btv, char *comment)
{
	unsigned int outbits, data;
	outbits = btread(BT848_GPIO_OUT_EN);
	data    = btread(BT848_GPIO_DATA);
	printk(KERN_DEBUG "bttv%d: gpio: en=%08x, out=%08x in=%08x [%s]\n",
	       btv->nr,outbits,data & outbits, data & ~outbits, comment);
}

void bttv_field_count(struct bttv *btv)
{
	int need_count = 0;

	if (locked_btres(btv,RESOURCE_STREAMING))
		need_count++;
	if (btv->vbi.users)
		need_count++;

	if (need_count) {
		/* start field counter */
		btor(BT848_INT_VSYNC,BT848_INT_MASK);
	} else {
		/* stop field counter */
		btand(~BT848_INT_VSYNC,BT848_INT_MASK);
		btv->field_count = 0;
	}
}

static const struct bttv_format*
format_by_palette(int palette)
{
	int i;

	for (i = 0; i < BTTV_FORMATS; i++) {
		if (-1 == bttv_formats[i].palette)
			continue;
		if (bttv_formats[i].palette == palette)
			return bttv_formats+i;
	}
	return NULL;
}

#ifdef HAVE_V4L2
static const struct bttv_format*
format_by_fourcc(int fourcc)
{
	int i;

	for (i = 0; i < BTTV_FORMATS; i++) {
		if (-1 == bttv_formats[i].fourcc)
			continue;
		if (bttv_formats[i].fourcc == fourcc)
			return bttv_formats+i;
	}
	return NULL;
}
#endif

/* ----------------------------------------------------------------------- */
/* misc helpers                                                            */

static int
bttv_switch_overlay(struct bttv *btv, struct bttv_fh *fh,
		    struct bttv_buffer *new)
{
	struct bttv_buffer *old;
	unsigned long flags;
	int retval = 0;

	if (new)
		new->vb.state = STATE_DONE;
	spin_lock_irqsave(&btv->s_lock,flags);
	old = btv->screen;
	btv->screen = new;
	bttv_set_dma(btv, 0x03, 1);
	spin_unlock_irqrestore(&btv->s_lock,flags);
	if (NULL == new)
		free_btres(btv,fh,RESOURCE_OVERLAY);
	if (NULL != old)
		bttv_dma_free(btv, old);
	return retval;
}

/* ----------------------------------------------------------------------- */
/* video4linux (1) interface                                               */

static int bttv_prepare_buffer(struct bttv *btv, struct bttv_buffer *buf,
 			       const struct bttv_format *fmt,
			       int width, int height, int field)
{
	int redo_dma_risc = 0;
	int rc;
	
	/* check settings */
	if (NULL == fmt)
		return -EINVAL;
	if (fmt->btformat == BT848_COLOR_FMT_RAW) {
		width  = RAW_BPL;
		height = RAW_LINES*2;
		if (width*height > buf->vb.bsize)
			return -EINVAL;
		buf->vb.size = buf->vb.bsize;
	} else {
		if (width  < 48 ||
		    height < 32 ||
		    width  > bttv_tvnorms[btv->tvnorm].swidth ||
		    height > bttv_tvnorms[btv->tvnorm].sheight)
			return -EINVAL;
		buf->vb.size = (width * height * fmt->depth) >> 3;
		if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
			return -EINVAL;
		field = bttv_buffer_field(btv, field, VBUF_FIELD_EVEN,
					  btv->tvnorm, height);
	}
	
	/* alloc + fill struct bttv_buffer (if changed) */
	if (buf->vb.width != width || buf->vb.height != height ||
	    buf->vb.field != field ||
	    buf->tvnorm != btv->tvnorm || buf->fmt != fmt) {
		buf->vb.width  = width;
		buf->vb.height = height;
		buf->vb.field  = field;
		buf->tvnorm    = btv->tvnorm;
		buf->fmt       = fmt;
		redo_dma_risc = 1;
	}

#if 0
	if (STATE_NEEDS_INIT == buf->vb.state)
		if (redo_dma_risc)
			bttv_dma_free(btv,buf);
	}
#endif

	/* alloc risc memory */
	if (STATE_NEEDS_INIT == buf->vb.state) {
		redo_dma_risc = 1;
		if (0 != (rc = videobuf_iolock(btv->dev,&buf->vb)))
			goto fail;
	}

	if (redo_dma_risc)
		if (0 != (rc = bttv_buffer_risc(btv,buf)))
			goto fail;

	buf->vb.state = STATE_PREPARED;
	return 0;

 fail:
	bttv_dma_free(btv,buf);
	return rc;
}

static int
buffer_setup(struct file *file, int *count, int *size)
{
	struct bttv_fh *fh = file->private_data;
	
	*size = fh->buf.fmt->depth*fh->buf.vb.width*fh->buf.vb.height >> 3;
	if (0 == *count)
		*count = gbuffers;
	while (*size * *count > gbuffers * gbufsize)
		(*count)--;
	return 0;
}

static int
buffer_prepare(struct file *file, struct videobuf_buffer *vb, int field)
{
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;
	struct bttv_fh *fh = file->private_data;

	return bttv_prepare_buffer(fh->btv,buf,fh->buf.fmt,
				   fh->buf.vb.width,fh->buf.vb.height,field);
}

static void
buffer_queue(struct file *file, struct videobuf_buffer *vb)
{
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;
	struct bttv_fh *fh = file->private_data;

	buf->vb.state = STATE_QUEUED;
	list_add_tail(&buf->vb.queue,&fh->btv->capture);
	bttv_set_dma(fh->btv, 0x03, 1);
}

static void buffer_release(struct file *file, struct videobuf_buffer *vb)
{
	struct bttv_buffer *buf = (struct bttv_buffer*)vb;
	struct bttv_fh *fh = file->private_data;

	bttv_dma_free(fh->btv,buf);
}

static struct videobuf_queue_ops bttv_qops = {
	buf_setup:    buffer_setup,
	buf_prepare:  buffer_prepare,
	buf_queue:    buffer_queue,
	buf_release:  buffer_release,
};

static const char *v4l1_ioctls[] = {
	"?", "CGAP", "GCHAN", "SCHAN", "GTUNER", "STUNER", "GPICT", "SPICT",
	"CCAPTURE", "GWIN", "SWIN", "GFBUF", "SFBUF", "KEY", "GFREQ",
	"SFREQ", "GAUDIO", "SAUDIO", "SYNC", "MCAPTURE", "GMBUF", "GUNIT",
	"GCAPTURE", "SCAPTURE", "SPLAYMODE", "SWRITEMODE", "GPLAYINFO",
	"SMICROCODE", "GVBIFMT", "SVBIFMT" };
#define V4L1_IOCTLS (sizeof(v4l1_ioctls)/sizeof(char*))

static const char *v4l2_ioctls[] = {
	"QUERYCAP", "1", "ENUM_PIXFMT", "ENUM_FBUFFMT", "G_FMT", "S_FMT",
	"G_COMP", "S_COMP", "REQBUFS", "QUERYBUF", "G_FBUF", "S_FBUF",
	"G_WIN", "S_WIN", "PREVIEW", "QBUF", "16", "DQBUF", "STREAMON",
	"STREAMOFF", "G_PERF", "G_PARM", "S_PARM", "G_STD", "S_STD",
	"ENUMSTD", "ENUMINPUT", "G_CTRL", "S_CTRL", "G_TUNER", "S_TUNER",
	"G_FREQ", "S_FREQ", "G_AUDIO", "S_AUDIO", "35", "QUERYCTRL",
	"QUERYMENU", "G_INPUT", "S_INPUT", "ENUMCVT", "41", "42", "43",
	"44", "45",  "G_OUTPUT", "S_OUTPUT", "ENUMOUTPUT", "G_AUDOUT",
	"S_AUDOUT", "ENUMFX", "G_EFFECT", "S_EFFECT", "G_MODULATOR",
	"S_MODULATOR"
};
#define V4L2_IOCTLS (sizeof(v4l2_ioctls)/sizeof(char*))

int bttv_common_ioctls(struct bttv *btv, unsigned int cmd, void *arg)
{
	switch (cmd) {
        case BTTV_VERSION:
                return BTTV_VERSION_CODE;

	/* ***  v4l1  *** ************************************************ */
	case VIDIOCGFREQ:
#ifdef HAVE_V4L2
	case VIDIOC_G_FREQ:
#endif
	{
		unsigned long *freq = arg;
		*freq = btv->freq;
		return 0;
	}
	case VIDIOCSFREQ:
#ifdef HAVE_V4L2
	case VIDIOC_S_FREQ:
#endif
	{
		unsigned long *freq = arg;
		down(&btv->lock);
		btv->freq=*freq;
		bttv_call_i2c_clients(btv,VIDIOCSFREQ,freq);
		if (btv->has_matchbox && btv->radio_user)
			tea5757_set_freq(btv,*freq);
		up(&btv->lock);
		return 0;
	}

	case VIDIOCGTUNER:
	{
		struct video_tuner *v = arg;
		
		if (v->tuner) /* Only tuner 0 */
			return -EINVAL;
		strcpy(v->name, "Television");
		v->rangelow  = 0;
		v->rangehigh = 0x7FFFFFFF;
		v->flags     = VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
		v->mode      = btv->tvnorm;
		v->signal    = (btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC) ? 0xFFFF : 0;
		bttv_call_i2c_clients(btv,cmd,v);
		return 0;
	}
	case VIDIOCSTUNER:
	{
		struct video_tuner *v = arg;

		if (v->tuner) /* Only tuner 0 */
			return -EINVAL;
		if (v->mode >= BTTV_TVNORMS)
			return -EINVAL;

		down(&btv->lock);
		set_tvnorm(btv,v->mode);
		bttv_call_i2c_clients(btv,cmd,v);
		up(&btv->lock);
		return 0;
	}
	
        case VIDIOCGCHAN:
        {
                struct video_channel *v = arg;

                if (v->channel >= bttv_tvcards[btv->type].video_inputs)
                        return -EINVAL;
                v->tuners=0;
                v->flags = VIDEO_VC_AUDIO;
                v->type = VIDEO_TYPE_CAMERA;
                v->norm = btv->tvnorm;
                if(v->channel == bttv_tvcards[btv->type].tuner)  {
                        strcpy(v->name,"Television");
                        v->flags|=VIDEO_VC_TUNER;
                        v->type=VIDEO_TYPE_TV;
                        v->tuners=1;
                } else if (v->channel == bttv_tvcards[btv->type].svhs) {
                        strcpy(v->name,"S-Video");
                } else {
                        sprintf(v->name,"Composite%d",v->channel);
		}
		return 0;
        }
        case VIDIOCSCHAN:
        {
                struct video_channel *v = arg;

		if (v->channel <  0 ||
		    v->channel >= bttv_tvcards[btv->type].video_inputs)
			return -EINVAL;
		if (v->norm >= BTTV_TVNORMS)
			return -EINVAL;

		down(&btv->lock);
		if (v->channel == btv->input &&
		    v->norm    == btv->tvnorm) {
			/* nothing to do */
			up(&btv->lock);
			return 0;
		}

		btv->tvnorm = v->norm;
		set_input(btv,v->channel);
		up(&btv->lock);
		return 0;
	}

        case VIDIOCGAUDIO:
	{
		struct video_audio *v = arg;

		memset(v,0,sizeof(*v));
		strcpy(v->name,"Television");
		v->flags |= VIDEO_AUDIO_MUTABLE;
		v->mode  = VIDEO_SOUND_MONO;

		down(&btv->lock);
		bttv_call_i2c_clients(btv,cmd,v);

		/* card specific hooks */
		if (btv->audio_hook)
			btv->audio_hook(btv,v,0);

		up(&btv->lock);
		return 0;
	}
	case VIDIOCSAUDIO:
	{
		struct video_audio *v = arg;

		if(v->audio <  0 ||
		   v->audio >= bttv_tvcards[btv->type].audio_inputs)
			return -EINVAL;

		down(&btv->lock);
		audio_mux(btv, (v->flags&VIDEO_AUDIO_MUTE) ? AUDIO_MUTE : AUDIO_UNMUTE);
		bttv_call_i2c_clients(btv,cmd,v);

		/* card specific hooks */
		if (btv->audio_hook)
			btv->audio_hook(btv,v,1);
		
		up(&btv->lock);
		return 0;
	}

#ifdef HAVE_V4L2
	/* ***  v4l2  *** ************************************************ */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_enumstd *e = arg;

		if (e->index < 0 || e->index >= BTTV_TVNORMS)
			return -EINVAL;
		v4l2_video_std_construct(&e->std, bttv_tvnorms[e->index].v4l2_id, 0);
		e->inputs  = 0x0f;
		e->outputs = 0x00;
		return 0;
	}
	case VIDIOC_G_STD:
	{
		struct v4l2_standard *s = arg;
		v4l2_video_std_construct(s,bttv_tvnorms[btv->tvnorm].v4l2_id,0);
		return 0;
	}
	case VIDIOC_S_STD:
	{
		struct v4l2_standard *s = arg;
		int i, id = v4l2_video_std_confirm(s);

		for(i = 0; i < BTTV_TVNORMS; i++)
			if (id == bttv_tvnorms[i].v4l2_id)
				break;
		if (i == BTTV_TVNORMS)
			return -EINVAL;

		down(&btv->lock);
		set_tvnorm(btv,i);
		i2c_vidiocschan(btv);
		up(&btv->lock);
		return 0;
	}

	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;

		if (i->index >= bttv_tvcards[btv->type].video_inputs)
			return -EINVAL;
		i->type = V4L2_INPUT_TYPE_CAMERA;
		i->capability = 0;
		i->assoc_audio = 0;
		if (i->index == bttv_tvcards[btv->type].tuner) {
			sprintf(i->name, "Television");
			i->type = V4L2_INPUT_TYPE_TUNER;
			i->capability = V4L2_INPUT_CAP_AUDIO;
		} else if (i->index==bttv_tvcards[btv->type].svhs) {
			sprintf(i->name, "S-Video");
		} else {
                        sprintf(i->name,"Composite%d",i->index);
		}
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *i = arg;
		*i = btv->input;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		int *i = arg;
		
		if (*i < 0 || *i > bttv_tvcards[btv->type].video_inputs)
			return -EINVAL;
		down(&btv->lock);
		set_input(btv,*i);
		i2c_vidiocschan(btv);
		up(&btv->lock);
		return 0;
	}
	
	case VIDIOC_G_TUNER: {
		struct v4l2_tuner *t = arg;

		down(&btv->lock);
		memset(t,0,sizeof(*t));
		t->input = bttv_tvcards[btv->type].tuner;
		strcpy(t->name, "Television");
		v4l2_video_std_construct(&t->std, bttv_tvnorms[btv->tvnorm].v4l2_id, 0);
		t->capability = V4L2_TUNER_CAP_NORM;
		t->rangehigh = 0xffffffffUL;
		t->rxsubchans = V4L2_TUNER_SUB_MONO;
		if (btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC)
			t->signal = 0xffff;
		{
			/* Hmmm ... */
			struct video_audio va;
			memset(&va, 0, sizeof(struct video_audio));
			bttv_call_i2c_clients(btv, VIDIOCGAUDIO, &va);
			if (btv->audio_hook)
				btv->audio_hook(btv,&va,0);
			if(va.mode & VIDEO_SOUND_STEREO)
				t->rxsubchans |= V4L2_TUNER_SUB_STEREO;
			if(va.mode & VIDEO_SOUND_LANG1)
				t->rxsubchans |= V4L2_TUNER_SUB_LANG1;
			if(va.mode & VIDEO_SOUND_LANG2)
				t->rxsubchans |= V4L2_TUNER_SUB_LANG2;
		}
		/* FIXME: fill capability+audmode */
		up(&btv->lock);
		return 0;
	}
	case VIDIOC_S_TUNER: {
		struct v4l2_tuner *t = arg;
		
		if(t->input!=bttv_tvcards[btv->type].tuner)
			return -EINVAL;
		down(&btv->lock);
		{
			struct video_audio va;
			memset(&va, 0, sizeof(struct video_audio));
			if (t->audmode == V4L2_TUNER_MODE_MONO)
				va.mode = VIDEO_SOUND_MONO;
			else if (t->audmode == V4L2_TUNER_MODE_STEREO)
				va.mode = VIDEO_SOUND_STEREO;
			else if (t->audmode == V4L2_TUNER_MODE_LANG1)
				va.mode = VIDEO_SOUND_LANG1;
			else if (t->audmode == V4L2_TUNER_MODE_LANG2)
				va.mode = VIDEO_SOUND_LANG2;
			bttv_call_i2c_clients(btv, VIDIOCSAUDIO, &va);
			if (btv->audio_hook)
				btv->audio_hook(btv,&va,1);
		}
		up(&btv->lock);
		return 0;
	}
#endif /* HAVE_V4L2 */

	default:
		return -ENOIOCTLCMD;
	
	}
	return 0;
}

static int setup_window(struct bttv_fh *fh, struct bttv *btv,
			int x, int y, int width, int height,
			struct video_clip *user_clips, int nclips)
{
	struct video_clip *clips = NULL;
	int n,size,retval = 0;
	
	if (width  < 48 ||
	    height < 32 ||
	    width  > bttv_tvnorms[btv->tvnorm].swidth ||
	    height > bttv_tvnorms[btv->tvnorm].sheight ||
	    NULL == fh->ovfmt)
		return -EINVAL;
	if (nclips > 2048)
		return -EINVAL;

	/* copy clips  --  luckily v4l1 + v4l2 are binary
	   compatible here ...*/
	n = nclips;
	size = sizeof(struct video_clip)*(n+4);
	clips = kmalloc(size,GFP_KERNEL);
	if (NULL == clips)
		return -ENOMEM;
	if (n > 0) {
		if (copy_from_user(clips,user_clips,
				   sizeof(struct video_clip)*nclips)) {
			kfree(clips);
			return -EFAULT;
		}
	}
	/* clip against screen */
	if (NULL != btv->fbuf.base)
		n = bttv_screen_clips(&btv->fbuf, x, y,
				      width, height,
				      clips, n);
	bttv_sort_clips(clips,nclips);
	
	down(&fh->q.lock);
	if (fh->ov.clips)
		kfree(fh->ov.clips);
	fh->ov.clips   = clips;
	fh->ov.nclips  = nclips;
	
	fh->ov.x       = x;
	fh->ov.y       = y;
	fh->ov.width   = width;
	fh->ov.height  = height;
	btv->init.ov.width   = width;
	btv->init.ov.height  = height;
	
	/* update overlay if needed */
	retval = 0;
	if (check_btres(fh, RESOURCE_OVERLAY)) {
		struct bttv_buffer *new;
		
		new = videobuf_alloc(sizeof(*new));
		bttv_overlay_risc(btv, &fh->ov, fh->ovfmt, new);
		retval = bttv_switch_overlay(btv,fh,new);
	}
	up(&fh->q.lock);
	return retval;
}

static int bttv_do_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, void *arg)
{
	struct bttv_fh *fh  = file->private_data;
	struct bttv    *btv = fh->btv;
	unsigned long flags;
	int retval;

	if (bttv_debug > 1) {
		switch (_IOC_TYPE(cmd)) {
		case 'v':
			printk("bttv%d: ioctl 0x%x (v4l1, VIDIOC%s)\n",
			       btv->nr, cmd, (_IOC_NR(cmd) < V4L1_IOCTLS) ?
			       v4l1_ioctls[_IOC_NR(cmd)] : "???");
			break;
		case 'V':
			printk("bttv%d: ioctl 0x%x (v4l2, VIDIOC_%s)\n",
			       btv->nr, cmd,  (_IOC_NR(cmd) < V4L2_IOCTLS) ?
			       v4l2_ioctls[_IOC_NR(cmd)] : "???");
			break;
		default:
			printk("bttv%d: ioctl 0x%x (???)\n",
			       btv->nr, cmd);
		}
	}
	if (btv->errors)
		bttv_reinit_bt848(btv);

	switch (cmd) {

	/* ***  v4l1  *** ************************************************ */
	case VIDIOCGCAP:
	{
                struct video_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->name,btv->video_dev.name);
                cap->type = VID_TYPE_CAPTURE|
			VID_TYPE_TUNER|
			VID_TYPE_OVERLAY|
			VID_TYPE_CLIPPING|
                        VID_TYPE_SCALES;
                cap->channels  = bttv_tvcards[btv->type].video_inputs;
                cap->audios    = bttv_tvcards[btv->type].audio_inputs;
                cap->maxwidth  = bttv_tvnorms[btv->tvnorm].swidth;
                cap->maxheight = bttv_tvnorms[btv->tvnorm].sheight;
                cap->minwidth  = 48;
                cap->minheight = 32;
                return 0;
	}

	case VIDIOCGPICT:
	{
		struct video_picture *pic = arg;

		memset(pic,0,sizeof(*pic));
		pic->brightness = btv->bright;
		pic->contrast   = btv->contrast;
		pic->hue        = btv->hue;
		pic->colour     = btv->saturation;
		if (fh->buf.fmt) {
			pic->depth   = fh->buf.fmt->depth;
			pic->palette = fh->buf.fmt->palette;
		}
		return 0;
	}
	case VIDIOCSPICT:
	{
		struct video_picture *pic = arg;
		const struct bttv_format *fmt;
		
		fmt = format_by_palette(pic->palette);
		if (NULL == fmt)
			return -EINVAL;
		down(&fh->q.lock);
		retval = -EINVAL;
		if (fmt->depth != pic->depth && !sloppy)
			goto fh_unlock_and_return;
		fh->ovfmt   = fmt;
		fh->buf.fmt = fmt;
		btv->init.ovfmt   = fmt;
		btv->init.buf.fmt = fmt;
		if (bigendian) {
			/* dirty hack time:  swap bytes for overlay if the
			   display adaptor is big endian (insmod option) */
			if (fmt->palette == VIDEO_PALETTE_RGB555 ||
			    fmt->palette == VIDEO_PALETTE_RGB565 ||
			    fmt->palette == VIDEO_PALETTE_RGB32) {
				fh->ovfmt = fmt+1;
			}
		}
		bt848_bright(btv,pic->brightness);
		bt848_contrast(btv,pic->contrast);
		bt848_hue(btv,pic->hue);
		bt848_sat(btv,pic->colour);
		up(&fh->q.lock);
                return 0;
	}

	case VIDIOCGWIN:
	{
		struct video_window *win = arg;

		memset(win,0,sizeof(*win));
		win->x      = fh->ov.x;
		win->y      = fh->ov.y;
		win->width  = fh->ov.width;
		win->height = fh->ov.height;
		return 0;
	}
	case VIDIOCSWIN:
	{
		struct video_window *win = arg;

		retval = setup_window(fh,btv,win->x,win->y,
				      win->width,win->height,
				      win->clips,
				      win->clipcount);
		if (0 == retval) {
			/* on v4l1 this ioctl affects the read() size too */
			fh->buf.vb.width  = fh->ov.width;
			fh->buf.vb.height = fh->ov.height;
			btv->init.buf.vb.width  = fh->ov.width;
			btv->init.buf.vb.height = fh->ov.height;
		}
		return retval;
	}

	case VIDIOCGFBUF:
	{
		struct video_buffer *fbuf = arg;
		*fbuf = btv->fbuf;
		return 0;
	}
	case VIDIOCSFBUF:
	{
		struct video_buffer *fbuf = arg;
		const struct bttv_format *fmt;
		unsigned long end;

		if(!capable(CAP_SYS_ADMIN) &&
                   !capable(CAP_SYS_RAWIO))
                        return -EPERM;
		end = (unsigned long)fbuf->base +
			fbuf->height * fbuf->bytesperline;
		if (0 == find_videomem((unsigned long)fbuf->base,end))
			return -EINVAL;
		down(&fh->q.lock);
		retval = -EINVAL;
		if (sloppy) {
			/* also set the default palette -- for backward
			   compatibility with older versions */
			switch (fbuf->depth) {
			case 8:
				fmt = format_by_palette(VIDEO_PALETTE_HI240);
				break;
			case 16:
				fmt = format_by_palette(VIDEO_PALETTE_RGB565);
				break;
			case 24:
				fmt = format_by_palette(VIDEO_PALETTE_RGB24);
				break;
			case 32:
				fmt = format_by_palette(VIDEO_PALETTE_RGB32);
				break;
			case 15:
				fbuf->depth = 16;
				fmt = format_by_palette(VIDEO_PALETTE_RGB555);
				break;
			default:
				fmt = NULL;
				break;
			}
			if (NULL == fmt)
				goto fh_unlock_and_return;
			fh->ovfmt   = fmt;
			fh->buf.fmt = fmt;
			btv->init.ovfmt   = fmt;
			btv->init.buf.fmt = fmt;
		} else {
			if (15 == fbuf->depth)
				fbuf->depth = 16;
			if (fbuf->depth !=  8 && fbuf->depth != 16 &&
			    fbuf->depth != 24 && fbuf->depth != 32)
				goto fh_unlock_and_return;
		}
		btv->fbuf = *fbuf;
		up(&fh->q.lock);
		return 0;
	}

	case VIDIOCCAPTURE:
#ifdef HAVE_V4L2
	case VIDIOC_PREVIEW:
#endif
	{
		struct bttv_buffer *new;
		int *on = arg;

		if (*on) {
			/* verify args */
			if (NULL == btv->fbuf.base)
				return -EINVAL;
			if (fh->ov.width <48 ||
			    fh->ov.height<32 ||
			    fh->ov.width >bttv_tvnorms[btv->tvnorm].swidth  ||
			    fh->ov.height>bttv_tvnorms[btv->tvnorm].sheight ||
			    NULL == fh->ovfmt)
				return -EINVAL;
		}

		if (!check_alloc_btres(btv,fh,RESOURCE_OVERLAY))
			return -EBUSY;
		
		down(&fh->q.lock);
		if (*on) {
			fh->ov.tvnorm = btv->tvnorm;
			new = videobuf_alloc(sizeof(*new));
			bttv_overlay_risc(btv, &fh->ov, fh->ovfmt, new);
		} else {
			new = NULL;
		}

		/* switch over */
	        retval = bttv_switch_overlay(btv,fh,new);
		up(&fh->q.lock);
		return retval;
	}

	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		int i;

		if (!mmap)
			return -EINVAL;
		down(&fh->q.lock);
		retval = videobuf_mmap_setup(file,&fh->q,gbuffers,gbufsize);
		if (retval < 0)
			goto fh_unlock_and_return;
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = gbuffers;
		mbuf->size   = gbuffers * gbufsize;
		for (i = 0; i < gbuffers; i++)
			mbuf->offsets[i] = i * gbufsize;
		up(&fh->q.lock);
		return 0;
	}
	case VIDIOCMCAPTURE:
	{
		struct video_mmap *vm = arg;
		struct bttv_buffer *buf;

		if (vm->frame >= VIDEO_MAX_FRAME)
			return -EINVAL;

		down(&fh->q.lock);
		retval = -EINVAL;
		buf = (struct bttv_buffer *)fh->q.bufs[vm->frame];
		if (NULL == buf)
			goto fh_unlock_and_return;
		if (0 == buf->vb.baddr)
			goto fh_unlock_and_return;
		if (buf->vb.state == STATE_QUEUED ||
		    buf->vb.state == STATE_ACTIVE)
			goto fh_unlock_and_return;
		
		retval = bttv_prepare_buffer(btv,buf,
					     format_by_palette(vm->format),
					     vm->width,vm->height,0);
		if (0 != retval)
			goto fh_unlock_and_return;
		spin_lock_irqsave(&btv->s_lock,flags);
		buffer_queue(file,&buf->vb);
		spin_unlock_irqrestore(&btv->s_lock,flags);
		up(&fh->q.lock);
		return 0;
	}
	case VIDIOCSYNC:
	{
		int *frame = arg;
		struct bttv_buffer *buf;

		if (*frame >= VIDEO_MAX_FRAME)
			return -EINVAL;

		down(&fh->q.lock);
		retval = -EINVAL;
		buf = (struct bttv_buffer *)fh->q.bufs[*frame];
		if (NULL == buf)
			goto fh_unlock_and_return;
		retval = videobuf_waiton(&buf->vb,0,1);
		if (0 != retval)
			goto fh_unlock_and_return;
		switch (buf->vb.state) {
		case STATE_ERROR:
			retval = -EIO;
			/* fall through */
		case STATE_DONE:
			videobuf_dma_pci_sync(btv->dev,&buf->vb.dma);
			bttv_dma_free(btv,buf);
			break;
		default:
			retval = -EINVAL;
			break;
		}
		up(&fh->q.lock);
		return retval;
	}

        case BTTV_VERSION:
        case VIDIOCGFREQ:
        case VIDIOCSFREQ:
        case VIDIOCGTUNER:
        case VIDIOCSTUNER:
        case VIDIOCGCHAN:
        case VIDIOCSCHAN:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
		return bttv_common_ioctls(btv,cmd,arg);


#ifdef HAVE_V4L2
	/* ***  v4l2  *** ************************************************ */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		if (0 == v4l2)
			return -EINVAL;
                strcpy(cap->name,btv->video_dev.name);
		cap->type = V4L2_TYPE_CAPTURE;
		cap->flags = V4L2_FLAG_TUNER | V4L2_FLAG_PREVIEW
			| V4L2_FLAG_READ | V4L2_FLAG_SELECT;
		if (mmap)
			cap->flags |= V4L2_FLAG_STREAMING;
		cap->inputs = bttv_tvcards[btv->type].video_inputs;
		cap->outputs = 0;
		cap->audios = bttv_tvcards[btv->type].audio_inputs;
		cap->maxwidth = bttv_tvnorms[btv->tvnorm].swidth;
		cap->maxheight = bttv_tvnorms[btv->tvnorm].sheight;
		cap->minwidth = 48;
		cap->minheight = 32;
		cap->maxframerate = 30;
		return 0;
	}

	case VIDIOC_ENUM_PIXFMT:
	case VIDIOC_ENUM_FBUFFMT:
	{
		struct v4l2_fmtdesc *f = arg;
		int i, index;

		index = -1;
		for (i = 0; i < BTTV_FORMATS; i++) {
			if (bttv_formats[i].fourcc != -1)
				index++;
			if (index == f->index)
				break;
		}
		if (BTTV_FORMATS == i)
			return -EINVAL;
		if (cmd == VIDIOC_ENUM_FBUFFMT &&
		    0 == (bttv_formats[i].flags & FORMAT_FLAGS_PACKED))
			return -EINVAL;
		memset(f,0,sizeof(*f));
		f->index = index;
		strncpy(f->description,bttv_formats[i].name,31);
		f->pixelformat = bttv_formats[i].fourcc;
		f->depth = bttv_formats[i].depth;
		return 0;
	}

	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;

		memset(f,0,sizeof(*f));
		f->type = V4L2_BUF_TYPE_CAPTURE;
		f->fmt.pix.width        = fh->buf.vb.width;
		f->fmt.pix.height       = fh->buf.vb.height;
		f->fmt.pix.depth        = fh->buf.fmt->depth;
		f->fmt.pix.pixelformat  = fh->buf.fmt->fourcc;
		f->fmt.pix.sizeimage    =
			(fh->buf.vb.width*fh->buf.vb.height*fh->buf.fmt->depth)/8;
		return 0;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;
		const struct bttv_format *fmt;

		if ((f->type & V4L2_BUF_TYPE_field) != V4L2_BUF_TYPE_CAPTURE)
			return -EINVAL;
		fmt = format_by_fourcc(f->fmt.pix.pixelformat);
		if (NULL == fmt)
			return -EINVAL;
		if (f->fmt.pix.width  < 48 ||
		    f->fmt.pix.height < 32)
			return -EINVAL;
		if (f->fmt.pix.flags & V4L2_FMT_FLAG_BYTESPERLINE)
			/* FIXME -- not implemented yet */
			return -EINVAL;

		down(&fh->q.lock);
		/* fixup format */
		if (f->fmt.pix.width  > bttv_tvnorms[btv->tvnorm].swidth)
			f->fmt.pix.width = bttv_tvnorms[btv->tvnorm].swidth;
		if (f->fmt.pix.height > bttv_tvnorms[btv->tvnorm].sheight)
			f->fmt.pix.height = bttv_tvnorms[btv->tvnorm].sheight;
		if (!(f->fmt.pix.flags & V4L2_FMT_FLAG_INTERLACED) &&
		    f->fmt.pix.height>bttv_tvnorms[btv->tvnorm].sheight/2)
			f->fmt.pix.height=bttv_tvnorms[btv->tvnorm].sheight/2;

		if (f->fmt.pix.height > bttv_tvnorms[btv->tvnorm].sheight/2) {
			/* must interlace -- no field splitting available */
			f->fmt.pix.flags &= ~(V4L2_FMT_FLAG_TOPFIELD|
					      V4L2_FMT_FLAG_BOTFIELD);
		} else {
			/* one field is enouth -- no interlace needed */
			f->fmt.pix.flags &= ~V4L2_FMT_FLAG_INTERLACED;
		}
		
		/* update our state informations */
		fh->buf.fmt             = fmt;
		fh->buf.vb.width        = f->fmt.pix.width;
		fh->buf.vb.height       = f->fmt.pix.height;
		btv->init.buf.fmt       = fmt;
		btv->init.buf.vb.width  = f->fmt.pix.width;
		btv->init.buf.vb.height = f->fmt.pix.height;

		/* update data for the application */
		f->fmt.pix.depth = fmt->depth;
		f->fmt.pix.sizeimage =
			(fh->buf.vb.width * fh->buf.vb.height * fmt->depth)/8;
		up(&fh->q.lock);
		return 0;
	}

	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;

		memset(fb,0,sizeof(*fb));
		fb->base[0]    = btv->fbuf.base;
		fb->fmt.width  = btv->fbuf.width;
		fb->fmt.height = btv->fbuf.height;
		fb->fmt.bytesperline = btv->fbuf.bytesperline;
		fb->fmt.flags  = V4L2_FMT_FLAG_BYTESPERLINE;
		fb->capability = V4L2_FBUF_CAP_CLIPPING;
		if (fh->ovfmt) {
			fb->fmt.depth  = fh->ovfmt->depth;
			fb->fmt.pixelformat  = fh->ovfmt->fourcc;
		}
		return 0;
	}
	case VIDIOC_S_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;
		const struct bttv_format *fmt;
		unsigned long end;
		
		if(!capable(CAP_SYS_ADMIN) &&
		   !capable(CAP_SYS_RAWIO))
			return -EPERM;

		/* check args */
		end = (unsigned long)fb->base[0] +
			fb->fmt.height * fb->fmt.bytesperline;
		if (0 == find_videomem((unsigned long)fb->base[0],end))
			return -EINVAL;

		fmt = format_by_fourcc(fb->fmt.pixelformat);
		if (NULL == fmt)
			return -EINVAL;
		if (0 == (fmt->flags & FORMAT_FLAGS_PACKED))
			return -EINVAL;

		down(&fh->q.lock);
		retval = -EINVAL;
		if (fb->flags & V4L2_FBUF_FLAG_OVERLAY) {
			if (fb->fmt.width > bttv_tvnorms[btv->tvnorm].swidth)
				goto fh_unlock_and_return;
			if (fb->fmt.height > bttv_tvnorms[btv->tvnorm].sheight)
				goto fh_unlock_and_return;
		}

		/* ok, accept it */
		btv->fbuf.base   = fb->base[0];
		btv->fbuf.width  = fb->fmt.width;
		btv->fbuf.height = fb->fmt.height;
		btv->fbuf.depth  = fmt->depth;
		if (fb->fmt.flags & V4L2_FMT_FLAG_BYTESPERLINE)
			btv->fbuf.bytesperline = fb->fmt.bytesperline;
		else
			btv->fbuf.bytesperline = btv->fbuf.width*fmt->depth/8;
		
		retval = 0;
		fh->ovfmt = fmt;
		btv->init.ovfmt = fmt;
		if (fb->flags & V4L2_FBUF_FLAG_OVERLAY) {
			fh->ov.x      = 0;
			fh->ov.y      = 0;
			fh->ov.width  = fb->fmt.width;
			fh->ov.height = fb->fmt.height;
			btv->init.ov.width  = fb->fmt.width;
			btv->init.ov.height = fb->fmt.height;
			if (fh->ov.clips)
				kfree(fh->ov.clips);
			fh->ov.clips = NULL;
			fh->ov.nclips = 0;

			if (check_btres(fh, RESOURCE_OVERLAY)) {
				struct bttv_buffer *new;
		
				new = videobuf_alloc(sizeof(*new));
				bttv_overlay_risc(btv,&fh->ov,fh->ovfmt,new);
				retval = bttv_switch_overlay(btv,fh,new);
			}
		}
		up(&fh->q.lock);
		return retval;
	}
	case VIDIOC_G_WIN:
	{
		struct v4l2_window *win = arg;

		memset(win,0,sizeof(*win));
		win->x      = fh->ov.x;
		win->y      = fh->ov.y;
		win->width  = fh->ov.width;
		win->height = fh->ov.height;
		return 0;
	}
	case VIDIOC_S_WIN:
	{
		struct v4l2_window *win = arg;

		return setup_window(fh,btv,win->x,win->y,
				    win->width,win->height,
				    (struct video_clip*)win->clips,
				    win->clipcount);
	}

	case VIDIOC_REQBUFS:
		if (!mmap)
			return -EINVAL;
		return videobuf_reqbufs(file,&fh->q,arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(&fh->q,arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(file,&fh->q,arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(file,&fh->q,arg);

	case VIDIOC_STREAMON:
		if (!check_alloc_btres(btv,fh,RESOURCE_STREAMING))
			return -EBUSY;
		bttv_field_count(btv);
		return videobuf_streamon(file,&fh->q);

	case VIDIOC_STREAMOFF:
		retval = videobuf_streamoff(file,&fh->q);
		if (retval < 0)
			return retval;
		free_btres(btv,fh,RESOURCE_STREAMING);
		bttv_field_count(btv);
		return 0;

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = arg;
		int i;

		v4l2_fill_ctrl_category(c);
		if ((c->id <  V4L2_CID_BASE ||
		     c->id >= V4L2_CID_LASTP1) &&
		    (c->id <  V4L2_CID_PRIVATE_BASE ||
		     c->id >= V4L2_CID_PRIVATE_LASTP1))
			return -EINVAL;
		for (i = 0; i < BTTV_CTLS; i++)
			if (bttv_ctls[i].id == c->id)
				break;
		if (i == BTTV_CTLS) {
			*c = no_ctl;
			return 0;
		}
		*c = bttv_ctls[i];
		if (bttv_ctls[i].category == V4L2_CTRL_CAT_AUDIO) {
			struct video_audio va;
			memset(&va,0,sizeof(va));
			bttv_call_i2c_clients(btv, VIDIOCGAUDIO, &va);
			if (btv->audio_hook)
				btv->audio_hook(btv,&va,0);
			switch (bttv_ctls[i].id) {
			case V4L2_CID_AUDIO_VOLUME:
				if (!(va.flags & VIDEO_AUDIO_VOLUME))
					*c = no_ctl;
				break;
			case V4L2_CID_AUDIO_BALANCE:
				if (!(va.flags & VIDEO_AUDIO_BALANCE))
					*c = no_ctl;
				break;
			case V4L2_CID_AUDIO_BASS:
				if (!(va.flags & VIDEO_AUDIO_BASS))
					*c = no_ctl;
				break;
			case V4L2_CID_AUDIO_TREBLE:
				if (!(va.flags & VIDEO_AUDIO_TREBLE))
					*c = no_ctl;
				break;
			}
		}
		return 0;
	}
	case VIDIOC_G_CTRL:
		return get_control(btv,arg);
	case VIDIOC_S_CTRL:
		return set_control(btv,arg);
	case VIDIOC_G_PARM:
	{
		struct v4l2_streamparm *parm = arg;
		struct v4l2_standard s;
		if (parm->type != V4L2_BUF_TYPE_CAPTURE)
			return -EINVAL;
		memset(parm,0,sizeof(*parm));
		v4l2_video_std_construct(&s, bttv_tvnorms[btv->tvnorm].v4l2_id, 0);
		parm->parm.capture.timeperframe = v4l2_video_std_tpf(&s);
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

 fh_unlock_and_return:
	up(&fh->q.lock);
	return retval;
}

static int bttv_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, bttv_do_ioctl);
}

#if 0
/*
 * blocking read for a complete video frame
 * => no kernel bounce buffer needed.
 */
static ssize_t
bttv_read_zerocopy(struct bttv_fh *fh, struct bttv *btv,
		   char *data,size_t count, loff_t *ppos)
{
        int rc;

        /* setup stuff */
	dprintk("bttv%d: read zerocopy\n",btv->nr);
        fh->q.read_buf->baddr = (unsigned long)data;
        fh->q.read_buf->bsize = count;
        rc = bttv_prepare_buffer(btv,fh->q.read_buf,fh->buf.fmt,
				 fh->buf.vb.width,fh->buf.vb.height,0);
        if (0 != rc)
		goto done;
	
        /* start capture & wait */
        bttv_queue_buffer(btv,fh->q.read_buf);
        rc = videobuf_waiton(fh->q.read_buf,0,1);
        if (0 == rc) {
		videobuf_dma_pci_sync(btv->dev,&fh->q.read_buf->dma);
                rc = fh->q.read_buf->size;
	}

 done:
	/* cleanup */
	bttv_dma_free(btv,fh->q.read_buf);
	fh->q.read_buf->baddr = 0;
	fh->q.read_buf->size = 0;
	return rc;
}
#endif


static ssize_t bttv_read(struct file *file, char *data,
			 size_t count, loff_t *ppos)
{
	struct bttv_fh *fh = file->private_data;

	if (fh->btv->errors)
		bttv_reinit_bt848(fh->btv);
	if (locked_btres(fh->btv,RESOURCE_STREAMING))
		return -EBUSY;

	return videobuf_read_one(file, &fh->q, data, count, ppos);
}

static unsigned int bttv_poll(struct file *file, poll_table *wait)
{
	struct bttv_fh *fh = file->private_data;
	struct bttv_buffer *buf;

	if (check_btres(fh,RESOURCE_STREAMING)) {
		/* streaming capture */
		if (list_empty(&fh->q.stream))
			return POLLERR;
		buf = list_entry(fh->q.stream.next,struct bttv_buffer,vb.stream);
	} else {
		/* read() capture */
		down(&fh->q.lock);
		if (NULL == fh->q.read_buf) {
			/* need to capture a new frame */
			if (locked_btres(fh->btv,RESOURCE_STREAMING)) {
				up(&fh->q.lock);
				return POLLERR;
			}
			fh->q.read_buf = videobuf_alloc(fh->q.msize);
			if (NULL == fh->q.read_buf) {
				up(&fh->q.lock);
				return POLLERR;
			}
			if (0 != fh->q.ops->buf_prepare(file,fh->q.read_buf,0)) {
				up(&fh->q.lock);
				return POLLERR;
			}
			fh->q.ops->buf_queue(file,fh->q.read_buf);
			fh->q.read_off = 0;
		}
		up(&fh->q.lock);
		buf = (struct bttv_buffer*)fh->q.read_buf;
	}
	
	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == STATE_DONE ||
	    buf->vb.state == STATE_ERROR)
		return POLLIN|POLLRDNORM;
	return 0;
}

static int bttv_open(struct inode *inode, struct file *file)
{
	unsigned int minor = minor(inode->i_rdev);
	struct bttv *btv = NULL;
	struct bttv_fh *fh;
	int i;

	dprintk(KERN_DEBUG "bttv: open minor=%d\n",minor);

	for (i = 0; i < bttv_num; i++) {
		if (bttvs[i].video_dev.minor == minor) {
			btv = &bttvs[i];
			break;
		}
	}
	if (NULL == btv)
		return -ENODEV;

	dprintk(KERN_DEBUG "bttv%d: open called (video)\n",btv->nr);

	/* allocate per filehandle data */
	fh = kmalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	file->private_data = fh;
	*fh = btv->init;
	videobuf_queue_init(&fh->q, &bttv_qops, btv->dev, &btv->s_lock,
			    V4L2_BUF_TYPE_CAPTURE,sizeof(struct bttv_buffer));
	
	i2c_vidiocschan(btv);
        return 0;
}

static int bttv_release(struct inode *inode, struct file *file)
{
	struct bttv_fh *fh = file->private_data;
	struct bttv *btv = fh->btv;

	/* turn off overlay, stop outstanding captures */
	if (check_btres(fh, RESOURCE_OVERLAY))
		bttv_switch_overlay(btv,fh,NULL);
	if (check_btres(fh, RESOURCE_STREAMING)) {
		videobuf_streamoff(file,&fh->q);
		free_btres(btv,fh,RESOURCE_STREAMING);
		bttv_field_count(btv);
	}
	if (fh->q.read_buf) {
		buffer_release(file,fh->q.read_buf);
		kfree(fh->q.read_buf);
	}

	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int
bttv_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct bttv_fh *fh = file->private_data;

	if (!mmap)
		return -EINVAL;
	dprintk("mmap 0x%lx+%ld\n",vma->vm_start,
		vma->vm_end - vma->vm_start);
	return videobuf_mmap_mapper(vma,&fh->q);
}

static struct file_operations bttv_fops =
{
	owner:	  THIS_MODULE,
	open:	  bttv_open,
	release:  bttv_release,
	ioctl:	  bttv_ioctl,
	llseek:	  no_llseek,
	read:	  bttv_read,
	mmap:	  bttv_mmap,
	poll:     bttv_poll,
};

static struct video_device bttv_template =
{
	name:     "UNSET",
	type:     VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_OVERLAY|
	          VID_TYPE_CLIPPING|VID_TYPE_SCALES,
	hardware: VID_HARDWARE_BT848,
	fops:     &bttv_fops,
	minor:    -1,
};

/* ----------------------------------------------------------------------- */
/* radio interface                                                         */

static int radio_open(struct inode *inode, struct file *file)
{
	unsigned int minor = minor(inode->i_rdev);
	struct bttv *btv = NULL;
	unsigned long v = 400*16;
	int i;

	dprintk("bttv: open minor=%d\n",minor);

	for (i = 0; i < bttv_num; i++) {
		if (bttvs[i].radio_dev.minor == minor) {
			btv = &bttvs[i];
			break;
		}
	}
	if (NULL == btv)
		return -ENODEV;

	dprintk("bttv%d: open called (radio)\n",btv->nr);
	down(&btv->lock);
	if (btv->radio_user) {
		up(&btv->lock);
		return -EBUSY;
	}
	btv->radio_user++;
	file->private_data = btv;

	i2c_vidiocschan(btv);
	bttv_call_i2c_clients(btv,VIDIOCSFREQ,&v);
        bttv_call_i2c_clients(btv,AUDC_SET_RADIO,&btv->tuner_type);
	audio_mux(btv,AUDIO_RADIO);

	up(&btv->lock);
        return 0;
}

static int radio_release(struct inode *inode, struct file *file)
{
	struct bttv    *btv = file->private_data;

	btv->radio_user--;
	return 0;
}

static int radio_do_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, void *arg)
{
	struct bttv    *btv = file->private_data;

	switch (cmd) {
	case VIDIOCGCAP:
	{
                struct video_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->name,btv->radio_dev.name);
                cap->type = VID_TYPE_TUNER;
		cap->channels = 1;
		cap->audios = 1;
                return 0;
	}

        case VIDIOCGTUNER:
        {
                struct video_tuner *v = arg;

                if(v->tuner)
                        return -EINVAL;
		memset(v,0,sizeof(*v));
                strcpy(v->name, "Radio");
                /* japan:          76.0 MHz -  89.9 MHz
                   western europe: 87.5 MHz - 108.0 MHz
                   russia:         65.0 MHz - 108.0 MHz */
                v->rangelow=(int)(65*16);
                v->rangehigh=(int)(108*16);
                bttv_call_i2c_clients(btv,cmd,v);
                return 0;
        }
        case VIDIOCSTUNER:
		/* nothing to do */
		return 0;
	
	case BTTV_VERSION:
        case VIDIOCGFREQ:
        case VIDIOCSFREQ:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
		return bttv_common_ioctls(btv,cmd,arg);

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int radio_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, radio_do_ioctl);
}

static struct file_operations radio_fops =
{
	owner:	  THIS_MODULE,
	open:	  radio_open,
	release:  radio_release,
	ioctl:	  radio_ioctl,
	llseek:	  no_llseek,
};

static struct video_device radio_template =
{
	name:     "bt848/878 radio",
	type:     VID_TYPE_TUNER,
	hardware: VID_HARDWARE_BT848,
	fops:     &radio_fops,
	minor:    -1,
};

/* ----------------------------------------------------------------------- */
/* irq handler                                                             */

static char *irq_name[] = { "FMTCHG", "VSYNC", "HSYNC", "OFLOW", "HLOCK",
			    "VPRES", "6", "7", "I2CDONE", "GPINT", "10",
			    "RISCI", "FBUS", "FTRGT", "FDSR", "PPERR",
			    "RIPERR", "PABORT", "OCERR", "SCERR" };

static void bttv_print_irqbits(u32 print, u32 mark)
{
	int i;
	
	printk("bits:");
	for (i = 0; i < (sizeof(irq_name)/sizeof(char*)); i++) {
		if (print & (1 << i))
			printk(" %s",irq_name[i]);
		if (mark & (1 << i))
			printk("*");
	}
}

static void bttv_print_riscaddr(struct bttv *btv)
{
	printk("  main: %08Lx\n",
	       (u64)btv->main.dma);
	printk("  vbi : o=%08Lx e=%08Lx\n",
	       btv->vcurr ? (u64)btv->vcurr->odd.dma : 0,
	       btv->vcurr ? (u64)btv->vcurr->even.dma : 0);
	printk("  cap : o=%08Lx e=%08Lx\n",
	       btv->odd ? (u64)btv->odd->odd.dma : 0,
	       btv->even ? (u64)btv->even->even.dma : 0);
	printk("  scr : o=%08Lx e=%08Lx\n",
	       btv->screen ? (u64)btv->screen->odd.dma  : 0,
	       btv->screen ? (u64)btv->screen->even.dma : 0);
}

static void bttv_irq_timeout(unsigned long data)
{
	struct bttv *btv = (struct bttv *)data;
	struct bttv_buffer *o_even,*o_odd,*o_vcurr;
	struct bttv_buffer *capture;

	if (bttv_verbose) {
		printk(KERN_INFO "bttv%d: timeout: risc=%08x, ",
		       btv->nr,btread(BT848_RISC_COUNT));
		bttv_print_irqbits(btread(BT848_INT_STAT),0);
		printk("\n");
	}

	spin_lock(&btv->s_lock);
	o_odd   = btv->odd;
	o_even  = btv->even;
	o_vcurr = btv->vcurr;
	btv->odd   = NULL;
	btv->even  = NULL;
	btv->vcurr = NULL;
	
	/* deactivate stuff */
	bttv_risc_hook(btv, RISC_SLOT_O_FIELD, NULL, 0);
	bttv_risc_hook(btv, RISC_SLOT_E_FIELD, NULL, 0);
	bttv_risc_hook(btv, RISC_SLOT_O_VBI, NULL, 0);
	bttv_risc_hook(btv, RISC_SLOT_E_VBI, NULL, 0);
	bttv_set_dma(btv, 0, 0);

	/* wake up + free */
	if (o_odd == o_even) {
		if (NULL != o_odd) {
			o_odd->vb.state = STATE_ERROR;
			wake_up(&o_odd->vb.done);
		}
	} else {
		if (NULL != o_odd) {
			o_odd->vb.state = STATE_ERROR;
			wake_up(&o_odd->vb.done);
		}
		if (NULL != o_even) {
			o_even->vb.state = STATE_ERROR;
			wake_up(&o_even->vb.done);
		}
	}
	if (NULL != o_vcurr) {
		o_vcurr->vb.state = STATE_ERROR;
		wake_up(&o_vcurr->vb.done);
	}

	/* cancel all outstanding capture / vbi requests */
	while (!list_empty(&btv->capture)) {
		capture = list_entry(btv->capture.next, struct bttv_buffer, vb.queue);
		list_del(&capture->vb.queue);
		capture->vb.state = STATE_ERROR;
		wake_up(&capture->vb.done);
	}
	while (!list_empty(&btv->vcapture)) {
		capture = list_entry(btv->vcapture.next, struct bttv_buffer, vb.queue);
		list_del(&capture->vb.queue);
		capture->vb.state = STATE_ERROR;
		wake_up(&capture->vb.done);
	}
	
	btv->errors++;
	spin_unlock(&btv->s_lock);	
}

static void
bttv_irq_switch_fields(struct bttv *btv)
{
	struct bttv_buffer *o_even,*o_odd,*o_vcurr;
	struct bttv_buffer *capture;
	int irqflags = 0;
#ifdef HAVE_V4L2
	stamp_t ts;
#endif

	spin_lock(&btv->s_lock);
	o_odd   = btv->odd;
	o_even  = btv->even;
	o_vcurr = btv->vcurr;
	btv->odd   = NULL;
	btv->even  = NULL;
	btv->vcurr = NULL;

	/* vbi request ? */
	if (!list_empty(&btv->vcapture)) {
		irqflags = 1;
		btv->vcurr = list_entry(btv->vcapture.next, struct bttv_buffer, vb.queue);
		list_del(&btv->vcurr->vb.queue);
	}

	/* capture request ? */
	if (!list_empty(&btv->capture)) {
		irqflags = 1;
		capture = list_entry(btv->capture.next, struct bttv_buffer, vb.queue);
		list_del(&capture->vb.queue);
		if (capture->vb.field & VBUF_FIELD_ODD)
			btv->odd = capture;
		if (capture->vb.field & VBUF_FIELD_EVEN)
			btv->even = capture;

		/* capture request for other field ? */
		if (!(capture->vb.field & VBUF_FIELD_INTER) &&
		    !list_empty(&btv->capture)) {
			capture = list_entry(btv->capture.next, struct bttv_buffer, vb.queue);
			if (!(capture->vb.field & VBUF_FIELD_INTER)) {
				if (NULL == btv->odd &&
				    capture->vb.field & VBUF_FIELD_ODD) {
					btv->odd = capture;
					list_del(&capture->vb.queue);
				}
				if (NULL == btv->even &&
				    capture->vb.field & VBUF_FIELD_EVEN) {
					btv->even = capture;
					list_del(&capture->vb.queue);
				}
			}
		}
	}

	/* screen overlay ? */
	if (NULL != btv->screen) {
		if (btv->screen->vb.field & VBUF_FIELD_INTER) {
			if (NULL == btv->odd && NULL == btv->even) {
				btv->odd  = btv->screen;
				btv->even = btv->screen;
			}
		} else {
			if ((btv->screen->vb.field & VBUF_FIELD_ODD) &&
			    NULL == btv->odd) {
				btv->odd = btv->screen;
			}
			if ((btv->screen->vb.field & VBUF_FIELD_EVEN) &&
			    NULL == btv->even) {
				btv->even = btv->screen;
			}
		}
	}

	if (irq_debug)
		printk(KERN_DEBUG
		       "bttv: irq odd=%p even=%p screen=%p vbi=%p\n",
		       btv->odd,btv->even,btv->screen,btv->vcurr);
	
	/* activate new fields */
	bttv_buffer_activate(btv,btv->odd,btv->even);
	if (btv->vcurr) {
		btv->vcurr->vb.state = STATE_ACTIVE;
		bttv_risc_hook(btv, RISC_SLOT_O_VBI, &btv->vcurr->odd, 0);
		bttv_risc_hook(btv, RISC_SLOT_E_VBI, &btv->vcurr->even, 0);
	} else {
		bttv_risc_hook(btv, RISC_SLOT_O_VBI, NULL, 0);
		bttv_risc_hook(btv, RISC_SLOT_E_VBI, NULL, 0);
	}
	bttv_set_dma(btv, 0, irqflags);
	
	/* wake up + free */
#ifdef HAVE_V4L2
	v4l2_masterclock_gettime(&ts);
#endif
	if (o_odd == o_even) {
		if (NULL != o_odd && btv->odd != o_odd) {
#ifdef HAVE_V4L2
			o_odd->vb.ts = ts;
#endif
			o_odd->vb.field_count = btv->field_count;
			o_odd->vb.state = STATE_DONE;
			wake_up(&o_odd->vb.done);
		}
	} else {
		if (NULL != o_odd && btv->odd != o_odd) {
#ifdef HAVE_V4L2
			o_odd->vb.ts = ts;
#endif
			o_odd->vb.field_count = btv->field_count;
			o_odd->vb.state = STATE_DONE;
			wake_up(&o_odd->vb.done);
		}
		if (NULL != o_even && btv->even != o_even) {
#ifdef HAVE_V4L2
			o_even->vb.ts = ts;
#endif
			o_even->vb.field_count = btv->field_count;
			o_even->vb.state = STATE_DONE;
			wake_up(&o_even->vb.done);
		}
	}
	if (NULL != o_vcurr) {
#ifdef HAVE_V4L2
		o_vcurr->vb.ts = ts;
#endif
		o_vcurr->vb.field_count = btv->field_count;
		o_vcurr->vb.state = STATE_DONE;
		wake_up(&o_vcurr->vb.done);
	}
	spin_unlock(&btv->s_lock);
}

static void bttv_irq(int irq, void *dev_id, struct pt_regs * regs)
{
	u32 stat,astat;
	u32 dstat;
	int count;
	struct bttv *btv;

	btv=(struct bttv *)dev_id;
	count=0;
	while (1) 
	{
		/* get/clear interrupt status bits */
		stat=btread(BT848_INT_STAT);
		astat=stat&btread(BT848_INT_MASK);
		if (!astat)
			return;
		btwrite(stat,BT848_INT_STAT);

		/* get device status bits */
		dstat=btread(BT848_DSTATUS);

		if (irq_debug) {
			printk(KERN_DEBUG "bttv%d: irq loop=%d fc=%d "
			       "riscs=%x, riscc=%08x, ",
			       btv->nr, count, btv->field_count,
			       stat>>28, btread(BT848_RISC_COUNT));
			bttv_print_irqbits(stat,astat);
			if (stat & BT848_INT_HLOCK)
				printk("   HLOC => %s", (dstat & BT848_DSTATUS_HLOC)
				       ? "yes" : "no");
			if (stat & BT848_INT_VPRES)
				printk("   PRES => %s", (dstat & BT848_DSTATUS_PRES)
				       ? "yes" : "no");
			if (stat & BT848_INT_FMTCHG)
				printk("   NUML => %s", (dstat & BT848_DSTATUS_PRES)
				       ? "625" : "525");
			printk("\n");
		}

		if (astat&BT848_INT_VSYNC) 
                        btv->field_count++;

		if (astat & BT848_INT_GPINT)
			wake_up(&btv->gpioq);
		
                if ((astat & BT848_INT_RISCI)  &&  (stat & (1<<28)))
			bttv_irq_switch_fields(btv);

		if ((astat & BT848_INT_HLOCK)  &&  btv->opt_automute)
			audio_mux(btv, -1);

		if (astat & (BT848_INT_SCERR|BT848_INT_OCERR)) {
			printk(KERN_INFO "bttv%d: %s%s @ %08x,",btv->nr,
			       (astat & BT848_INT_SCERR) ? "SCERR" : "",
			       (astat & BT848_INT_OCERR) ? "OCERR" : "",
			       btread(BT848_RISC_COUNT));
			bttv_print_irqbits(stat,astat);
			printk("\n");
			if (bttv_debug)
				bttv_print_riscaddr(btv);
		}
		if (fdsr && astat & BT848_INT_FDSR) {
			printk(KERN_INFO "bttv%d: FDSR @ %08x\n",
			       btv->nr,btread(BT848_RISC_COUNT));
			if (bttv_debug)
				bttv_print_riscaddr(btv);
		}

		count++;
		if (count > 20) {
			btwrite(0, BT848_INT_MASK);
			printk(KERN_ERR 
			       "bttv%d: IRQ lockup, cleared int mask\n", btv->nr);
		}
	}
}


/* ----------------------------------------------------------------------- */
/* initialitation                                                          */

/* register video4linux devices */
static int __devinit bttv_register_video(struct bttv *btv)
{
        if(video_register_device(&btv->video_dev,VFL_TYPE_GRABBER,video_nr)<0)
                return -1;
	printk(KERN_INFO "bttv%d: registered device video%d\n",
	       btv->nr,btv->video_dev.minor & 0x1f);

        if(video_register_device(&btv->vbi_dev,VFL_TYPE_VBI,vbi_nr)<0) {
                video_unregister_device(&btv->video_dev);
                return -1;
        }
	printk(KERN_INFO "bttv%d: registered device vbi%d\n",
	       btv->nr,btv->vbi_dev.minor & 0x1f);

        if (!btv->has_radio)
		return 0;
	if (video_register_device(&btv->radio_dev, VFL_TYPE_RADIO,radio_nr)<0) {
		video_unregister_device(&btv->vbi_dev);
		video_unregister_device(&btv->video_dev);
		return -1;
        }
	printk(KERN_INFO "bttv%d: registered device radio%d\n",
	       btv->nr,btv->radio_dev.minor & 0x1f);
        return 0;
}


/* on OpenFirmware machines (PowerMac at least), PCI memory cycle */
/* response on cards with no firmware is not enabled by OF */
static void pci_set_command(struct pci_dev *dev)
{
#if defined(__powerpc__)
        unsigned int cmd;
	
        pci_read_config_dword(dev, PCI_COMMAND, &cmd);
        cmd = (cmd | PCI_COMMAND_MEMORY ); 
        pci_write_config_dword(dev, PCI_COMMAND, cmd);
#endif
}

static int __devinit bttv_probe(struct pci_dev *dev,
				const struct pci_device_id *pci_id)
{
	int result;
	unsigned char lat;
	struct bttv *btv;

	if (bttv_num == BTTV_MAX)
		return -ENOMEM;
	printk(KERN_INFO "bttv: Bt8xx card found (%d).\n", bttv_num);
        btv=&bttvs[bttv_num];
	memset(btv,0,sizeof(*btv));
	btv->nr  = bttv_num;
	sprintf(btv->name,"bttv%d",btv->nr);

	/* initialize structs / fill in defaults */
        init_MUTEX(&btv->lock);
        init_MUTEX(&btv->reslock);
        btv->s_lock = SPIN_LOCK_UNLOCKED;
        init_waitqueue_head(&btv->gpioq);
        INIT_LIST_HEAD(&btv->capture);
        INIT_LIST_HEAD(&btv->vcapture);
	videobuf_queue_init(&btv->vbi.q, &vbi_qops, btv->dev, &btv->s_lock,
			    V4L2_BUF_TYPE_VBI,sizeof(struct bttv_buffer));

	btv->timeout.function = bttv_irq_timeout;
	btv->timeout.data     = (unsigned long)btv;
	
        btv->i2c_rc = -1;
        btv->tuner_type = -1;

	memcpy(&btv->video_dev, &bttv_template,     sizeof(bttv_template));
	memcpy(&btv->radio_dev, &radio_template,    sizeof(radio_template));
	memcpy(&btv->vbi_dev,   &bttv_vbi_template, sizeof(bttv_vbi_template));
        btv->video_dev.minor = -1;
	btv->video_dev.priv = btv;
        btv->radio_dev.minor = -1;
	btv->radio_dev.priv = btv;
        btv->vbi_dev.minor = -1;
	btv->vbi_dev.priv = btv;
	btv->has_radio=radio[btv->nr];
	
	/* pci stuff (init, get irq/mmip, ... */
	btv->dev = dev;
        btv->id  = dev->device;
	if (pci_enable_device(dev)) {
                printk(KERN_WARNING "bttv%d: Can't enable device.\n",
		       btv->nr);
		return -EIO;
	}
        if (pci_set_dma_mask(dev, 0xffffffff)) {
                printk(KERN_WARNING "bttv%d: No suitable DMA available.\n",
		       btv->nr);
		return -EIO;
        }
	if (!request_mem_region(pci_resource_start(dev,0),
				pci_resource_len(dev,0),
				btv->name)) {
		return -EBUSY;
	}
        pci_set_master(dev);
	pci_set_command(dev);
	pci_set_drvdata(dev,btv);
	if (!pci_dma_supported(dev,0xffffffff)) {
		printk("bttv: Oops: no 32bit PCI DMA ???\n");
		result = -EIO;
		goto fail1;
	}

	if (-1 != latency) {
		printk(KERN_INFO "bttv%d: setting pci latency timer to %d\n",
		       bttv_num,latency);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, latency);
	}
        pci_read_config_byte(dev, PCI_CLASS_REVISION, &btv->revision);
        pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
        printk(KERN_INFO "bttv%d: Bt%d (rev %d) at %02x:%02x.%x, ",
               bttv_num,btv->id, btv->revision, dev->bus->number,
	       PCI_SLOT(dev->devfn),PCI_FUNC(dev->devfn));
        printk("irq: %d, latency: %d, mmio: 0x%lx\n",
	       btv->dev->irq, lat, pci_resource_start(dev,0));
	
#ifdef __sparc__
	/* why no ioremap for sparc? */
	btv->bt848_mmio=(unsigned char *)pci_resource_start(dev,0);
#else
	btv->bt848_mmio=ioremap(pci_resource_start(dev,0), 0x1000);
#endif

        /* identify card */
	bttv_idcard(btv);

        /* disable irqs, register irq handler */
	btwrite(0, BT848_INT_MASK);
        result = request_irq(btv->dev->irq, bttv_irq,
                             SA_SHIRQ | SA_INTERRUPT,btv->name,(void *)btv);
        if (result < 0) {
                printk(KERN_ERR "bttv%d: can't get IRQ %d\n",
		       bttv_num,btv->dev->irq);
		goto fail1;
        }

	if (0 != bttv_handle_chipset(btv)) {
		result = -EIO;
		goto fail2;
        }

	/* init options from insmod args */
	btv->opt_combfilter = combfilter;
	btv->opt_lumafilter = lumafilter;
	btv->opt_automute   = automute;
	btv->opt_chroma_agc = chroma_agc;
	btv->opt_adc_crush  = adc_crush;
	
	/* fill struct bttv with some useful defaults */
	btv->init.btv = btv;
	btv->init.ov.width      = 320;
	btv->init.ov.height     = 240;
	btv->init.buf.fmt       = format_by_palette(VIDEO_PALETTE_RGB24);
	btv->init.buf.vb.width  = 320;
	btv->init.buf.vb.height = 240;
	btv->input = 0;

	/* initialize hardware */
        if (bttv_gpio)
                bttv_gpio_tracking(btv,"pre-init");

	bttv_risc_init_main(btv);
	init_bt848(btv);

	/* gpio */
        btwrite(0x00, BT848_GPIO_REG_INP);
        btwrite(0x00, BT848_GPIO_OUT_EN);
        if (bttv_gpio)
                bttv_gpio_tracking(btv,"init");

        /* interrupt */
        btwrite(0xfffffUL, BT848_INT_STAT);
        btwrite((btv->triton1) |
                BT848_INT_GPINT|
                BT848_INT_SCERR|
                (fdsr ? BT848_INT_FDSR : 0) |
                BT848_INT_RISCI|BT848_INT_OCERR|BT848_INT_VPRES|
                BT848_INT_FMTCHG|BT848_INT_HLOCK,
                BT848_INT_MASK);
	
        /* needs to be done before i2c is registered */
        bttv_init_card1(btv);

        /* register i2c */
        init_bttv_i2c(btv);

        /* some card-specific stuff (needs working i2c) */
        bttv_init_card2(btv);

        /* register video4linux */
        bttv_register_video(btv);

	bt848_bright(btv,32768);
	bt848_contrast(btv,32768);
	bt848_hue(btv,32768);
	bt848_sat(btv,32768);
	audio_mux(btv,AUDIO_MUTE);
	set_input(btv,0);

	/* everything is fine */
	bttv_num++;
        return 0;

 fail2:
        free_irq(btv->dev->irq,btv);
	
 fail1:
	release_mem_region(pci_resource_start(btv->dev,0),
			   pci_resource_len(btv->dev,0));
	pci_set_drvdata(dev,NULL);
	return result;
}

static void __devexit bttv_remove(struct pci_dev *pci_dev)
{
        struct bttv *btv = pci_get_drvdata(pci_dev);

	if (bttv_verbose)
		printk("bttv%d: unloading\n",btv->nr);

        /* shutdown everything (DMA+IRQs) */
	btand(~15, BT848_GPIO_DMA_CTL);
	btwrite(0, BT848_INT_MASK);
	btwrite(~0x0UL,BT848_INT_STAT);
	btwrite(0x0, BT848_GPIO_OUT_EN);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"cleanup");

	/* tell gpio modules we are leaving ... */
	btv->shutdown=1;
	wake_up(&btv->gpioq);

        /* unregister i2c_bus */
	if (0 == btv->i2c_rc)
		i2c_bit_del_bus(&btv->i2c_adap);

	/* unregister video4linux */
        if (btv->video_dev.minor!=-1)
                video_unregister_device(&btv->video_dev);
        if (btv->radio_dev.minor!=-1)
                video_unregister_device(&btv->radio_dev);
	if (btv->vbi_dev.minor!=-1)
                video_unregister_device(&btv->vbi_dev);

	/* free allocated memory */
	bttv_riscmem_free(btv->dev,&btv->main);

	/* free ressources */
        free_irq(btv->dev->irq,btv);
        release_mem_region(pci_resource_start(btv->dev,0),
                           pci_resource_len(btv->dev,0));

	pci_set_drvdata(pci_dev, NULL);
        return;
}

static struct pci_device_id bttv_pci_tbl[] __devinitdata = {
        {PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT848,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT849,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT878,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT879,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {0,}
};

MODULE_DEVICE_TABLE(pci, bttv_pci_tbl);

static struct pci_driver bttv_pci_driver = {
        name:     "bttv",
        id_table: bttv_pci_tbl,
        probe:    bttv_probe,
        remove:   __devexit_p(bttv_remove),
};

static int bttv_init_module(void)
{
	bttv_num = 0;

	printk(KERN_INFO "bttv: driver version %d.%d.%d loaded "
	       BTTV_APIS "\n",
	       (BTTV_VERSION_CODE >> 16) & 0xff,
	       (BTTV_VERSION_CODE >> 8) & 0xff,
	       BTTV_VERSION_CODE & 0xff);
	if (gbuffers < 2 || gbuffers > VIDEO_MAX_FRAME)
		gbuffers = 2;
	if (gbufsize < 0 || gbufsize > BTTV_MAX_FBUF)
		gbufsize = BTTV_MAX_FBUF;
	gbufsize = (gbufsize + PAGE_SIZE - 1) & PAGE_MASK;
	if (bttv_verbose)
		printk(KERN_INFO "bttv: using %d buffers with %dk (%d pages) each for capture\n",
		       gbuffers, gbufsize >> 10, gbufsize >> PAGE_SHIFT);

	bttv_check_chipset();

	return pci_module_init(&bttv_pci_driver);
}

static void bttv_cleanup_module(void)
{
	pci_unregister_driver(&bttv_pci_driver);
	return;
}

module_init(bttv_init_module);
module_exit(bttv_cleanup_module);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
