/*
 * Platform dependent support for Intel SoftSDV simulator.
 *
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Vijay Chander <vijay@engr.sgi.com>
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

#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/system.h>

/*
 * This is here so we can use the CMOS detection in ide-probe.c to
 * determine what drives are present.  In theory, we don't need this
 * as the auto-detection could be done via ide-probe.c:do_probe() but
 * in practice that would be much slower, which is painful when
 * running in the simulator.  Note that passing zeroes in DRIVE_INFO
 * is sufficient (the IDE driver will autodetect the drive geometry).
 */
char drive_info[4*16];

unsigned char aux_device_present = 0xaa;	/* XXX remove this when legacy I/O is gone */

void __init
dig_setup (char **cmdline_p)
{
	unsigned int orig_x, orig_y, num_cols, num_rows, font_height;

	/*
	 * Default to /dev/sda2.  This assumes that the EFI partition
	 * is physical disk 1 partition 1 and the Linux root disk is
	 * physical disk 1 partition 2.
	 */
	ROOT_DEV = to_kdev_t(0x0802);		/* default to second partition on first drive */

#ifdef	CONFIG_IA64_SOFTSDV_HACKS
	ROOT_DEV = to_kdev_t(0x0302);		/* 2nd partion on 1st IDE */
#endif /* CONFIG_IA64_SOFTSDV_HACKS */

#ifdef CONFIG_SMP
	init_smp_config();
#endif

	memset(&screen_info, 0, sizeof(screen_info));

	if (!ia64_boot_param.console_info.num_rows
	    || !ia64_boot_param.console_info.num_cols)
	{
		printk("dig_setup: warning: invalid screen-info, guessing 80x25\n");
		orig_x = 0;
		orig_y = 0;
		num_cols = 80;
		num_rows = 25;
		font_height = 16;
	} else {
		orig_x = ia64_boot_param.console_info.orig_x;
		orig_y = ia64_boot_param.console_info.orig_y;
		num_cols = ia64_boot_param.console_info.num_cols;
		num_rows = ia64_boot_param.console_info.num_rows;
		font_height = 400 / num_rows;
	}

	screen_info.orig_x = orig_x;
	screen_info.orig_y = orig_y;
	screen_info.orig_video_cols  = num_cols;
	screen_info.orig_video_lines = num_rows;
	screen_info.orig_video_points = font_height;
	screen_info.orig_video_mode = 3;	/* XXX fake */
	screen_info.orig_video_isVGA = 1;	/* XXX fake */
	screen_info.orig_video_ega_bx = 3;	/* XXX fake */
}

void
dig_irq_init (void)
{
	/*
	 * Disable the compatibility mode interrupts (8259 style), needs IN/OUT support
	 * enabled.
	 */
	outb(0xff, 0xA1);
	outb(0xff, 0x21);
}
