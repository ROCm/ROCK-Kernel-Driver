/*
 * linux/include/asm-arm/mach/time.h
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_MACH_TIME_H
#define __ASM_ARM_MACH_TIME_H

#include <linux/sysdev.h>

/*
 * This is our kernel timer structure.
 */
struct sys_timer {
	struct sys_device	dev;
	void			(*init)(void);
	void			(*suspend)(void);
	void			(*resume)(void);
	unsigned long		(*offset)(void);
};

extern struct sys_timer *system_timer;

extern int (*set_rtc)(void);

extern void timer_tick(struct pt_regs *);

extern void save_time_delta(struct timespec *delta, struct timespec *rtc);
extern void restore_time_delta(struct timespec *delta, struct timespec *rtc);

#endif
