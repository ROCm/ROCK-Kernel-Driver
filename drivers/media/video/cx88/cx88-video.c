/*
 * device driver for Conexant 2388x based TV cards
 * video4linux video interface
 *
 * (c) 2003-04 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
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
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/div64.h>

#include "cx88.h"

#define V4L2_I2C_CLIENTS 1

MODULE_DESCRIPTION("v4l2 driver module for cx2388x based TV cards");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

static unsigned int video_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
MODULE_PARM(video_nr,"1-" __stringify(CX88_MAXBOARDS) "i");
MODULE_PARM_DESC(video_nr,"video device numbers");

static unsigned int vbi_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
MODULE_PARM(vbi_nr,"1-" __stringify(CX88_MAXBOARDS) "i");
MODULE_PARM_DESC(vbi_nr,"vbi device numbers");

static unsigned int radio_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
MODULE_PARM(radio_nr,"1-" __stringify(CX88_MAXBOARDS) "i");
MODULE_PARM_DESC(radio_nr,"radio device numbers");

static unsigned int latency = UNSET;
MODULE_PARM(latency,"i");
MODULE_PARM_DESC(latency,"pci latency timer");

static unsigned int video_debug = 0;
MODULE_PARM(video_debug,"i");
MODULE_PARM_DESC(video_debug,"enable debug messages [video]");

static unsigned int irq_debug = 0;
MODULE_PARM(irq_debug,"i");
MODULE_PARM_DESC(irq_debug,"enable debug messages [IRQ handler]");

static unsigned int vid_limit = 16;
MODULE_PARM(vid_limit,"i");
MODULE_PARM_DESC(vid_limit,"capture memory limit in megabytes");

static unsigned int tuner[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
MODULE_PARM(tuner,"1-" __stringify(CX88_MAXBOARDS) "i");
MODULE_PARM_DESC(tuner,"tuner type");

static unsigned int card[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
MODULE_PARM(card,"1-" __stringify(CX88_MAXBOARDS) "i");
MODULE_PARM_DESC(card,"card type");

static unsigned int nicam = 0;
MODULE_PARM(nicam,"i");
MODULE_PARM_DESC(nicam,"tv audio is nicam");

#define dprintk(level,fmt, arg...)	if (video_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */

static struct list_head  cx8800_devlist;
static unsigned int      cx8800_devcount;

/* ------------------------------------------------------------------- */
/* static data                                                         */

static unsigned int inline norm_swidth(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 922 : 754;
}

static unsigned int inline norm_hdelay(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 186 : 135;
}

static unsigned int inline norm_vdelay(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 0x24 : 0x18;
}

static unsigned int inline norm_maxw(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 768 : 640;
//	return (norm->id & V4L2_STD_625_50) ? 720 : 640;
}

static unsigned int inline norm_maxh(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 576 : 480;
}

static unsigned int inline norm_fsc8(struct cx8800_tvnorm *norm)
{
	static const unsigned int ntsc = 28636360;
	static const unsigned int pal  = 35468950;
	
	return (norm->id & V4L2_STD_625_50) ? pal : ntsc;
}

static unsigned int inline norm_notchfilter(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50)
		? HLNotchFilter135PAL
		: HLNotchFilter135NTSC;
}

static unsigned int inline norm_htotal(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 1135 : 910;
}

static unsigned int inline norm_vbipack(struct cx8800_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 511 : 288;
}

static struct cx8800_tvnorm tvnorms[] = {
	{
		.name      = "NTSC-M",
		.id        = V4L2_STD_NTSC_M,
		.cxiformat = VideoFormatNTSC,
		.cxoformat = 0x181f0008,
	},{
		.name      = "NTSC-JP",
		.id        = V4L2_STD_NTSC_M_JP,
		.cxiformat = VideoFormatNTSCJapan,
		.cxoformat = 0x181f0008,
#if 0
	},{
		.name      = "NTSC-4.43",
		.id        = FIXME,
		.cxiformat = VideoFormatNTSC443,
		.cxoformat = 0x181f0008,
#endif
	},{
		.name      = "PAL-BG",
		.id        = V4L2_STD_PAL_BG,
		.cxiformat = VideoFormatPAL,
		.cxoformat = 0x181f0008,
	},{
		.name      = "PAL-DK",
		.id        = V4L2_STD_PAL_DK,
		.cxiformat = VideoFormatPAL,
		.cxoformat = 0x181f0008,
	},{
		.name      = "PAL-I",
		.id        = V4L2_STD_PAL_I,
		.cxiformat = VideoFormatPAL,
		.cxoformat = 0x181f0008,
        },{
		.name      = "PAL-M",
		.id        = V4L2_STD_PAL_M,
		.cxiformat = VideoFormatPALM,
		.cxoformat = 0x1c1f0008,
	},{
		.name      = "PAL-N",
		.id        = V4L2_STD_PAL_N,
		.cxiformat = VideoFormatPALN,
		.cxoformat = 0x1c1f0008,
	},{
		.name      = "PAL-Nc",
		.id        = V4L2_STD_PAL_Nc,
		.cxiformat = VideoFormatPALNC,
		.cxoformat = 0x1c1f0008,
	},{
		.name      = "PAL-60",
		.id        = V4L2_STD_PAL_60,
		.cxiformat = VideoFormatPAL60,
		.cxoformat = 0x181f0008,
	},{
		.name      = "SECAM-L",
		.id        = V4L2_STD_SECAM_L,
		.cxiformat = VideoFormatSECAM,
		.cxoformat = 0x181f0008,
	},{
		.name      = "SECAM-DK",
		.id        = V4L2_STD_SECAM_DK,
		.cxiformat = VideoFormatSECAM,
		.cxoformat = 0x181f0008,
	}
};

static struct cx8800_fmt formats[] = {
	{
		.name     = "8 bpp, gray",
		.fourcc   = V4L2_PIX_FMT_GREY,
		.cxformat = ColorFormatY8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.cxformat = ColorFormatRGB15,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB555X,
		.cxformat = ColorFormatRGB15 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.cxformat = ColorFormatRGB16,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB565X,
		.cxformat = ColorFormatRGB16 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "24 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.cxformat = ColorFormatRGB24,
		.depth    = 24,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.cxformat = ColorFormatRGB32,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.cxformat = ColorFormatRGB32 | ColorFormatBSWAP | ColorFormatWSWAP,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.cxformat = ColorFormatYUY2,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.cxformat = ColorFormatYUY2 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},
};

static struct cx8800_fmt* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;
	
	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ------------------------------------------------------------------- */

static const struct v4l2_queryctrl no_ctl = {
	.name  = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};

