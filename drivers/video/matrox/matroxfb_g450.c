/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.64 2002/06/02
 *
 * See matroxfb_base.c for contributors.
 *
 */

#include "matroxfb_base.h"
#include "matroxfb_misc.h"
#include "matroxfb_DAC1064.h"
#include "g450_pll.h"
#include <linux/matroxfb.h>
#include <asm/uaccess.h>
#include <asm/div64.h>

static void cve2_set_reg(WPMINFO int reg, int val) {
	unsigned long flags;

	matroxfb_DAC_lock_irqsave(flags);
	matroxfb_DAC_out(PMINFO 0x87, reg);
	matroxfb_DAC_out(PMINFO 0x88, val);
	matroxfb_DAC_unlock_irqrestore(flags);
}

struct output_desc {
	unsigned int	h_vis;
	unsigned int	h_f_porch;
	unsigned int	h_sync;
	unsigned int	h_b_porch;
	unsigned long long int	chromasc;
	unsigned int	burst;
	unsigned int	v_total;
};

static void computeRegs(WPMINFO struct mavenregs* r, struct my_timming* mt, const struct output_desc* outd) {
	u_int32_t chromasc;
	u_int32_t hlen;
	u_int32_t hsl;
	u_int32_t hbp;
	u_int32_t hfp;
	u_int32_t hvis;
	unsigned int pixclock;
	unsigned long long piic;
	int mnp;
	int over;
	
	r->regs[0x80] = 0x03;	/* | 0x40 for SCART */

	hvis = ((mt->HDisplay << 1) + 3) & ~3;
	
	if (hvis >= 2048) {
		hvis = 2044;
	}
	
	piic = 1000000000ULL * hvis;
	do_div(piic, outd->h_vis);

	dprintk(KERN_DEBUG "Want %u kHz pixclock\n", (unsigned int)piic);
	
	mnp = matroxfb_g450_setclk(PMINFO piic, M_VIDEO_PLL);
	
	mt->mnp = mnp;
	mt->pixclock = g450_mnp2f(PMINFO mnp);

	dprintk(KERN_DEBUG "MNP=%08X\n", mnp);

	pixclock = 1000000000U / mt->pixclock;

	dprintk(KERN_DEBUG "Got %u ps pixclock\n", pixclock);

	piic = outd->chromasc;
	do_div(piic, mt->pixclock);
	chromasc = piic;
	
	dprintk(KERN_DEBUG "Chroma is %08X\n", chromasc);

	r->regs[0] = piic >> 24;
	r->regs[1] = piic >> 16;
	r->regs[2] = piic >>  8;
	r->regs[3] = piic >>  0;
	hbp = (((outd->h_b_porch + pixclock) / pixclock)) & ~1;
	hfp = (((outd->h_f_porch + pixclock) / pixclock)) & ~1;
	hsl = (((outd->h_sync + pixclock) / pixclock)) & ~1;
	hlen = hvis + hfp + hsl + hbp;
	over = hlen & 0x0F;
	
	dprintk(KERN_DEBUG "WL: vis=%u, hf=%u, hs=%u, hb=%u, total=%u\n", hvis, hfp, hsl, hbp, hlen);

	if (over) {
		hfp -= over;
		hlen -= over;
		if (over <= 2) {
		} else if (over < 10) {
			hfp += 4;
			hlen += 4;
		} else {
			hfp += 16;
			hlen += 16;
		}
	}

	/* maybe cve2 has requirement 800 < hlen < 1184 */
	r->regs[0x08] = hsl;
	r->regs[0x09] = (outd->burst + pixclock - 1) / pixclock;	/* burst length */
	r->regs[0x0A] = hbp;
	r->regs[0x2C] = hfp;
	r->regs[0x31] = hvis / 8;
	r->regs[0x32] = hvis & 7;
	
	dprintk(KERN_DEBUG "PG: vis=%04X, hf=%02X, hs=%02X, hb=%02X, total=%04X\n", hvis, hfp, hsl, hbp, hlen);

	r->regs[0x84] = 1;	/* x sync point */
	r->regs[0x85] = 0;
	hvis = hvis >> 1;
	hlen = hlen >> 1;
	
	dprintk(KERN_DEBUG "hlen=%u hvis=%u\n", hlen, hvis);

	mt->interlaced = 1;

	mt->HDisplay = hvis & ~7;
	mt->HSyncStart = mt->HDisplay + 8;
	mt->HSyncEnd = (hlen & ~7) - 8;
	mt->HTotal = hlen;

	{
		int upper;
		unsigned int vtotal;
		unsigned int vsyncend;
		unsigned int vdisplay;
		
		vtotal = mt->VTotal;
		vsyncend = mt->VSyncEnd;
		vdisplay = mt->VDisplay;
		if (vtotal < outd->v_total) {
			unsigned int yovr = outd->v_total - vtotal;
			
			vsyncend += yovr >> 1;
		} else if (vtotal > outd->v_total) {
			vdisplay = outd->v_total - 4;
			vsyncend = outd->v_total;
		}
		upper = (outd->v_total - vsyncend) >> 1;	/* in field lines */
		r->regs[0x17] = outd->v_total / 4;
		r->regs[0x18] = outd->v_total & 3;
		r->regs[0x33] = upper - 1;	/* upper blanking */
		r->regs[0x82] = upper;		/* y sync point */
		r->regs[0x83] = upper >> 8;
		
		mt->VDisplay = vdisplay;
		mt->VSyncStart = outd->v_total - 2;
		mt->VSyncEnd = outd->v_total;
		mt->VTotal = outd->v_total;
	}
}

