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

static int matroxfb_g450_connect(struct matroxfb_g450_info* m2info) {
	MINFO_FROM(m2info->primary_dev);

	down_write(&ACCESS_FBINFO(altout.lock));
	ACCESS_FBINFO(altout.device) = m2info;
	ACCESS_FBINFO(altout.output) = &matroxfb_g450_altout;
	up_write(&ACCESS_FBINFO(altout.lock));
	ACCESS_FBINFO(output.all) |= MATROXFB_OUTPUT_CONN_SECONDARY;
	matroxfb_switch(ACCESS_FBINFO(currcon), (struct fb_info*)MINFO);	
	return 0;
}

static void matroxfb_g450_shutdown(struct matroxfb_g450_info* m2info) {
	MINFO_FROM(m2info->primary_dev);
	
	if (MINFO) {
		ACCESS_FBINFO(output.all) &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
		ACCESS_FBINFO(output.ph) &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
		ACCESS_FBINFO(output.sh) &= ~MATROXFB_OUTPUT_CONN_SECONDARY;
		down_write(&ACCESS_FBINFO(altout.lock));
		ACCESS_FBINFO(altout.device) = NULL;
		ACCESS_FBINFO(altout.output) = NULL;
		up_write(&ACCESS_FBINFO(altout.lock));
		m2info->primary_dev = NULL;
	}
}

/* we do not have __setup() yet */
static void* matroxfb_g450_probe(struct matrox_fb_info* minfo) {
	struct matroxfb_g450_info* m2info;

	/* hardware is not G450... */
	if (!ACCESS_FBINFO(devflags.g450dac))
		return NULL;
	m2info = (struct matroxfb_g450_info*)kmalloc(sizeof(*m2info), GFP_KERNEL);
	if (!m2info) {
		printk(KERN_ERR "matroxfb_g450: Not enough memory for G450 DAC control structs\n");
		return NULL;
	}
	memset(m2info, 0, sizeof(*m2info));
	m2info->primary_dev = MINFO;
	if (matroxfb_g450_connect(m2info)) {
		kfree(m2info);
		printk(KERN_ERR "matroxfb_g450: G450 DAC failed to initialize\n");
		return NULL;
	}
	return m2info;
}

static void matroxfb_g450_remove(struct matrox_fb_info* minfo, void* g450) {
	matroxfb_g450_shutdown(g450);
	kfree(g450);
}

static struct matroxfb_driver g450 = {
		name:	"Matrox G450 output #2",
		probe:	matroxfb_g450_probe,
		remove:	matroxfb_g450_remove };

static int matroxfb_g450_init(void) {
	matroxfb_register_driver(&g450);
	return 0;
}

static void matroxfb_g450_exit(void) {
	matroxfb_unregister_driver(&g450);
}

MODULE_AUTHOR("(c) 2000-2001 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G450 secondary output driver");
MODULE_LICENSE("GPL");
module_init(matroxfb_g450_init);
module_exit(matroxfb_g450_exit);