static struct cx88_ctrl cx8800_ctls[] = {
	/* --- video --- */
	{
		.v = {
			.id            = V4L2_CID_BRIGHTNESS,
			.name          = "Brightness",
			.minimum       = 0x00,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 128,
		.reg                   = MO_CONTR_BRIGHT,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
		.v = {
			.id            = V4L2_CID_CONTRAST,
			.name          = "Contrast",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = MO_CONTR_BRIGHT,
		.mask                  = 0xff00,
		.shift                 = 8,
	},{
		.v = {
			.id            = V4L2_CID_HUE,
			.name          = "Hue",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 0,
		.reg                   = MO_HUE,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
		/* strictly, this only describes only U saturation.
		 * V saturation is handled specially through code.
		 */
		.v = {
			.id            = V4L2_CID_SATURATION,
			.name          = "Saturation",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 0,
		.reg                   = MO_UV_SATURATION,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
	/* --- audio --- */
		.v = {
			.id            = V4L2_CID_AUDIO_MUTE,
			.name          = "Mute",
			.minimum       = 0,
			.maximum       = 1,
			.type          = V4L2_CTRL_TYPE_BOOLEAN,
		},
		.reg                   = AUD_VOL_CTL,
		.sreg                  = SHADOW_AUD_VOL_CTL,
		.mask                  = (1 << 6),
		.shift                 = 6,
	},{
		.v = {
			.id            = V4L2_CID_AUDIO_VOLUME,
			.name          = "Volume",
			.minimum       = 0,
			.maximum       = 0x3f,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = AUD_VOL_CTL,
		.sreg                  = SHADOW_AUD_VOL_CTL,
		.mask                  = 0x3f,
		.shift                 = 0,
	},{
		.v = {
			.id            = V4L2_CID_AUDIO_BALANCE,
			.name          = "Balance",
			.minimum       = 0,
			.maximum       = 0x7f,
			.step          = 1,
			.default_value = 0x40,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = AUD_BAL_CTL,
		.sreg                  = SHADOW_AUD_BAL_CTL,
		.mask                  = 0x7f,
		.shift                 = 0,
	}
};
const int CX8800_CTLS = ARRAY_SIZE(cx8800_ctls);

/* ------------------------------------------------------------------- */
/* resource management                                                 */

static int res_get(struct cx8800_dev *dev, struct cx8800_fh *fh, unsigned int bit)
{
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	down(&dev->lock);
	if (dev->resources & bit) {
		/* no, someone else uses it */
		up(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	dev->resources |= bit;
	dprintk(1,"res: get %d\n",bit);
	up(&dev->lock);
	return 1;
}

static
int res_check(struct cx8800_fh *fh, unsigned int bit)
{
	return (fh->resources & bit);
}

static
int res_locked(struct cx8800_dev *dev, unsigned int bit)
{
	return (dev->resources & bit);
}

static
void res_free(struct cx8800_dev *dev, struct cx8800_fh *fh, unsigned int bits)
{
	if ((fh->resources & bits) != bits)
		BUG();

	down(&dev->lock);
	fh->resources  &= ~bits;
	dev->resources &= ~bits;
	dprintk(1,"res: put %d\n",bits);
	up(&dev->lock);
}

/* ------------------------------------------------------------------ */

static const u32 xtal = 28636363;

static int set_pll(struct cx8800_dev *dev, int prescale, u32 ofreq)
{
	static u32 pre[] = { 0, 0, 0, 3, 2, 1 };
	u64 pll;
	u32 reg;
	int i;

	if (prescale < 2)
		prescale = 2;
	if (prescale > 5)
		prescale = 5;

	pll = ofreq * 8 * prescale * (u64)(1 << 20);
	do_div(pll,xtal);
	reg = (pll & 0x3ffffff) | (pre[prescale] << 26);
	if (((reg >> 20) & 0x3f) < 14) {
		printk("%s: pll out of range\n",dev->name);
		return -1;
	}
		
	dprintk(1,"set_pll:    MO_PLL_REG       0x%08x [old=0x%08x,freq=%d]\n",
		reg, cx_read(MO_PLL_REG), ofreq);
	cx_write(MO_PLL_REG, reg);
	for (i = 0; i < 10; i++) {
		reg = cx_read(MO_DEVICE_STATUS);
		if (reg & (1<<2)) {
			dprintk(1,"pll locked [pre=%d,ofreq=%d]\n",
				prescale,ofreq);
			return 0;
		}
		dprintk(1,"pll not locked yet, waiting ...\n");
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/10);
	}
	dprintk(1,"pll NOT locked [pre=%d,ofreq=%d]\n",prescale,ofreq);
	return -1;
}

static int set_tvaudio(struct cx8800_dev *dev)
{
	if (CX88_VMUX_TELEVISION != INPUT(dev->input)->type)
		return 0;

	if (V4L2_STD_PAL_BG & dev->tvnorm->id) {
		dev->tvaudio = nicam ? WW_NICAM_BGDKL : WW_A2_BG;

	} else if (V4L2_STD_PAL_DK & dev->tvnorm->id) {
		dev->tvaudio = nicam ? WW_NICAM_BGDKL : WW_A2_DK;

	} else if (V4L2_STD_PAL_I & dev->tvnorm->id) {
		dev->tvaudio = WW_NICAM_I;

	} else if (V4L2_STD_SECAM_L & dev->tvnorm->id) {
		dev->tvaudio = WW_SYSTEM_L_AM;

	} else if (V4L2_STD_SECAM_DK & dev->tvnorm->id) {
		dev->tvaudio = WW_A2_DK;

	} else if ((V4L2_STD_NTSC_M & dev->tvnorm->id) ||
		   (V4L2_STD_PAL_M  & dev->tvnorm->id)) {
		dev->tvaudio = WW_BTSC;

	} else if (V4L2_STD_NTSC_M_JP & dev->tvnorm->id) {
		dev->tvaudio = WW_EIAJ;

	} else {
		printk("%s: tvaudio support needs work for this tv norm [%s], sorry\n",
		       dev->name, dev->tvnorm->name);
		dev->tvaudio = 0;
		return 0;
	}

	cx_andor(MO_AFECFG_IO, 0x1f, 0x0);
	cx88_set_tvaudio(dev);
	// cx88_set_stereo(dev,V4L2_TUNER_MODE_STEREO);

	cx_write(MO_AUDD_LNGTH, 128/8);  /* fifo size */
	cx_write(MO_AUDR_LNGTH, 128/8);  /* fifo size */
	cx_write(MO_AUD_DMACNTRL, 0x03); /* need audio fifo */
	return 0;
}

static int set_tvnorm(struct cx8800_dev *dev, struct cx8800_tvnorm *norm)
{
	u32 fsc8;
	u32 adc_clock;
	u32 vdec_clock;
	u64 tmp64;
	u32 bdelay,agcdelay,htotal;
	
	dev->tvnorm = norm;
	fsc8       = norm_fsc8(norm);
	adc_clock  = xtal;
	vdec_clock = fsc8;

	dprintk(1,"set_tvnorm: \"%s\" fsc8=%d adc=%d vdec=%d\n",
		norm->name, fsc8, adc_clock, vdec_clock);
	set_pll(dev,2,vdec_clock);
	
	dprintk(1,"set_tvnorm: MO_INPUT_FORMAT  0x%08x [old=0x%08x]\n",
		norm->cxiformat, cx_read(MO_INPUT_FORMAT) & 0x0f);
	cx_andor(MO_INPUT_FORMAT, 0xf, norm->cxiformat);

#if 1
	// FIXME: as-is from DScaler
	dprintk(1,"set_tvnorm: MO_OUTPUT_FORMAT 0x%08x [old=0x%08x]\n",
		norm->cxoformat, cx_read(MO_OUTPUT_FORMAT));
	cx_write(MO_OUTPUT_FORMAT, norm->cxoformat);
#endif

	// MO_SCONV_REG = adc clock / video dec clock * 2^17
	tmp64  = adc_clock * (u64)(1 << 17);
	do_div(tmp64, vdec_clock);
	dprintk(1,"set_tvnorm: MO_SCONV_REG     0x%08x [old=0x%08x]\n",
		(u32)tmp64, cx_read(MO_SCONV_REG));
	cx_write(MO_SCONV_REG, (u32)tmp64);

	// MO_SUB_STEP = 8 * fsc / video dec clock * 2^22
	tmp64  = fsc8 * (u64)(1 << 22);
	do_div(tmp64, vdec_clock);
	dprintk(1,"set_tvnorm: MO_SUB_STEP      0x%08x [old=0x%08x]\n",
		(u32)tmp64, cx_read(MO_SUB_STEP));
	cx_write(MO_SUB_STEP, (u32)tmp64);

	// MO_SUB_STEP_DR = 8 * 4406250 / video dec clock * 2^22
	tmp64  = 4406250 * 8 * (u64)(1 << 22);
	do_div(tmp64, vdec_clock);
	dprintk(1,"set_tvnorm: MO_SUB_STEP_DR   0x%08x [old=0x%08x]\n",
		(u32)tmp64, cx_read(MO_SUB_STEP_DR));
	cx_write(MO_SUB_STEP_DR, (u32)tmp64);

	// bdelay + agcdelay
	bdelay   = vdec_clock * 65 / 20000000 + 21;
	agcdelay = vdec_clock * 68 / 20000000 + 15;
	dprintk(1,"set_tvnorm: MO_AGC_BURST     0x%08x [old=0x%08x,bdelay=%d,agcdelay=%d]\n",
		(bdelay << 8) | agcdelay, cx_read(MO_AGC_BURST), bdelay, agcdelay);
	cx_write(MO_AGC_BURST, (bdelay << 8) | agcdelay);

	// htotal
	tmp64 = norm_htotal(norm) * (u64)vdec_clock;
	do_div(tmp64, fsc8);
	htotal = (u32)tmp64 | (norm_notchfilter(norm) << 11);
	dprintk(1,"set_tvnorm: MO_HTOTAL        0x%08x [old=0x%08x,htotal=%d]\n",
		htotal, cx_read(MO_HTOTAL), (u32)tmp64);
	cx_write(MO_HTOTAL, htotal);

	// vbi stuff
	cx_write(MO_VBI_PACKET, ((1 << 11) | /* (norm_vdelay(norm)   << 11) | */
				 norm_vbipack(norm)));
	
	// audio
	set_tvaudio(dev);

	// tell i2c chips
#ifdef V4L2_I2C_CLIENTS
	cx8800_call_i2c_clients(dev,VIDIOC_S_STD,&norm->id);
#else
	{
		struct video_channel c;
		memset(&c,0,sizeof(c));
		c.channel = dev->input;
		c.norm = VIDEO_MODE_PAL;
		if ((norm->id & (V4L2_STD_NTSC_M|V4L2_STD_NTSC_M_JP)))
			c.norm = VIDEO_MODE_NTSC;
		if (norm->id & V4L2_STD_SECAM)
			c.norm = VIDEO_MODE_SECAM;
		cx8800_call_i2c_clients(dev,VIDIOCSCHAN,&c);
	}
#endif

	// done
	return 0;
}

static int set_scale(struct cx8800_dev *dev, unsigned int width, unsigned int height,
		     enum v4l2_field field)
{
	unsigned int swidth  = norm_swidth(dev->tvnorm);
	unsigned int sheight = norm_maxh(dev->tvnorm);
	u32 value;

	dprintk(1,"set_scale: %dx%d [%s%s,%s]\n", width, height,
		V4L2_FIELD_HAS_TOP(field)    ? "T" : "",
		V4L2_FIELD_HAS_BOTTOM(field) ? "B" : "",
		dev->tvnorm->name);
	if (!V4L2_FIELD_HAS_BOTH(field))
		height *= 2;

	// recalc H delay and scale registers
	value = (width * norm_hdelay(dev->tvnorm)) / swidth;
	value &= 0x3fe;
	cx_write(MO_HDELAY_EVEN,  value);
	cx_write(MO_HDELAY_ODD,   value);
	dprintk(1,"set_scale: hdelay  0x%04x\n", value);
	
	value = (swidth * 4096 / width) - 4096;
	cx_write(MO_HSCALE_EVEN,  value);
	cx_write(MO_HSCALE_ODD,   value);
	dprintk(1,"set_scale: hscale  0x%04x\n", value);

	cx_write(MO_HACTIVE_EVEN, width);
	cx_write(MO_HACTIVE_ODD,  width);
	dprintk(1,"set_scale: hactive 0x%04x\n", width);
	
	// recalc V scale Register (delay is constant)
	cx_write(MO_VDELAY_EVEN, norm_vdelay(dev->tvnorm));
	cx_write(MO_VDELAY_ODD,  norm_vdelay(dev->tvnorm));
	dprintk(1,"set_scale: vdelay  0x%04x\n", norm_vdelay(dev->tvnorm));
	
	value = (0x10000 - (sheight * 512 / height - 512)) & 0x1fff;
	cx_write(MO_VSCALE_EVEN,  value);
	cx_write(MO_VSCALE_ODD,   value);
	dprintk(1,"set_scale: vscale  0x%04x\n", value);

	cx_write(MO_VACTIVE_EVEN, sheight);
	cx_write(MO_VACTIVE_ODD,  sheight);
	dprintk(1,"set_scale: vactive 0x%04x\n", sheight);

	// setup filters
	value = 0;
	value |= (1 << 19);        // CFILT (default)
	if (V4L2_FIELD_INTERLACED == field)
		value |= (1 << 3); // VINT (interlaced vertical scaling)
	if (width < 385)
		value |= (1 << 0); // 3-tap interpolation
	if (width < 193)
		value |= (1 << 1); // 5-tap interpolation

	cx_write(MO_FILTER_EVEN,  value);
	cx_write(MO_FILTER_ODD,   value);
	dprintk(1,"set_scale: filter  0x%04x\n", value);
	
	return 0;
}

static int video_mux(struct cx8800_dev *dev, unsigned int input)
{
	dprintk(1,"video_mux: %d [vmux=%d,gpio=0x%x,0x%x,0x%x,0x%x]\n",
		input, INPUT(input)->vmux,
		INPUT(input)->gpio0,INPUT(input)->gpio1,
		INPUT(input)->gpio2,INPUT(input)->gpio3);
	dev->input = input;
	cx_andor(MO_INPUT_FORMAT, 0x03 << 14, INPUT(input)->vmux << 14);
	cx_write(MO_GP0_IO, INPUT(input)->gpio0);
	cx_write(MO_GP1_IO, INPUT(input)->gpio1);
	cx_write(MO_GP2_IO, INPUT(input)->gpio2);
	cx_write(MO_GP3_IO, INPUT(input)->gpio3);

	switch (INPUT(input)->type) {
	case CX88_VMUX_SVIDEO:
		cx_set(MO_AFECFG_IO,    0x00000001);
		cx_set(MO_INPUT_FORMAT, 0x00010010);
		break;
	default:
		cx_clear(MO_AFECFG_IO,    0x00000001);
		cx_clear(MO_INPUT_FORMAT, 0x00010010);
		break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */

static int start_video_dma(struct cx8800_dev    *dev,
			   struct cx88_dmaqueue *q,
			   struct cx88_buffer   *buf)
{
	/* setup fifo + format */
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH21],
				buf->bpl, buf->risc.dma);
	set_scale(dev, buf->vb.width, buf->vb.height, buf->vb.field);
	cx_write(MO_COLOR_CTRL, buf->fmt->cxformat | ColorFormatGamma);

	/* reset counter */
	cx_write(MO_VIDY_GPCNTRL,0x3);
	q->count = 1;

	/* enable irqs */
	cx_set(MO_PCI_INTMSK, 0x00fc01);
	cx_set(MO_VID_INTMSK, 0x0f0011);
	
	/* enable capture */
	cx_set(VID_CAPTURE_CONTROL,0x06);
	
	/* start dma */
	cx_set(MO_DEV_CNTRL2, (1<<5));
	cx_set(MO_VID_DMACNTRL, 0x11);

	return 0;
}

static int restart_video_queue(struct cx8800_dev    *dev,
			       struct cx88_dmaqueue *q)
{
	struct cx88_buffer *buf, *prev;
	struct list_head *item;
	
	if (!list_empty(&q->active)) {
	        buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		dprintk(2,"restart_queue [%p/%d]: restart dma\n",
			buf, buf->vb.i);
		start_video_dma(dev, q, buf);
		list_for_each(item,&q->active) {
			buf = list_entry(item, struct cx88_buffer, vb.queue);
			buf->count    = q->count++;
		}
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		return 0;
	}

	prev = NULL;
	for (;;) {
		if (list_empty(&q->queued))
			return 0;
	        buf = list_entry(q->queued.next, struct cx88_buffer, vb.queue);
		if (NULL == prev) {
			list_del(&buf->vb.queue);
			list_add_tail(&buf->vb.queue,&q->active);
			start_video_dma(dev, q, buf);
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
			dprintk(2,"[%p/%d] restart_queue - first active\n",
				buf,buf->vb.i);

		} else if (prev->vb.width  == buf->vb.width  &&
			   prev->vb.height == buf->vb.height &&
			   prev->fmt       == buf->fmt) {
			list_del(&buf->vb.queue);
			list_add_tail(&buf->vb.queue,&q->active);
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] restart_queue - move to active\n",
				buf,buf->vb.i);
		} else {
			return 0;
		}
		prev = buf;
	}
}

/* ------------------------------------------------------------------ */

static int
buffer_setup(struct file *file, unsigned int *count, unsigned int *size)
{
	struct cx8800_fh *fh = file->private_data;
	
	*size = fh->fmt->depth*fh->width*fh->height >> 3;
	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;
	return 0;
}

static int
buffer_prepare(struct file *file, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct cx8800_fh   *fh  = file->private_data;
	struct cx8800_dev  *dev = fh->dev;
	struct cx88_buffer *buf = (struct cx88_buffer*)vb;
	int rc, init_buffer = 0;

	BUG_ON(NULL == fh->fmt);
	if (fh->width  < 48 || fh->width  > norm_maxw(dev->tvnorm) ||
	    fh->height < 32 || fh->height > norm_maxh(dev->tvnorm))
		return -EINVAL;
	buf->vb.size = (fh->width * fh->height * fh->fmt->depth) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != fh->fmt    ||
	    buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	    buf->vb.field  != field) {
		buf->fmt       = fh->fmt;
		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field  = field;
		init_buffer = 1;
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		init_buffer = 1;
		if (0 != (rc = videobuf_iolock(dev->pci,&buf->vb,NULL)))
			goto fail;
	}

	if (init_buffer) {
		buf->bpl = buf->vb.width * buf->fmt->depth >> 3;
		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, 0, UNSET,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, UNSET, 0,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, 0, buf->bpl,
					 buf->bpl, buf->bpl,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist,
					 0, buf->bpl * (buf->vb.height >> 1),
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_BT:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist,
					 buf->bpl * (buf->vb.height >> 1), 0,
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}
	dprintk(2,"[%p/%d] buffer_prepare - %dx%d %dbpp \"%s\" - dma=0x%08lx\n",
		buf, buf->vb.i,
		fh->width, fh->height, fh->fmt->depth, fh->fmt->name,
		(unsigned long)buf->risc.dma);

	buf->vb.state = STATE_PREPARED;
	return 0;

 fail:
	cx88_free_buffer(dev->pci,buf);
	return rc;
}

static void
buffer_queue(struct file *file, struct videobuf_buffer *vb)
{
	struct cx88_buffer    *buf  = (struct cx88_buffer*)vb;
	struct cx88_buffer    *prev;
	struct cx8800_fh      *fh   = file->private_data;
	struct cx8800_dev     *dev  = fh->dev;
	struct cx88_dmaqueue  *q    = &dev->vidq;

	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | 0x10000);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);

	if (!list_empty(&q->queued)) {
		list_add_tail(&buf->vb.queue,&q->queued);
		buf->vb.state = STATE_QUEUED;
		dprintk(2,"[%p/%d] buffer_queue - append to queued\n",
			buf, buf->vb.i);

	} else if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue,&q->active);
		start_video_dma(dev, q, buf);
		buf->vb.state = STATE_ACTIVE;
		buf->count    = q->count++;
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(2,"[%p/%d] buffer_queue - first active\n",
			buf, buf->vb.i);

	} else {
		prev = list_entry(q->active.prev, struct cx88_buffer, vb.queue);
		if (prev->vb.width  == buf->vb.width  &&
		    prev->vb.height == buf->vb.height &&
		    prev->fmt       == buf->fmt) {
			list_add_tail(&buf->vb.queue,&q->active);
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] buffer_queue - append to active\n",
				buf, buf->vb.i);

		} else {
			list_add_tail(&buf->vb.queue,&q->queued);
			buf->vb.state = STATE_QUEUED;
			dprintk(2,"[%p/%d] buffer_queue - first queued\n",
				buf, buf->vb.i);
		}
	}
}