static void cve2_init_TVdata(int norm, struct mavenregs* data, const struct output_desc** outd) {
	static const struct output_desc paloutd = {
		.h_vis	   = 52148148,	// ps
		.h_f_porch =  1407407,	// ps
		.h_sync    =  4666667,	// ps
		.h_b_porch =  5777778,	// ps
		.chromasc  = 19042247534182ULL,	// 4433618.750 Hz
		.burst     =  2518518,	// ps
		.v_total   =      625,
	};
	static const struct output_desc ntscoutd = {
		.h_vis     = 52888889,	// ps
		.h_f_porch =  1333333,	// ps
		.h_sync    =  4666667,	// ps
		.h_b_porch =  4666667,	// ps
		.chromasc  = 15374030659475ULL,	// 3579545.454 Hz
		.burst     =  2418418,	// ps
		.v_total   =      525,	// lines
	};

	static const struct mavenregs palregs = { {
		0x2A, 0x09, 0x8A, 0xCB,	/* 00: chroma subcarrier */
		0x00,
		0x00,	/* test */
		0xF9,	/* modified by code (F9 written...) */
		0x00,	/* ? not written */
		0x7E,	/* 08 */
		0x44,	/* 09 */
		0x9C,	/* 0A */
		0x2E,	/* 0B */
		0x21,	/* 0C */
		0x00,	/* ? not written */
//		0x3F, 0x03, /* 0E-0F */
		0x3C, 0x03,
		0x3C, 0x03, /* 10-11 */
		0x1A,	/* 12 */
		0x2A,	/* 13 */
		0x1C, 0x3D, 0x14, /* 14-16 */
		0x9C, 0x01, /* 17-18 */
		0x00,	/* 19 */
		0xFE,	/* 1A */
		0x7E,	/* 1B */
		0x60,	/* 1C */
		0x05,	/* 1D */
//		0x89, 0x03, /* 1E-1F */
		0xAD, 0x03,
//		0x72,	/* 20 */
		0xA5,
		0x07,	/* 21 */
//		0x72,	/* 22 */
		0xA5,
		0x00,	/* 23 */
		0x00,	/* 24 */
		0x00,	/* 25 */
		0x08,	/* 26 */
		0x04,	/* 27 */
		0x00,	/* 28 */
		0x1A,	/* 29 */
		0x55, 0x01, /* 2A-2B */
		0x26,	/* 2C */
		0x07, 0x7E, /* 2D-2E */
		0x02, 0x54, /* 2F-30 */
		0xB0, 0x00, /* 31-32 */
		0x14,	/* 33 */
		0x49,	/* 34 */
		0x00,	/* 35 written multiple times */
		0x00,	/* 36 not written */
		0xA3,	/* 37 */
		0xC8,	/* 38 */
		0x22,	/* 39 */
		0x02,	/* 3A */
		0x22,	/* 3B */
		0x3F, 0x03, /* 3C-3D */
		0x00,	/* 3E written multiple times */
		0x00,	/* 3F not written */
	} };
	static struct mavenregs ntscregs = { {
		0x21, 0xF0, 0x7C, 0x1F,	/* 00: chroma subcarrier */
		0x00,
		0x00,	/* test */
		0xF9,	/* modified by code (F9 written...) */
		0x00,	/* ? not written */
		0x7E,	/* 08 */
		0x43,	/* 09 */
		0x7E,	/* 0A */
		0x3D,	/* 0B */
		0x00,	/* 0C */
		0x00,	/* ? not written */
		0x41, 0x00, /* 0E-0F */
		0x3C, 0x00, /* 10-11 */
		0x17,	/* 12 */
		0x21,	/* 13 */
		0x1B, 0x1B, 0x24, /* 14-16 */
		0x83, 0x01, /* 17-18 */
		0x00,	/* 19 */
		0x0F,	/* 1A */
		0x0F,	/* 1B */
		0x60,	/* 1C */
		0x05,	/* 1D */
		//0x89, 0x02, /* 1E-1F */
		0xC0, 0x02, /* 1E-1F */
		//0x5F,	/* 20 */
		0x9C,	/* 20 */
		0x04,	/* 21 */
		//0x5F,	/* 22 */
		0x9C,	/* 22 */
		0x01,	/* 23 */
		0x02,	/* 24 */
		0x00,	/* 25 */
		0x0A,	/* 26 */
		0x05,	/* 27 */
		0x00,	/* 28 */
		0x10,	/* 29 */
		0xFF, 0x03, /* 2A-2B */
		0x24,	/* 2C */
		0x0F, 0x78, /* 2D-2E */
		0x00, 0x00, /* 2F-30 */
		0xB2, 0x04, /* 31-32 */
		0x14,	/* 33 */
		0x02,	/* 34 */
		0x00,	/* 35 written multiple times */
		0x00,	/* 36 not written */
		0xA3,	/* 37 */
		0xC8,	/* 38 */
		0x15,	/* 39 */
		0x05,	/* 3A */
		0x3B,	/* 3B */
		0x3C, 0x00, /* 3C-3D */
		0x00,	/* 3E written multiple times */
		0x00,	/* never written */
	} };

	if (norm == MATROXFB_OUTPUT_MODE_PAL) {
		*data = palregs;
		*outd = &paloutd;
	} else {
  		*data = ntscregs;
		*outd = &ntscoutd;
	}
 	return;
}

