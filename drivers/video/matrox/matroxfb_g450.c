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
#define m2info ((struct matroxfb_g450_info*)md)
#define minfo (m2info->primary_dev)
	ACCESS_FBINFO(hw).vidclk = mt->pixclock;
#undef minfo
#undef m2info
	return 0;
}

static int matroxfb_g450_program(void* md) {
#define m2info ((struct matroxfb_g450_info*)md)
#define minfo (m2info->primary_dev)
	matroxfb_g450_setclk(PMINFO ACCESS_FBINFO(hw).vidclk, M_VIDEO_PLL);
#undef minfo
#undef m2info	
	return 0;
}

static int matroxfb_g450_start(void* md) {
	return 0;
}

static void matroxfb_g450_incuse(void* md) {
	MOD_INC_USE_COUNT;
}

static void matroxfb_g450_decuse(void* md) {
	MOD_DEC_USE_COUNT;
}

static int matroxfb_g450_set_mode(void* md, u_int32_t arg) {
	if (arg == MATROXFB_OUTPUT_MODE_MONITOR) {
		return 1;
	}
	return -EINVAL;
}

static int matroxfb_g450_get_mode(void* md, u_int32_t* arg) {
	*arg = MATROXFB_OUTPUT_MODE_MONITOR;
	return 0;
}

static struct matrox_altout matroxfb_g450_altout = {
	matroxfb_g450_compute,
	matroxfb_g450_program,
	matroxfb_g450_start,
	matroxfb_g450_incuse,
	matroxfb_g450_decuse,
	matroxfb_g450_set_mode,
	matroxfb_g450_get_mode
};

void matroxfb_g450_connect(WPMINFO2) {
	struct matroxfb_g450_info* m2info;
	
	/* hardware is not G450... */
	if (!ACCESS_FBINFO(devflags.g450dac))
		return;
	m2info = (struct matroxfb_g450_info*)kmalloc(sizeof(*m2info), GFP_KERNEL);
	if (!m2info) {
		printk(KERN_ERR "matroxfb_g450: Not enough memory for G450 DAC control structs\n");
		return;
	}
	memset(m2info, 0, sizeof(*m2info));
	down_write(&ACCESS_FBINFO(altout.lock));
	m2info->primary_dev = MINFO;
	ACCESS_FBINFO(altout.device) = m2info;
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
	kfree(ACCESS_FBINFO(altout.device));
	ACCESS_FBINFO(altout.device) = NULL;
	ACCESS_FBINFO(altout.output) = NULL;
	up_write(&ACCESS_FBINFO(altout.lock));
}

EXPORT_SYMBOL(matroxfb_g450_connect);
EXPORT_SYMBOL(matroxfb_g450_shutdown);

MODULE_AUTHOR("(c) 2000-2001 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G450 secondary output driver");
MODULE_LICENSE("GPL");