static void buffer_release(struct file *file, struct videobuf_buffer *vb)
{
	struct cx88_buffer *buf = (struct cx88_buffer*)vb;
	struct cx8800_fh   *fh  = file->private_data;

	cx88_free_buffer(fh->dev->pci,buf);
}

struct videobuf_queue_ops cx8800_video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */

#if 0 /* overlay support not finished yet */
static u32* ov_risc_field(struct cx8800_dev *dev, struct cx8800_fh *fh,
			  u32 *rp, struct btcx_skiplist *skips,
			  u32 sync_line, int skip_even, int skip_odd)
{
	int line,maxy,start,end,skip,nskips;
	u32 ri,ra;
	u32 addr;

	/* sync instruction */
	*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	addr  = (unsigned long)dev->fbuf.base;
	addr += dev->fbuf.fmt.bytesperline * fh->win.w.top;
	addr += (fh->fmt->depth >> 3)      * fh->win.w.left;

	/* scan lines */
	for (maxy = -1, line = 0; line < fh->win.w.height;
	     line++, addr += dev->fbuf.fmt.bytesperline) {
		if ((line%2) == 0  &&  skip_even)
			continue;
		if ((line%2) == 1  &&  skip_odd)
			continue;

		/* calculate clipping */
		if (line > maxy)
			btcx_calc_skips(line, fh->win.w.width, &maxy,
					skips, &nskips, fh->clips, fh->nclips);

		/* write out risc code */
		for (start = 0, skip = 0; start < fh->win.w.width; start = end) {
			if (skip >= nskips) {
				ri  = RISC_WRITE;
				end = fh->win.w.width;
			} else if (start < skips[skip].start) {
				ri  = RISC_WRITE;
				end = skips[skip].start;
			} else {
				ri  = RISC_SKIP;
				end = skips[skip].end;
				skip++;
			}
			if (RISC_WRITE == ri)
				ra = addr + (fh->fmt->depth>>3)*start;
			else
				ra = 0;
				
			if (0 == start)
				ri |= RISC_SOL;
			if (fh->win.w.width == end)
				ri |= RISC_EOL;
			ri |= (fh->fmt->depth>>3) * (end-start);

			*(rp++)=cpu_to_le32(ri);
			if (0 != ra)
				*(rp++)=cpu_to_le32(ra);
		}
	}
	kfree(skips);
	return rp;
}

