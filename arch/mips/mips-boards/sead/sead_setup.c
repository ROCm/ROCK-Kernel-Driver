/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2002 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * SEAD specific setup.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mc146818rtc.h>
#include <linux/ioport.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>
#include <asm/mips-boards/seadint.h>
#include <asm/time.h>

#if defined(CONFIG_SERIAL_CONSOLE) || defined(CONFIG_PROM_CONSOLE)
extern void console_setup(char *, int *);
char serial_console[20];
#endif

extern void mips_reboot_setup(void);
extern void mips_time_init(void);
extern void mips_timer_setup(struct irqaction *irq);

const char *get_system_type(void)
{
	return "MIPS SEAD";
}

void __init sead_setup(void)
{
	char *argptr;

	ioport_resource.end = 0x7fffffff;

#ifdef CONFIG_SERIAL_CONSOLE
	argptr = prom_getcmdline();
	if ((argptr = strstr(argptr, "console=ttyS0")) == NULL) {
		int i = 0;
		char *s = prom_getenv("modetty0");
		while(s[i] >= '0' && s[i] <= '9')
			i++;
		strcpy(serial_console, "ttyS0,");
		strncpy(serial_console + 6, s, i);
		prom_printf("Config serial console: %s\n", serial_console);
		console_setup(serial_console, NULL);
	}
#endif

	argptr = prom_getcmdline();

	if ((argptr = strstr(argptr, "nofpu")) != NULL)
		cpu_data[0].options &= ~MIPS_CPU_FPU;

	board_time_init = mips_time_init;
	board_timer_setup = mips_timer_setup;

	mips_reboot_setup();
}