#define LR(x) cve2_set_reg(PMINFO (x), m->regs[(x)])
static void cve2_init_TV(WPMINFO const struct mavenregs* m) {
	int i;

	LR(0x80);
	LR(0x82); LR(0x83);
	LR(0x84); LR(0x85);
	
	cve2_set_reg(PMINFO 0x3E, 0x01);
	
	for (i = 0; i < 0x3E; i++) {
		LR(i);
	}
	cve2_set_reg(PMINFO 0x3E, 0x00);
}

static int matroxfb_g450_compute(void* md, struct my_timming* mt) {
	MINFO_FROM(md);

	dprintk(KERN_DEBUG "Computing, mode=%u\n", ACCESS_FBINFO(outputs[1]).mode);

	if (mt->crtc == MATROXFB_SRC_CRTC2 &&
	    ACCESS_FBINFO(outputs[1]).mode != MATROXFB_OUTPUT_MODE_MONITOR) {
		const struct output_desc* outd;

		cve2_init_TVdata(ACCESS_FBINFO(outputs[1]).mode, &ACCESS_FBINFO(hw).maven, &outd);
		computeRegs(PMINFO &ACCESS_FBINFO(hw).maven, mt, outd);
	} else if (mt->mnp < 0) {
		/* We must program clocks before CRTC2, otherwise interlaced mode
		   startup may fail */
		mt->mnp = matroxfb_g450_setclk(PMINFO mt->pixclock, (mt->crtc == MATROXFB_SRC_CRTC1) ? M_PIXEL_PLL_C : M_VIDEO_PLL);
		mt->pixclock = g450_mnp2f(PMINFO mt->mnp);
	}
	dprintk(KERN_DEBUG "Pixclock = %u\n", mt->pixclock);
	return 0;
}