static int ov_risc_frame(struct cx8800_dev *dev, struct cx8800_fh *fh,
			 struct cx88_buffer *buf)
{
	struct btcx_skiplist *skips;
	u32 instructions,fields;
	u32 *rp;
	int rc;
	
	/* skip list for window clipping */
	if (NULL == (skips = kmalloc(sizeof(*skips) * fh->nclips,GFP_KERNEL)))
		return -ENOMEM;
	
	fields = 0;
	if (V4L2_FIELD_HAS_TOP(fh->win.field))
		fields++;
	if (V4L2_FIELD_HAS_BOTTOM(fh->win.field))
		fields++;

        /* estimate risc mem: worst case is (clip+1) * lines instructions
           + syncs + jump (all 2 dwords) */
	instructions  = (fh->nclips+1) * fh->win.w.height;
	instructions += 3 + 4;
	if ((rc = btcx_riscmem_alloc(dev->pci,&buf->risc,instructions*8)) < 0) {
		kfree(skips);
		return rc;
	}

	/* write risc instructions */
	rp = buf->risc.cpu;
	switch (fh->win.field) {
	case V4L2_FIELD_TOP:
		rp = ov_risc_field(dev, fh, rp, skips, 0,     0, 0);
		break;
	case V4L2_FIELD_BOTTOM:
		rp = ov_risc_field(dev, fh, rp, skips, 0x200, 0, 0);
		break;
	case V4L2_FIELD_INTERLACED:
		rp = ov_risc_field(dev, fh, rp, skips, 0,     0, 1);
		rp = ov_risc_field(dev, fh, rp, skips, 0x200, 1, 0);
		break;
	default:
		BUG();
	}

	/* save pointer to jmp instruction address */
	buf->risc.jmp = rp;
	kfree(skips);
	return 0;
}

static int verify_window(struct cx8800_dev *dev, struct v4l2_window *win)
{
	enum v4l2_field field;
	int maxw, maxh;

	if (NULL == dev->fbuf.base)
		return -EINVAL;
	if (win->w.width < 48 || win->w.height <  32)
		return -EINVAL;
	if (win->clipcount > 2048)
		return -EINVAL;

	field = win->field;
	maxw  = norm_maxw(dev->tvnorm);
	maxh  = norm_maxh(dev->tvnorm);

	if (V4L2_FIELD_ANY == field) {
                field = (win->w.height > maxh/2)
                        ? V4L2_FIELD_INTERLACED
                        : V4L2_FIELD_TOP;
        }
        switch (field) {
        case V4L2_FIELD_TOP:
        case V4L2_FIELD_BOTTOM:
                maxh = maxh / 2;
                break;
        case V4L2_FIELD_INTERLACED:
                break;
        default:
                return -EINVAL;
        }

	win->field = field;
	if (win->w.width > maxw)
		win->w.width = maxw;
	if (win->w.height > maxh)
		win->w.height = maxh;
	return 0;
}

static int setup_window(struct cx8800_dev *dev, struct cx8800_fh *fh,
			struct v4l2_window *win)
{
	struct v4l2_clip *clips = NULL;
	int n,size,retval = 0;

	if (NULL == fh->fmt)
		return -EINVAL;
	retval = verify_window(dev,win);
	if (0 != retval)
		return retval;

	/* copy clips  --  luckily v4l1 + v4l2 are binary
	   compatible here ...*/
	n = win->clipcount;
	size = sizeof(*clips)*(n+4);
	clips = kmalloc(size,GFP_KERNEL);
	if (NULL == clips)
		return -ENOMEM;
	if (n > 0) {
		if (copy_from_user(clips,win->clips,sizeof(struct v4l2_clip)*n)) {
			kfree(clips);
			return -EFAULT;
		}
	}

	/* clip against screen */
	if (NULL != dev->fbuf.base)
		n = btcx_screen_clips(dev->fbuf.fmt.width, dev->fbuf.fmt.height,
				      &win->w, clips, n);
	btcx_sort_clips(clips,n);

	/* 4-byte alignments */
	switch (fh->fmt->depth) {
	case 8:
	case 24:
		btcx_align(&win->w, clips, n, 3);
		break;
	case 16:
		btcx_align(&win->w, clips, n, 1);
		break;
	case 32:
		/* no alignment fixups needed */
		break;
	default:
		BUG();
	}
	
	down(&fh->vidq.lock);
	if (fh->clips)
		kfree(fh->clips);
	fh->clips    = clips;
	fh->nclips   = n;
	fh->win      = *win;
#if 0
	fh->ov.setup_ok = 1;
#endif
	
	/* update overlay if needed */
	retval = 0;
#if 0
	if (check_btres(fh, RESOURCE_OVERLAY)) {
		struct bttv_buffer *new;
		
		new = videobuf_alloc(sizeof(*new));
		bttv_overlay_risc(btv, &fh->ov, fh->ovfmt, new);
		retval = bttv_switch_overlay(btv,fh,new);
	}
#endif
	up(&fh->vidq.lock);
	return retval;
}
#endif

/* ------------------------------------------------------------------ */

static struct videobuf_queue* get_queue(struct cx8800_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &fh->vidq;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return &fh->vbiq;
	default:
		BUG();
		return NULL;
	}
}

static int get_ressource(struct cx8800_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return RESOURCE_VIDEO;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return RESOURCE_VBI;
	default:
		BUG();
		return 0;
	}
}

static int video_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cx8800_dev *h,*dev = NULL;
	struct cx8800_fh *fh;
	struct list_head *list;
	enum v4l2_buf_type type = 0;
	int radio = 0;
	
	list_for_each(list,&cx8800_devlist) {
		h = list_entry(list, struct cx8800_dev, devlist);
		if (h->video_dev->minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
		if (h->vbi_dev->minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VBI_CAPTURE;
		}
		if (h->radio_dev &&
		    h->radio_dev->minor == minor) {
			radio = 1;
			dev   = h;
		}
	}
	if (NULL == dev)
		return -ENODEV;

	dprintk(1,"open minor=%d radio=%d type=%s\n",
		minor,radio,v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kmalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	memset(fh,0,sizeof(*fh));
	file->private_data = fh;
	fh->dev      = dev;
	fh->radio    = radio;
	fh->type     = type;
	fh->width    = 320;
	fh->height   = 240;
	fh->fmt      = format_by_fourcc(V4L2_PIX_FMT_BGR24);

	videobuf_queue_init(&fh->vidq, &cx8800_video_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct cx88_buffer));
	videobuf_queue_init(&fh->vbiq, &cx8800_vbi_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct cx88_buffer));
	init_MUTEX(&fh->vidq.lock);
	init_MUTEX(&fh->vbiq.lock);

	if (fh->radio) {
		dprintk(1,"video_open: setting radio device\n");
		cx_write(MO_GP0_IO, cx88_boards[dev->board].radio.gpio0);
		cx_write(MO_GP1_IO, cx88_boards[dev->board].radio.gpio1);
		cx_write(MO_GP2_IO, cx88_boards[dev->board].radio.gpio2);
		cx_write(MO_GP3_IO, cx88_boards[dev->board].radio.gpio3);
		dev->tvaudio = WW_FM;
		cx88_set_tvaudio(dev);
		cx88_set_stereo(dev,V4L2_TUNER_MODE_STEREO);
		cx8800_call_i2c_clients(dev,AUDC_SET_RADIO,NULL);
	}

        return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct cx8800_fh *fh = file->private_data;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (res_locked(fh->dev,RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(file, &fh->vidq, data, count, ppos);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return -EBUSY;
		return videobuf_read_stream(file, &fh->vbiq, data, count, ppos, 1);
	default:
		BUG();
		return 0;
	}
}

