/*
 *  arch/i386/mach-pc9800/mach_reboot.h
 *
 *  Machine specific reboot functions for PC-9800.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _MACH_REBOOT_H
#define _MACH_REBOOT_H

#ifdef CMOS_WRITE
#undef CMOS_WRITE
#define CMOS_WRITE(a,b)	do{}while(0)
#endif

static inline void mach_reboot(void)
{
	outb(0, 0xf0);		/* signal CPU reset */
	mdelay(1);
}

#endif /* !_MACH_REBOOT_H */
