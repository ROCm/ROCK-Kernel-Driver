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
#define minfo ((struct matrox_fb_info*)md)
	ACCESS_FBINFO(hw).vidclk = mt->pixclock;
#undef minfo
	return 0;
}

static int matroxfb_g450_program(void* md) {
#define minfo ((struct matrox_fb_info*)md)
	matroxfb_g450_setclk(PMINFO ACCESS_FBINFO(hw).vidclk, M_VIDEO_PLL);
#undef minfo
	return 0;
}

static struct matrox_altout matroxfb_g450_altout = {
	.compute	= matroxfb_g450_compute,
	.program	= matroxfb_g450_program,
};

void matroxfb_g450_connect(WPMINFO2) {
	/* hardware is not G450... */
	if (!ACCESS_FBINFO(devflags.g450dac))
		return;
	down_write(&ACCESS_FBINFO(altout.lock));
	ACCESS_FBINFO(altout.device) = MINFO;
	ACCESS_FBINFO(altout.output) = &matroxfb_g450_altout;
	up_write(&ACCESS_FBINFO(altout.lock));
	ACCESS_FBINFO(output.all) |= MATROXFB_OUTPUT_CONN_SECONDARY;
	matroxfb_switch(ACCESS_FBINFO(fbcon.currcon), (struct fb_info*)MINFO);
}

void matroxfb_g450_shutdown(WPMINFO2) {
	ACCESS_FBINFO(output.all) &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
	ACCESS_FBINFO(output.ph) &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
	ACCESS_FBINFO(output.sh) &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
	down_write(&ACCESS_FBINFO(altout.lock));
	ACCESS_FBINFO(altout.device) = NULL;
	ACCESS_FBINFO(altout.output) = NULL;
	up_write(&ACCESS_FBINFO(altout.lock));
}

EXPORT_SYMBOL(matroxfb_g450_connect);
EXPORT_SYMBOL(matroxfb_g450_shutdown);

MODULE_AUTHOR("(c) 2000-2001 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G450 secondary output driver");
MODULE_LICENSE("GPL");