static unsigned int
video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cx8800_fh *fh = file->private_data;

	if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type)
		return videobuf_poll_stream(file, &fh->vbiq, wait);

	/* FIXME */
	return POLLERR;
}

static int video_release(struct inode *inode, struct file *file)
{
	struct cx8800_fh  *fh  = file->private_data;
	struct cx8800_dev *dev = fh->dev;

	/* turn off overlay */
	if (res_check(fh, RESOURCE_OVERLAY)) {
		/* FIXME */
		res_free(dev,fh,RESOURCE_OVERLAY);
	}

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO)) {
		videobuf_queue_cancel(file,&fh->vidq);
		res_free(dev,fh,RESOURCE_VIDEO);
	}
	if (fh->vidq.read_buf) {
		buffer_release(file,fh->vidq.read_buf);
		kfree(fh->vidq.read_buf);
	}

	/* stop vbi capture */
	if (res_check(fh, RESOURCE_VBI)) {
		if (fh->vbiq.streaming)
			videobuf_streamoff(file,&fh->vbiq);
		if (fh->vbiq.reading)
			videobuf_read_stop(file,&fh->vbiq);
		res_free(dev,fh,RESOURCE_VBI);
	}

	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int
video_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct cx8800_fh *fh = file->private_data;

	return videobuf_mmap_mapper(vma, get_queue(fh));
}

/* ------------------------------------------------------------------ */

static int get_control(struct cx8800_dev *dev, struct v4l2_control *ctl)
{
	struct cx88_ctrl *c = NULL;
	u32 value;
	int i;
	
	for (i = 0; i < CX8800_CTLS; i++)
		if (cx8800_ctls[i].v.id == ctl->id)
			c = &cx8800_ctls[i];
	if (NULL == c)
		return -EINVAL;

	value = c->sreg ? cx_sread(c->sreg) : cx_read(c->reg);
	switch (ctl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		ctl->value = (value & 0x40) ? (value & 0x3f) : (0x40 - (value & 0x3f));
		break;
	case V4L2_CID_AUDIO_VOLUME:
		ctl->value = 0x3f - (value & 0x3f);
		break;
	default:
		ctl->value = ((value + (c->off << c->shift)) & c->mask) >> c->shift;
		break;
	}
	return 0;
}

static int set_control(struct cx8800_dev *dev, struct v4l2_control *ctl)
{
	struct cx88_ctrl *c = NULL;
        u32 v_sat_value;
	u32 value;
	int i;

	for (i = 0; i < CX8800_CTLS; i++)
		if (cx8800_ctls[i].v.id == ctl->id)
			c = &cx8800_ctls[i];
	if (NULL == c)
		return -EINVAL;

	if (ctl->value < c->v.minimum)
		return -ERANGE;
	if (ctl->value > c->v.maximum)
		return -ERANGE;
	switch (ctl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		value = (ctl->value < 0x40) ? (0x40 - ctl->value) : ctl->value;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		value = 0x3f - (ctl->value & 0x3f);
		break;
	case V4L2_CID_SATURATION:
		/* special v_sat handling */
		v_sat_value = ctl->value - (0x7f - 0x5a);
		if (v_sat_value > 0xff)
			v_sat_value = 0xff;
		if (v_sat_value < 0x00)
			v_sat_value = 0x00;
		cx_andor(MO_UV_SATURATION, 0xff00, v_sat_value << 8);
		/* fall through to default route for u_sat */
	default:
		value = ((ctl->value - c->off) << c->shift) & c->mask;
		break;
	}
	dprintk(1,"set_control id=0x%X reg=0x%x val=0x%x%s\n",
		ctl->id, c->reg, value, c->sreg ? " [shadowed]" : "");
	if (c->sreg) {
		cx_sandor(c->sreg, c->reg, c->mask, value);
	} else {
		cx_andor(c->reg, c->mask, value);
	}
	return 0;
}

static void init_controls(struct cx8800_dev *dev)
{
	static struct v4l2_control mute = {
		.id    = V4L2_CID_AUDIO_MUTE,
		.value = 1,
	};
	static struct v4l2_control volume = {
		.id    = V4L2_CID_AUDIO_VOLUME,
		.value = 0x3f,
	};

	set_control(dev,&mute);
	set_control(dev,&volume);
}

/* ------------------------------------------------------------------ */

static int cx8800_g_fmt(struct cx8800_dev *dev, struct cx8800_fh *fh,
			struct v4l2_format *f)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(&f->fmt.pix,0,sizeof(f->fmt.pix));
		f->fmt.pix.width        = fh->width;
		f->fmt.pix.height       = fh->height;
		f->fmt.pix.field        = fh->vidq.field;
		f->fmt.pix.pixelformat  = fh->fmt->fourcc;
		f->fmt.pix.bytesperline =
			(f->fmt.pix.width * fh->fmt->depth) >> 3;
		f->fmt.pix.sizeimage =
			f->fmt.pix.height * f->fmt.pix.bytesperline;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		cx8800_vbi_fmt(dev, f);
		return 0;
	default:
		return -EINVAL;
	}
}

static int cx8800_try_fmt(struct cx8800_dev *dev, struct cx8800_fh *fh,
			  struct v4l2_format *f)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		struct cx8800_fmt *fmt;
		enum v4l2_field field;
		unsigned int maxw, maxh;

		fmt = format_by_fourcc(f->fmt.pix.pixelformat);
		if (NULL == fmt)
			return -EINVAL;

		field = f->fmt.pix.field;
		maxw  = norm_maxw(dev->tvnorm);
		maxh  = norm_maxh(dev->tvnorm);

		if (V4L2_FIELD_ANY == field) {
			field = (f->fmt.pix.height > maxh/2)
				? V4L2_FIELD_INTERLACED
				: V4L2_FIELD_BOTTOM;
		}

		switch (field) {
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
			maxh = maxh / 2;
			break;
		case V4L2_FIELD_INTERLACED:
			break;
		default:
			return -EINVAL;
		}

		f->fmt.pix.field = field;
		if (f->fmt.pix.height < 32)
			f->fmt.pix.height = 32;
		if (f->fmt.pix.height > maxh)
			f->fmt.pix.height = maxh;
		if (f->fmt.pix.width < 48)
			f->fmt.pix.width = 48;
		if (f->fmt.pix.width > maxw)
			f->fmt.pix.width = maxw;
		f->fmt.pix.width &= ~0x03;
		f->fmt.pix.bytesperline =
			(f->fmt.pix.width * fmt->depth) >> 3;
		f->fmt.pix.sizeimage =
			f->fmt.pix.height * f->fmt.pix.bytesperline;

		return 0;
	}
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		cx8800_vbi_fmt(dev, f);
		return 0;
	default:
		return -EINVAL;
	}
}

static int cx8800_s_fmt(struct cx8800_dev *dev, struct cx8800_fh *fh,
			struct v4l2_format *f)
{
	int err;
	
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		err = cx8800_try_fmt(dev,fh,f);
		if (0 != err)
			return err;

