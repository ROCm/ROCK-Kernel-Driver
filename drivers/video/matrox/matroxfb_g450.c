#include "matroxfb_g450.h"
#include "matroxfb_misc.h"
#include "matroxfb_DAC1064.h"
#include <linux/matroxfb.h>
#include <asm/uaccess.h>

static int matroxfb_g450_get_reg(WPMINFO int reg) {
	int val;

	matroxfb_DAC_lock();
	val = matroxfb_DAC_in(PMINFO reg);
	matroxfb_DAC_unlock();
	return val;
}

static int matroxfb_g450_set_reg(WPMINFO int reg, int val) {
	matroxfb_DAC_lock();
	matroxfb_DAC_out(PMINFO reg, val);
	matroxfb_DAC_unlock();
	return 0;
}

static const struct matrox_pll_features maven_pll = {
	110000,
	27000,
	4, 127,
	2, 31,
	3
};

static void DAC1064_calcclock(unsigned int freq, unsigned int fmax,
		unsigned int* in, unsigned int* feed, unsigned int* post) {
	unsigned int fvco;
	unsigned int p;

	fvco = matroxfb_PLL_calcclock(&maven_pll, freq, fmax, in, feed, &p);
	/* 0 => 100 ... 275 MHz
           1 => 243 ... 367 MHz
           2 => 320 ... 475 MHz
           3 => 453 ... 556 MHz
           4 => 540 ... 594 MHz
           5 => 588 ... 621 MHz
           6 => 626 ... 637 MHz
           7 => 631 ... 642 MHz

           As you can see, never choose frequency > 621 MHz, there is unavailable gap...
           Just to be sure, currently driver uses 110 ... 500 MHz range.
         */
	if (fvco <= 260000)
		;
	else if (fvco <= 350000)
		p |= 0x08;
	else if (fvco <= 460000)
		p |= 0x10;
	else if (fvco <= 550000)
		p |= 0x18;
	else if (fvco <= 590000)
		p |= 0x20;
	else
		p |= 0x28;
	*post = p;
	return;
}

static inline int matroxfb_g450_compute_timming(struct matroxfb_g450_info* m2info,
		struct my_timming* mt,
		struct mavenregs* m) {
	unsigned int a, b, c;

	DAC1064_calcclock(mt->pixclock, 500000, &a, &b, &c);
	m->regs[0x80] = a;
	m->regs[0x81] = b;
	m->regs[0x82] = c;
	printk(KERN_DEBUG "PLL: %02X %02X %02X\n", a, b, c);
	return 0;
}

static inline int matroxfb_g450_program_timming(struct matroxfb_g450_info* m2info, const struct mavenregs* m) {
	MINFO_FROM(m2info->primary_dev);

	matroxfb_g450_set_reg(PMINFO M1064_XPIXPLL2M, m->regs[0x81]);
	matroxfb_g450_set_reg(PMINFO M1064_XPIXPLL2N, m->regs[0x80]);
	matroxfb_g450_set_reg(PMINFO M1064_XPIXPLL2P, m->regs[0x82]);
	return 0;
}

/******************************************************/

static int matroxfb_g450_compute(void* md, struct my_timming* mt, struct matrox_hw_state* mr) {
	return matroxfb_g450_compute_timming(md, mt, &mr->maven);
}

static int matroxfb_g450_program(void* md, const struct matrox_hw_state* mr) {
	return matroxfb_g450_program_timming(md, &mr->maven);
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

	/* hardware is not G450 incapable... */
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

MODULE_AUTHOR("(c) 2000 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G450 secondary output driver");
module_init(matroxfb_g450_init);
module_exit(matroxfb_g450_exit);
