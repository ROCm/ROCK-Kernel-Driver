/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2001 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.62 2001/11/29
 *
 * See matroxfb_base.c for contributors.
 *
 */

#include "matroxfb_g450.h"
#include "matroxfb_misc.h"
#include "matroxfb_DAC1064.h"
#include "g450_pll.h"
#include <linux/matroxfb.h>
#include <asm/uaccess.h>

static int matroxfb_g450_compute(void* md, struct my_timming* mt) {
	MINFO_FROM(md);

	if (mt->mnp < 0) {
		/* We must program clocks before CRTC2, otherwise interlaced mode
		   startup may fail */
		mt->mnp = matroxfb_g450_setclk(PMINFO mt->pixclock, (mt->crtc == MATROXFB_SRC_CRTC1) ? M_PIXEL_PLL_C : M_VIDEO_PLL);
		mt->pixclock = g450_mnp2f(PMINFO mt->mnp);
	}
	return 0;
}

static int matroxfb_g450_program(void* md) {
	return 0;
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