		fh->fmt        = format_by_fourcc(f->fmt.pix.pixelformat);
		fh->width      = f->fmt.pix.width;
		fh->height     = f->fmt.pix.height;
		fh->vidq.field = f->fmt.pix.field;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		cx8800_vbi_fmt(dev, f);
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * This function is _not_ called directly, but from
 * video_generic_ioctl (and maybe others).  userspace
 * copying is done already, arg is a kernel pointer.
 */
static int video_do_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, void *arg)
{
	struct cx8800_fh  *fh  = file->private_data;
	struct cx8800_dev *dev = fh->dev;
#if 0
	unsigned long flags;
#endif
	int err;

	if (video_debug > 1)
		cx88_print_ioctl(dev->name,cmd);
	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;
		
		memset(cap,0,sizeof(*cap));
                strcpy(cap->driver, "cx8800");
		strlcpy(cap->card, cx88_boards[dev->board].name,
			sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
		cap->version = CX88_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE     |
			V4L2_CAP_STREAMING     |
			V4L2_CAP_VBI_CAPTURE   |
#if 0
			V4L2_CAP_VIDEO_OVERLAY |
#endif
			0;
		if (UNSET != dev->tuner_type)
			cap->capabilities |= V4L2_CAP_TUNER;

		return 0;
	}

	/* ---------- tv norms ---------- */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = arg;
		unsigned int i;

		i = e->index;
		if (i >= ARRAY_SIZE(tvnorms))
			return -EINVAL;
		err = v4l2_video_std_construct(e, tvnorms[e->index].id,
					       tvnorms[e->index].name);
		e->index = i;
		if (err < 0)
			return err;
		return 0;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;

		*id = dev->tvnorm->id;
		return 0;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		unsigned int i;

		for(i = 0; i < ARRAY_SIZE(tvnorms); i++)
			if (*id & tvnorms[i].id)
				break;
		if (i == ARRAY_SIZE(tvnorms))
			return -EINVAL;

		down(&dev->lock);
		set_tvnorm(dev,&tvnorms[i]);
		up(&dev->lock);
		return 0;
	}

	/* ------ input switching ---------- */
	case VIDIOC_ENUMINPUT:
	{
		static const char *iname[] = {
			[ CX88_VMUX_COMPOSITE1 ] = "Composite1",
			[ CX88_VMUX_COMPOSITE2 ] = "Composite2",
			[ CX88_VMUX_COMPOSITE3 ] = "Composite3",
			[ CX88_VMUX_COMPOSITE4 ] = "Composite4",
			[ CX88_VMUX_TELEVISION ] = "Television",
			[ CX88_VMUX_SVIDEO     ] = "S-Video",
			[ CX88_VMUX_DEBUG      ] = "for debug only",
		};
		struct v4l2_input *i = arg;
		unsigned int n;

		n = i->index;
		if (n >= 4)
			return -EINVAL;
		if (0 == INPUT(n)->type)
			return -EINVAL;
		memset(i,0,sizeof(*i));
		i->index = n;
		i->type  = V4L2_INPUT_TYPE_CAMERA;
		strcpy(i->name,iname[INPUT(n)->type]);
		if (CX88_VMUX_TELEVISION == INPUT(n)->type)
			i->type = V4L2_INPUT_TYPE_TUNER;
		for (n = 0; n < ARRAY_SIZE(tvnorms); n++)
			i->std |= tvnorms[n].id;
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		unsigned int *i = arg;

		*i = dev->input;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		unsigned int *i = arg;
		
		if (*i >= 4)
			return -EINVAL;
		down(&dev->lock);
		video_mux(dev,*i);
		up(&dev->lock);
		return 0;
	}

	/* --- capture ioctls ---------------------------------------- */
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		enum v4l2_buf_type type;
		unsigned int index;

		index = f->index;
		type  = f->type;
		switch (type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (index >= ARRAY_SIZE(formats))
				return -EINVAL;
			memset(f,0,sizeof(*f));
			f->index = index;
			f->type  = type;
			strlcpy(f->description,formats[index].name,sizeof(f->description));
			f->pixelformat = formats[index].fourcc;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;
		return cx8800_g_fmt(dev,fh,f);
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;
		return cx8800_s_fmt(dev,fh,f);
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;
		return cx8800_try_fmt(dev,fh,f);
	}

	/* --- controls ---------------------------------------------- */
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = arg;
		int i;

		if (c->id <  V4L2_CID_BASE ||
		    c->id >= V4L2_CID_LASTP1)
			return -EINVAL;
		for (i = 0; i < CX8800_CTLS; i++)
			if (cx8800_ctls[i].v.id == c->id)
				break;
		if (i == CX8800_CTLS) {
			*c = no_ctl;
			return 0;
		}
		*c = cx8800_ctls[i].v;
		return 0;
	}
	case VIDIOC_G_CTRL:
		return get_control(dev,arg);
	case VIDIOC_S_CTRL:
		return set_control(dev,arg);
		
	/* --- tuner ioctls ------------------------------------------ */
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;
		u32 reg;
		
		if (UNSET == dev->tuner_type)
			return -EINVAL;
		if (0 != t->index)
			return -EINVAL;

		memset(t,0,sizeof(*t));
		strcpy(t->name, "Television");
		t->type       = V4L2_TUNER_ANALOG_TV;
		t->capability = V4L2_TUNER_CAP_NORM;
		t->rangehigh  = 0xffffffffUL;

		cx88_get_stereo(dev ,t);
		reg = cx_read(MO_DEVICE_STATUS);
                t->signal = (reg & (1<<5)) ? 0xffff : 0x0000;
		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (UNSET == dev->tuner_type)
			return -EINVAL;
		if (0 != t->index)
			return -EINVAL;
		cx88_set_stereo(dev,t->audmode);
		return 0;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (UNSET == dev->tuner_type)
			return -EINVAL;
		if (f->tuner != 0)
			return -EINVAL;
		memset(f,0,sizeof(*f));
		f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
		f->frequency = dev->freq;
		return 0;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (UNSET == dev->tuner_type)
			return -EINVAL;
		if (f->tuner != 0)
			return -EINVAL;
		if (0 == fh->radio && f->type != V4L2_TUNER_ANALOG_TV)
			return -EINVAL;
		if (1 == fh->radio && f->type != V4L2_TUNER_RADIO)
			return -EINVAL;
		down(&dev->lock);
		dev->freq = f->frequency;
#ifdef V4L2_I2C_CLIENTS
		cx8800_call_i2c_clients(dev,VIDIOC_S_FREQUENCY,f);
#else
		cx8800_call_i2c_clients(dev,VIDIOCSFREQ,&dev->freq);
#endif
		up(&dev->lock);
		return 0;
	}

	/* --- streaming capture ------------------------------------- */
	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		struct videobuf_queue *q;
		struct v4l2_requestbuffers req;
		unsigned int i;

		q = get_queue(fh);
		memset(&req,0,sizeof(req));
		req.type   = q->type;
		req.count  = 8;
		req.memory = V4L2_MEMORY_MMAP;
		err = videobuf_reqbufs(file,q,&req);
		if (err < 0)
			return err;
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = req.count;
		mbuf->size   = 0;
		for (i = 0; i < mbuf->frames; i++) {
			mbuf->offsets[i]  = q->bufs[i]->boff;
			mbuf->size       += q->bufs[i]->bsize;
		}
		return 0;
	}
	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(file, get_queue(fh), arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(get_queue(fh), arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(file, get_queue(fh), arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(file, get_queue(fh), arg);

	case VIDIOC_STREAMON:
	{
		int res = get_ressource(fh);

                if (!res_get(dev,fh,res))
			return -EBUSY;
		return videobuf_streamon(file, get_queue(fh));
	}
	case VIDIOC_STREAMOFF:
	{
		int res = get_ressource(fh);

		err = videobuf_streamoff(file, get_queue(fh));
		if (err < 0)
			return err;
		res_free(dev,fh,res);
		return 0;
	}

	default:
		return v4l_compat_translate_ioctl(inode,file,cmd,arg,
						  video_do_ioctl);
	}
	return 0;
}

static int video_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, video_do_ioctl);
}

/* ----------------------------------------------------------- */

static int radio_do_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *arg)
{
	struct cx8800_fh *fh = file->private_data;
	struct cx8800_dev *dev = fh->dev;
	
	if (video_debug > 1)
		cx88_print_ioctl(dev->name,cmd);

	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;
		
		memset(cap,0,sizeof(*cap));
                strcpy(cap->driver, "cx8800");
		strlcpy(cap->card, cx88_boards[dev->board].name,
			sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s", pci_name(dev->pci));
		cap->version = CX88_VERSION_CODE;
		cap->capabilities = V4L2_CAP_TUNER;
		return 0;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (t->index > 0)
			return -EINVAL;

		memset(t,0,sizeof(*t));
		strcpy(t->name, "Radio");
                t->rangelow  = (int)(65*16);
                t->rangehigh = (int)(108*16);
		
#ifdef V4L2_I2C_CLIENTS
		cx8800_call_i2c_clients(dev,VIDIOC_G_TUNER,t);
#else
		{
			struct video_tuner vt;
			memset(&vt,0,sizeof(vt));
			cx8800_call_i2c_clients(dev,VIDIOCGTUNER,&vt);
			t->signal = vt.signal;
		}
#endif
		return 0;
	}
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;
		
		if (i->index != 0)
			return -EINVAL;
		strcpy(i->name,"Radio");
		i->type = V4L2_INPUT_TYPE_TUNER;
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *i = arg;
		*i = 0;
		return 0;
	}
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		memset(a,0,sizeof(*a));
		strcpy(a->name,"Radio");
		return 0;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;
		*id = 0;
		return 0;
	}
	case VIDIOC_S_AUDIO:
	case VIDIOC_S_TUNER:
	case VIDIOC_S_INPUT:
	case VIDIOC_S_STD:
		return 0;

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = arg;
		int i;

		if (c->id <  V4L2_CID_BASE ||
		    c->id >= V4L2_CID_LASTP1)
			return -EINVAL;
		if (c->id == V4L2_CID_AUDIO_MUTE) {
			for (i = 0; i < CX8800_CTLS; i++)
				if (cx8800_ctls[i].v.id == c->id)
					break;
			*c = cx8800_ctls[i].v;
		} else
			*c = no_ctl;
		return 0;
	}


	case VIDIOC_G_CTRL:
	case VIDIOC_S_CTRL:
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
		return video_do_ioctl(inode,file,cmd,arg);
		
	default:
		return v4l_compat_translate_ioctl(inode,file,cmd,arg,
						  radio_do_ioctl);
	}
	return 0;
};

static int radio_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, radio_do_ioctl);
};

/* ----------------------------------------------------------- */

