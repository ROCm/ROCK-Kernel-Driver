/*
 *  drivers/s390/char/hwc_con.c
 *    HWC line mode console driver
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <peschke@fh-brandenburg.de>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/console.h>
#include <linux/fs.h>
#include <linux/init.h>

#include "hwc_rw.h"

extern void hwc_tty_init (void);

#ifdef CONFIG_HWC_CONSOLE

#define  hwc_console_major 4
#define  hwc_console_minor 0
#define  hwc_console_name  "console"

void hwc_console_write (struct console *, const char *, unsigned int);
kdev_t hwc_console_device (struct console *);

#define  HWC_CON_PRINT_HEADER "hwc console driver: "

struct console hwc_console =
{

	name:		hwc_console_name,
	write:		hwc_console_write,
	device:		hwc_console_device,
	flags:		CON_PRINTBUFFER,
};

void 
hwc_console_write (
			  struct console *console,
			  const char *message,
			  unsigned int count)
{

	if (console->device (console) != hwc_console.device (&hwc_console)) {

		hwc_printk (KERN_WARNING HWC_CON_PRINT_HEADER
			    "hwc_console_write() called with wrong "
			    "device number");
		return;
	}
	hwc_write (0, message, count);
}

kdev_t 
hwc_console_device (struct console * c)
{
	return MKDEV (hwc_console_major, hwc_console_minor);
}

#endif

void __init 
hwc_console_init (void)
{

#ifdef CONFIG_3215
	if (MACHINE_IS_VM)
		return;
#endif
	if (MACHINE_IS_P390)
		return;

	if (hwc_init () == 0) {

#ifdef CONFIG_HWC_CONSOLE

		register_console (&hwc_console);
#endif

		hwc_tty_init ();
	} else
		panic (HWC_CON_PRINT_HEADER "hwc initialisation failed !");

	return;
}
