/* $Id: ip22-setup.c,v 1.4 1999/10/08 21:07:51 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI IP22 specific setup.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998, 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silcon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/console.h>
#include <linux/sched.h>
#include <linux/mc146818rtc.h>
#include <linux/pc_keyb.h>
#include <linux/tty.h>

#include <asm/addrspace.h>
#include <asm/mmu_context.h>
#include <asm/bcache.h>
#include <asm/keyboard.h>
#include <asm/irq.h>
#include <asm/sgialib.h>
#include <asm/sgi/sgi.h>
#include <asm/sgi/sgimc.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>

extern struct rtc_ops indy_rtc_ops;
extern void ip22_reboot_setup(void);
extern void ip22_volume_set(unsigned char);

#define sgi_kh ((struct hpc_keyb *) (KSEG1 + 0x1fbd9800 + 64))

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static void ip22_request_region(void)
{
	/* No I/O ports are being used on the Indy.  */
}

static int ip22_request_irq(void (*handler)(int, void *, struct pt_regs *))
{
	/* Dirty hack, this get's called as a callback from the keyboard
	   driver.  We piggyback the initialization of the front panel
	   button handling on it even though they're technically not
	   related with the keyboard driver in any way.  Doing it from
	   indy_setup wouldn't work since kmalloc isn't initialized yet.  */
	ip22_reboot_setup();

	return request_irq(SGI_KEYBOARD_IRQ, handler, 0, "keyboard", NULL);
}

static int ip22_aux_request_irq(void (*handler)(int, void *, struct pt_regs *))
{
	/* Nothing to do, interrupt is shared with the keyboard hw  */
	return 0;
}

static void ip22_aux_free_irq(void)
{
	/* Nothing to do, interrupt is shared with the keyboard hw  */
}

static unsigned char ip22_read_input(void)
{
	return sgi_kh->data;
}

static void ip22_write_output(unsigned char val)
{
	int status;

	do {
		status = sgi_kh->command;
	} while (status & KBD_STAT_IBF);
	sgi_kh->data = val;
}

static void ip22_write_command(unsigned char val)
{
	int status;

	do {
		status = sgi_kh->command;
	} while (status & KBD_STAT_IBF);
	sgi_kh->command = val;
}

static unsigned char ip22_read_status(void)
{
	return sgi_kh->command;
}

struct kbd_ops sgi_kbd_ops = {
	ip22_request_region,
	ip22_request_irq,

	ip22_aux_request_irq,
	ip22_aux_free_irq,

	ip22_read_input,
	ip22_write_output,
	ip22_write_command,
	ip22_read_status
};

int __init page_is_ram(unsigned long pagenr)
{
	if ((pagenr<<PAGE_SHIFT) < 0x2000UL)
		return 1;
	if ((pagenr<<PAGE_SHIFT) > 0x08002000)
		return 1;
	return 0;
}

void __init ip22_setup(void)
{
#ifdef CONFIG_SERIAL_CONSOLE
	char *ctype;
#endif
	TLBMISS_HANDLER_SETUP();

	/* Init the INDY HPC I/O controller.  Need to call this before
	 * fucking with the memory controller because it needs to know the
	 * boardID and whether this is a Guiness or a FullHouse machine.
	 */
	sgihpc_init();

	/* Init INDY memory controller. */
	sgimc_init();

	/* Now enable boardcaches, if any. */
	indy_sc_init();

#ifdef CONFIG_SERIAL_CONSOLE
	/* ARCS console environment variable is set to "g?" for
	 * graphics console, it is set to "d" for the first serial
	 * line and "d2" for the second serial line.
	 */
	ctype = ArcArcGetEnvironmentVariable("console");
	if(*ctype == 'd') {
		if(*(ctype+1)=='2')
			console_setup ("ttyS1");
		else
			console_setup ("ttyS0");
	}
#endif
#ifdef CONFIG_ARC_CONSOLE
	console_setup("ttyS0");
#endif

	ip22_volume_set(simple_strtoul(ArcGetEnvironmentVariable("volume"),
	                              NULL, 10));

#ifdef CONFIG_VT
#ifdef CONFIG_SGI_NEWPORT_CONSOLE
	conswitchp = &newport_con;

	screen_info = (struct screen_info) {
		0, 0,		/* orig-x, orig-y */
		0,		/* unused */
		0,		/* orig_video_page */
		0,		/* orig_video_mode */
		160,		/* orig_video_cols */
		0, 0, 0,	/* unused, ega_bx, unused */
		64,		/* orig_video_lines */
		0,		/* orig_video_isVGA */
		16		/* orig_video_points */
	};
#else
	conswitchp = &dummy_con;
#endif
#endif
	rtc_ops = &indy_rtc_ops;
	kbd_ops = &sgi_kbd_ops;
#ifdef CONFIG_PSMOUSE
	aux_device_present = 0xaa;
#endif
#ifdef CONFIG_VIDEO_VINO
	init_vino();
#endif
}