static void cx8800_vid_timeout(unsigned long data)
{
	struct cx8800_dev *dev = (struct cx8800_dev*)data;
	struct cx88_dmaqueue *q = &dev->vidq;
	struct cx88_buffer *buf;
	unsigned long flags;

	cx88_sram_channel_dump(dev, &cx88_sram_channels[SRAM_CH21]);
	//cx88_risc_disasm(dev,&dev->vidq.stopper);
	
	cx_clear(MO_VID_DMACNTRL, 0x11);
	cx_clear(VID_CAPTURE_CONTROL, 0x06);

	spin_lock_irqsave(&dev->slock,flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = STATE_ERROR;
		wake_up(&buf->vb.done);
		printk("%s: [%p/%d] timeout - dma=0x%08lx\n", dev->name,
		       buf, buf->vb.i, (unsigned long)buf->risc.dma);
	}
	restart_video_queue(dev,q);
	spin_unlock_irqrestore(&dev->slock,flags);
}

static void cx8800_wakeup(struct cx8800_dev *dev,
			  struct cx88_dmaqueue *q, u32 count)
{
	struct cx88_buffer *buf;

	for (;;) {
		if (list_empty(&q->active))
			break;
		buf = list_entry(q->active.next,
				 struct cx88_buffer, vb.queue);
		if (buf->count > count)
			break;
		do_gettimeofday(&buf->vb.ts);
		dprintk(2,"[%p/%d] wakeup reg=%d buf=%d\n",buf,buf->vb.i,
			count, buf->count);
		buf->vb.state = STATE_DONE;
		list_del(&buf->vb.queue);
		wake_up(&buf->vb.done);
	}
	if (list_empty(&q->active)) {
		del_timer(&q->timeout);
	} else {
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
	}
}

static void cx8800_vid_irq(struct cx8800_dev *dev)
{
	u32 status, mask, count;

	status = cx_read(MO_VID_INTSTAT);
	mask   = cx_read(MO_VID_INTMSK);
	if (0 == (status & mask))
		return;
	cx_write(MO_VID_INTSTAT, status);
	if (irq_debug  ||  (status & mask & ~0xff))
		cx88_print_irqbits(dev->name, "irq vid",
				   cx88_vid_irqs, status, mask);

	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s: video risc op code error\n",dev->name);
		cx_clear(MO_VID_DMACNTRL, 0x11);
		cx_clear(VID_CAPTURE_CONTROL, 0x06);
		cx88_sram_channel_dump(dev, &cx88_sram_channels[SRAM_CH21]);
	}
	
	/* risc1 y */
	if (status & 0x01) {
		spin_lock(&dev->slock);
		count = cx_read(MO_VIDY_GPCNT);
		cx8800_wakeup(dev, &dev->vidq, count);
		spin_unlock(&dev->slock);
	}

	/* risc1 vbi */
	if (status & 0x08) {
		spin_lock(&dev->slock);
		count = cx_read(MO_VBI_GPCNT);
		cx8800_wakeup(dev, &dev->vbiq, count);
		spin_unlock(&dev->slock);
	}

	/* risc2 y */
	if (status & 0x10) {
		dprintk(2,"stopper video\n");
		spin_lock(&dev->slock);
		restart_video_queue(dev,&dev->vidq);
		spin_unlock(&dev->slock);
	}

	/* risc2 vbi */
	if (status & 0x80) {
		dprintk(2,"stopper vbi\n");
		spin_lock(&dev->slock);
		cx8800_restart_vbi_queue(dev,&dev->vbiq);
		spin_unlock(&dev->slock);
	}
}

static irqreturn_t cx8800_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cx8800_dev *dev = dev_id;
	u32 status, mask;
	int loop, handled = 0;

	for (loop = 0; loop < 10; loop++) {
		status = cx_read(MO_PCI_INTSTAT);
		mask   = cx_read(MO_PCI_INTMSK);
		if (0 == (status & mask))
			goto out;
		handled = 1;
		cx_write(MO_PCI_INTSTAT, status);
		if (irq_debug  ||  (status & mask & ~0x1f))
			cx88_print_irqbits(dev->name, "irq pci",
					   cx88_pci_irqs, status, mask);

		if (status & 1)
			cx8800_vid_irq(dev);
	};
	if (10 == loop) {
		printk(KERN_WARNING "%s: irq loop -- clearing mask\n",
		       dev->name);
		cx_write(MO_PCI_INTMSK,0);
	}
	
 out:
	return IRQ_RETVAL(handled);
}

/* ----------------------------------------------------------- */
/* exported stuff                                              */

static struct file_operations video_fops =
{
	.owner	       = THIS_MODULE,
	.open	       = video_open,
	.release       = video_release,
	.read	       = video_read,
	.poll          = video_poll,
	.mmap	       = video_mmap,
	.ioctl	       = video_ioctl,
	.llseek        = no_llseek,
};

struct video_device cx8800_video_template =
{
	.name          = "cx8800-video",
	.type          = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_SCALES,
	.hardware      = 0,
	.fops          = &video_fops,
	.minor         = -1,
};

struct video_device cx8800_vbi_template =
{
	.name          = "cx8800-vbi",
	.type          = VID_TYPE_TELETEXT|VID_TYPE_TUNER,
	.hardware      = 0,
	.fops          = &video_fops,
	.minor         = -1,
};

static struct file_operations radio_fops =
{
	.owner         = THIS_MODULE,
	.open          = video_open,
	.release       = video_release,
	.ioctl         = radio_ioctl,
	.llseek        = no_llseek,
};

struct video_device cx8800_radio_template =
{
	.name          = "cx8800-radio",
	.type          = VID_TYPE_TUNER,
	.hardware      = 0,
	.fops          = &radio_fops,
	.minor         = -1,
};

/* ----------------------------------------------------------- */

static void cx8800_shutdown(struct cx8800_dev *dev)
{
	/* disable RISC controller + IRQs */
	cx_write(MO_DEV_CNTRL2, 0);

	/* stop dma transfers */
	cx_write(MO_VID_DMACNTRL, 0x0);
	cx_write(MO_AUD_DMACNTRL, 0x0);
	cx_write(MO_TS_DMACNTRL, 0x0);
	cx_write(MO_VIP_DMACNTRL, 0x0);
	cx_write(MO_GPHST_DMACNTRL, 0x0);

	/* stop interupts */
	cx_write(MO_PCI_INTMSK, 0x0);
	cx_write(MO_VID_INTMSK, 0x0);
	cx_write(MO_AUD_INTMSK, 0x0);
	cx_write(MO_TS_INTMSK, 0x0);
	cx_write(MO_VIP_INTMSK, 0x0);
	cx_write(MO_GPHST_INTMSK, 0x0);

	/* stop capturing */
	cx_write(VID_CAPTURE_CONTROL, 0);
}

static int cx8800_reset(struct cx8800_dev *dev)
{
	dprintk(1,"cx8800_reset\n");

	cx8800_shutdown(dev);
	
	/* clear irq status */
	cx_write(MO_VID_INTSTAT, 0xFFFFFFFF); // Clear PIV int
	cx_write(MO_PCI_INTSTAT, 0xFFFFFFFF); // Clear PCI int
	cx_write(MO_INT1_STAT,   0xFFFFFFFF); // Clear RISC int

	/* wait a bit */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/10);
	
	/* init sram */
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH21], 720*4, 0);
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH22], 128, 0);
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH23], 128, 0);
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH24], 128, 0);
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH25], 128, 0);
	cx88_sram_channel_setup(dev, &cx88_sram_channels[SRAM_CH26], 128, 0);
	
	/* misc init ... */
	cx_write(MO_INPUT_FORMAT, ((1 << 13) |   // agc enable
				   (1 << 12) |   // agc gain
				   (1 << 11) |   // adaptibe agc
				   (0 << 10) |   // chroma agc
				   (0 <<  9) |   // ckillen
				   (7)));

	/* setup image format */
	cx_andor(MO_COLOR_CTRL, 0x4000, 0x4000);

	/* setup FIFO Threshholds */
	cx_write(MO_PDMA_STHRSH,   0x0807);
	cx_write(MO_PDMA_DTHRSH,   0x0807);

	/* fixes flashing of image */
	cx_write(MO_AGC_SYNC_TIP1, 0x0380000F);
	cx_write(MO_AGC_BACK_VBI,  0x00E00555);
	
	cx_write(MO_VID_INTSTAT,   0xFFFFFFFF); // Clear PIV int
	cx_write(MO_PCI_INTSTAT,   0xFFFFFFFF); // Clear PCI int
	cx_write(MO_INT1_STAT,     0xFFFFFFFF); // Clear RISC int

	return 0;
}

static struct video_device *vdev_init(struct cx8800_dev *dev,
				      struct video_device *template,
				      char *type)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template;
	vfd->minor   = -1;
	vfd->dev     = &dev->pci->dev;
	vfd->release = video_device_release;
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)",
		 dev->name, type, cx88_boards[dev->board].name);
	return vfd;
}

static void cx8800_unregister_video(struct cx8800_dev *dev)
{
	if (dev->radio_dev) {
		if (-1 != dev->radio_dev->minor)
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}
	if (dev->vbi_dev) {
		if (-1 != dev->vbi_dev->minor)
			video_unregister_device(dev->vbi_dev);
		else
			video_device_release(dev->vbi_dev);
		dev->vbi_dev = NULL;
	}
	if (dev->video_dev) {
		if (-1 != dev->video_dev->minor)
			video_unregister_device(dev->video_dev);
		else
			video_device_release(dev->video_dev);
		dev->video_dev = NULL;
	}
}

