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

extern void (*init_arch_time)(void);

extern int (*set_rtc)(void);
extern unsigned long (*gettimeoffset)(void);

extern void timer_tick(struct pt_regs *);

extern void save_time_delta(struct timespec *delta, struct timespec *rtc);
extern void restore_time_delta(struct timespec *delta, struct timespec *rtc);

#endif