static int matroxfb_g450_program(void* md) {
	MINFO_FROM(md);
	
	if (ACCESS_FBINFO(outputs[1]).mode != MATROXFB_OUTPUT_MODE_MONITOR) {
		cve2_init_TV(PMINFO &ACCESS_FBINFO(hw).maven);
	}
	return 0;
}

static int matroxfb_g450_verify_mode(void* md, u_int32_t arg) {
	switch (arg) {
		case MATROXFB_OUTPUT_MODE_PAL:
		case MATROXFB_OUTPUT_MODE_NTSC:
		case MATROXFB_OUTPUT_MODE_MONITOR:
			return 0;
	}
	return -EINVAL;
}

static int g450_dvi_compute(void* md, struct my_timming* mt) {
	MINFO_FROM(md);

	if (mt->mnp < 0) {
		mt->mnp = matroxfb_g450_setclk(PMINFO mt->pixclock, (mt->crtc == MATROXFB_SRC_CRTC1) ? M_PIXEL_PLL_C : M_VIDEO_PLL);
		mt->pixclock = g450_mnp2f(PMINFO mt->mnp);
	}
	return 0;
}

static struct matrox_altout matroxfb_g450_altout = {
	.name		= "Secondary output",
	.compute	= matroxfb_g450_compute,
	.program	= matroxfb_g450_program,
	.verifymode	= matroxfb_g450_verify_mode,
};

static struct matrox_altout matroxfb_g450_dvi = {
	.name		= "DVI output",
	.compute	= g450_dvi_compute,
};

void matroxfb_g450_connect(WPMINFO2) {
	if (ACCESS_FBINFO(devflags.g450dac)) {
		down_write(&ACCESS_FBINFO(altout.lock));
		ACCESS_FBINFO(outputs[1]).src = MATROXFB_SRC_CRTC1;
		ACCESS_FBINFO(outputs[1]).data = MINFO;
		ACCESS_FBINFO(outputs[1]).output = &matroxfb_g450_altout;
		ACCESS_FBINFO(outputs[1]).mode = MATROXFB_OUTPUT_MODE_MONITOR;
		ACCESS_FBINFO(outputs[2]).src = MATROXFB_SRC_CRTC1;
		ACCESS_FBINFO(outputs[2]).data = MINFO;
		ACCESS_FBINFO(outputs[2]).output = &matroxfb_g450_dvi;
		ACCESS_FBINFO(outputs[2]).mode = MATROXFB_OUTPUT_MODE_MONITOR;
		up_write(&ACCESS_FBINFO(altout.lock));
	}
}

void matroxfb_g450_shutdown(WPMINFO2) {
	if (ACCESS_FBINFO(devflags.g450dac)) {
		down_write(&ACCESS_FBINFO(altout.lock));
		ACCESS_FBINFO(outputs[1]).src = MATROXFB_SRC_NONE;
		ACCESS_FBINFO(outputs[1]).output = NULL;
		ACCESS_FBINFO(outputs[1]).data = NULL;
		ACCESS_FBINFO(outputs[1]).mode = MATROXFB_OUTPUT_MODE_MONITOR;
		ACCESS_FBINFO(outputs[2]).src = MATROXFB_SRC_NONE;
		ACCESS_FBINFO(outputs[2]).output = NULL;
		ACCESS_FBINFO(outputs[2]).data = NULL;
		ACCESS_FBINFO(outputs[2]).mode = MATROXFB_OUTPUT_MODE_MONITOR;
		up_write(&ACCESS_FBINFO(altout.lock));
	}
}

EXPORT_SYMBOL(matroxfb_g450_connect);
EXPORT_SYMBOL(matroxfb_g450_shutdown);

MODULE_AUTHOR("(c) 2000-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G450/G550 output driver");
MODULE_LICENSE("GPL");