static int __devinit cx8800_initdev(struct pci_dev *pci_dev,
				    const struct pci_device_id *pci_id)
{
	struct cx8800_dev *dev;
	unsigned int i;
	int err;

	dev = kmalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;
	memset(dev,0,sizeof(*dev));

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail1;
	}
	sprintf(dev->name,"cx%x[%d]",pci_dev->device,cx8800_devcount);

	/* pci quirks */
	cx88_pci_quirks(dev->name, dev->pci, &latency);
	if (UNSET != latency) {
		printk(KERN_INFO "%s: setting pci latency timer to %d\n",
		       dev->name,latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, latency);
	}

	/* print pci info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
        pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
        printk(KERN_INFO "%s: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%lx\n", dev->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
	       dev->pci_lat,pci_resource_start(pci_dev,0));

	pci_set_master(pci_dev);
	if (!pci_dma_supported(pci_dev,0xffffffff)) {
		printk("%s: Oops: no 32bit PCI DMA ???\n",dev->name);
		err = -EIO;
		goto fail1;
	}

	/* board config */
	dev->board = card[cx8800_devcount];
	for (i = 0; UNSET == dev->board  &&  i < cx88_idcount; i++) 
		if (pci_dev->subsystem_vendor == cx88_subids[i].subvendor &&
		    pci_dev->subsystem_device == cx88_subids[i].subdevice)
			dev->board = cx88_subids[i].card;
	if (UNSET == dev->board) {
		dev->board = CX88_BOARD_UNKNOWN;
		cx88_card_list(dev);
	}
        printk(KERN_INFO "%s: subsystem: %04x:%04x, board: %s [card=%d,%s]\n",
	       dev->name,pci_dev->subsystem_vendor,
	       pci_dev->subsystem_device,cx88_boards[dev->board].name,
	       dev->board, card[cx8800_devcount] == dev->board ?
	       "insmod option" : "autodetected");

	dev->tuner_type = tuner[cx8800_devcount];
	if (UNSET == dev->tuner_type)
		dev->tuner_type = cx88_boards[dev->board].tuner_type;

	/* get mmio */
	if (!request_mem_region(pci_resource_start(pci_dev,0),
				pci_resource_len(pci_dev,0),
				dev->name)) {
		err = -EBUSY;
		printk(KERN_ERR "%s: can't get MMIO memory @ 0x%lx\n",
		       dev->name,pci_resource_start(pci_dev,0));
		goto fail1;
	}
	dev->lmmio = ioremap(pci_resource_start(pci_dev,0),
			     pci_resource_len(pci_dev,0));
	dev->bmmio = (u8*)dev->lmmio;

	/* initialize driver struct */
        init_MUTEX(&dev->lock);
	dev->slock = SPIN_LOCK_UNLOCKED;
	dev->tvnorm = tvnorms;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);
	dev->vidq.timeout.function = cx8800_vid_timeout;
	dev->vidq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vidq.timeout);
	cx88_risc_stopper(dev->pci,&dev->vidq.stopper,
			  MO_VID_DMACNTRL,0x11,0x00);

	/* init vbi dma queues */
	INIT_LIST_HEAD(&dev->vbiq.active);
	INIT_LIST_HEAD(&dev->vbiq.queued);
	dev->vbiq.timeout.function = cx8800_vbi_timeout;
	dev->vbiq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vbiq.timeout);
	cx88_risc_stopper(dev->pci,&dev->vbiq.stopper,
			  MO_VID_DMACNTRL,0x88,0x00);

	/* initialize hardware */
	cx8800_reset(dev);

	/* get irq */
	err = request_irq(pci_dev->irq, cx8800_irq,
			  SA_SHIRQ | SA_INTERRUPT, dev->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get IRQ %d\n",
		       dev->name,pci_dev->irq);
		goto fail2;
	}

	/* register i2c bus + load i2c helpers */
	cx8800_i2c_init(dev);
	cx88_card_setup(dev);

	/* load and configure helper modules */
	if (TUNER_ABSENT != dev->tuner_type)
		request_module("tuner");
	if (cx88_boards[dev->board].needs_tda9887)
		request_module("tda9887");
	if (dev->tuner_type != UNSET)
		cx8800_call_i2c_clients(dev,TUNER_SET_TYPE,&dev->tuner_type);

	/* register v4l devices */
	dev->video_dev = vdev_init(dev,&cx8800_video_template,"video");
	err = video_register_device(dev->video_dev,VFL_TYPE_GRABBER,
				    video_nr[cx8800_devcount]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       dev->name);
		goto fail3;
	}
	printk(KERN_INFO "%s: registered device video%d [v4l2]\n",
	       dev->name,dev->video_dev->minor & 0x1f);

	dev->vbi_dev = vdev_init(dev,&cx8800_vbi_template,"vbi");
	err = video_register_device(dev->vbi_dev,VFL_TYPE_VBI,
				    vbi_nr[cx8800_devcount]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register vbi device\n",
		       dev->name);
		goto fail3;
	}
	printk(KERN_INFO "%s: registered device vbi%d\n",
	       dev->name,dev->vbi_dev->minor & 0x1f);

	if (dev->has_radio) {
		dev->radio_dev = vdev_init(dev,&cx8800_radio_template,"radio");
		err = video_register_device(dev->radio_dev,VFL_TYPE_RADIO,
					    radio_nr[cx8800_devcount]);
		if (err < 0) {
			printk(KERN_INFO "%s: can't register radio device\n",
			       dev->name);
			goto fail3;
		}
		printk(KERN_INFO "%s: registered device radio%d\n",
		       dev->name,dev->radio_dev->minor & 0x1f);
	}

	/* everything worked */
	list_add_tail(&dev->devlist,&cx8800_devlist);
	pci_set_drvdata(pci_dev,dev);
	cx8800_devcount++;

	/* initial device configuration */
	down(&dev->lock);
	init_controls(dev);
	set_tvnorm(dev,tvnorms);
	video_mux(dev,0);
	up(&dev->lock);

	/* start tvaudio thread */
	init_completion(&dev->texit);
	dev->tpid = kernel_thread(cx88_audio_thread, dev, 0);
	return 0;

 fail3:
	cx8800_unregister_video(dev);
	if (0 == dev->i2c_rc)
		i2c_bit_del_bus(&dev->i2c_adap);
	free_irq(pci_dev->irq, dev);
 fail2:
	release_mem_region(pci_resource_start(pci_dev,0),
			   pci_resource_len(pci_dev,0));
 fail1:
	kfree(dev);
	return err;
}

static void __devexit cx8800_finidev(struct pci_dev *pci_dev)
{
        struct cx8800_dev *dev = pci_get_drvdata(pci_dev);

	/* stop thread */
	dev->shutdown = 1;
	if (dev->tpid >= 0)
		wait_for_completion(&dev->texit);

	cx8800_shutdown(dev);
	pci_disable_device(pci_dev);

	/* unregister stuff */	
	if (0 == dev->i2c_rc)
		i2c_bit_del_bus(&dev->i2c_adap);

	free_irq(pci_dev->irq, dev);
	release_mem_region(pci_resource_start(pci_dev,0),
			   pci_resource_len(pci_dev,0));

	cx8800_unregister_video(dev);
	pci_set_drvdata(pci_dev, NULL);

	/* free memory */
	btcx_riscmem_free(dev->pci,&dev->vidq.stopper);
	list_del(&dev->devlist);
	cx8800_devcount--;
	kfree(dev);
}

static int cx8800_suspend(struct pci_dev *pci_dev, u32 state)
{
        struct cx8800_dev *dev = pci_get_drvdata(pci_dev);

	printk("%s: suspend %d\n", dev->name, state);

	cx8800_shutdown(dev);
	del_timer(&dev->vidq.timeout);
	
	pci_save_state(pci_dev, dev->state.pci_cfg);
	if (0 != pci_set_power_state(pci_dev, state)) {
		pci_disable_device(pci_dev);
		dev->state.disabled = 1;
	}
	return 0;
}

static int cx8800_resume(struct pci_dev *pci_dev)
{
        struct cx8800_dev *dev = pci_get_drvdata(pci_dev);

	printk("%s: resume\n", dev->name);

	if (dev->state.disabled) {
		pci_enable_device(pci_dev);
		dev->state.disabled = 0;
	}
	pci_set_power_state(pci_dev, 0);
	pci_restore_state(pci_dev, dev->state.pci_cfg);

	/* re-initialize hardware */
	cx8800_reset(dev);

	/* restart video capture */
	spin_lock(&dev->slock);
	restart_video_queue(dev,&dev->vidq);
	spin_unlock(&dev->slock);

	return 0;
}

/* ----------------------------------------------------------- */

struct pci_device_id cx8800_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8800,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, cx8800_pci_tbl);

static struct pci_driver cx8800_pci_driver = {
        .name     = "cx8800",
        .id_table = cx8800_pci_tbl,
        .probe    = cx8800_initdev,
        .remove   = cx8800_finidev,

	.suspend  = cx8800_suspend,
	.resume   = cx8800_resume,
};

static int cx8800_init(void)
{
	INIT_LIST_HEAD(&cx8800_devlist);
	printk(KERN_INFO "cx2388x v4l2 driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_module_init(&cx8800_pci_driver);
}

static void cx8800_fini(void)
{
	pci_unregister_driver(&cx8800_pci_driver);
}

module_init(cx8800_init);
module_exit(cx8800_fini);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
