/*
 *  include/asm-i386/mach-pc9800/mach_timer.h
 *
 *  Machine specific calibrate_tsc() for PC-9800.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
/* ------ Calibrate the TSC ------- 
 * PC-9800:
 *  CTC cannot be used because some models (especially
 *  note-machines) may disable clock to speaker channel (#1)
 *  unless speaker is enabled.  We use ARTIC instead.
 */
#ifndef _MACH_TIMER_H
#define _MACH_TIMER_H

#define CALIBRATE_LATCH	(5 * 307200/HZ) /* 0.050sec * 307200Hz = 15360 */

static inline void mach_prepare_counter(void)
{
	/* ARTIC can't be stopped nor reset. So we wait roundup. */
	while (inw(0x5c));
}

static inline void mach_countup(unsigned long *count)
{
	do {
		*count = inw(0x5c);
	} while (*count < CALIBRATE_LATCH);
}

#endif /* !_MACH_TIMER_H */
