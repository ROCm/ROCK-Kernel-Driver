/*
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander(vijay@engr.sgi.com)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/sn/mmzone_sn1.h>
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * The format of "screen_info" is strange, and due to early i386-setup
 * code. This is just enough to make the console code think we're on a
 * VGA color display.
 */
struct screen_info sn1_screen_info = {
	orig_x:			 0,
	orig_y:			 0,
	orig_video_mode:	 3,
	orig_video_cols:	80,
	orig_video_ega_bx:	 3,
	orig_video_lines:	25,
	orig_video_isVGA:	 1,
	orig_video_points:	16
};

/*
 * This is here so we can use the CMOS detection in ide-probe.c to
 * determine what drives are present.  In theory, we don't need this
 * as the auto-detection could be done via ide-probe.c:do_probe() but
 * in practice that would be much slower, which is painful when
 * running in the simulator.  Note that passing zeroes in DRIVE_INFO
 * is sufficient (the IDE driver will autodetect the drive geometry).
 */
char drive_info[4*16];

unsigned long
sn1_map_nr (unsigned long addr)
{
#ifdef CONFIG_DISCONTIGMEM
	return MAP_NR_SN1(addr);
#else
	return MAP_NR_DENSE(addr);
#endif
}

void __init
sn1_setup(char **cmdline_p)
{
	extern void init_sn1_smp_config(void);
	ROOT_DEV = to_kdev_t(0x0301);		/* default to first IDE drive */

	init_sn1_smp_config();
#ifdef ZZZ
#if !defined (CONFIG_IA64_SOFTSDV_HACKS)
        /*
         * Program the timer to deliver timer ticks.  0x40 is the I/O port
         * address of PIT counter 0, 0x43 is the I/O port address of the
         * PIT control word.
         */
        request_region(0x40,0x20,"timer");
        outb(0x34, 0x43);            /* Control word */
        outb(LATCH & 0xff , 0x40);   /* LSB */
        outb(LATCH >> 8, 0x40);      /* MSB */
        printk("PIT: LATCH at 0x%x%x for %d HZ\n", LATCH >> 8, LATCH & 0xff, HZ);
#endif
#endif
#ifdef CONFIG_SMP
	init_smp_config();
#endif
	screen_info = sn1_screen_info;
}

int
IS_RUNNING_ON_SIMULATOR(void)
{
#ifdef CONFIG_IA64_SGI_SN1_SIM
	long sn;
	asm("mov %0=cpuid[%1]" : "=r"(sn) : "r"(2));
	return(sn == SNMAGIC);
#else
	return(0);
#endif
}
