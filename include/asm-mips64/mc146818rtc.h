/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Machine dependent access functions for RTC registers.
 *
 * Copyright (C) 1996, 1997, 1998 Ralf Baechle
 */
#ifndef _ASM_MC146818RTC_H
#define _ASM_MC146818RTC_H

#include <asm/io.h>

#ifndef RTC_PORT
#define RTC_PORT(x)	(0x70 + (x))
#endif

/*
 * The yet supported machines all access the RTC index register via
 * an ISA port access but the way to access the date register differs ...
 */
#define CMOS_READ(addr) ({ \
rtc_ops->rtc_read_data(addr); \
})
#define CMOS_WRITE(val, addr) ({ \
rtc_ops->rtc_write_data(val, addr); \
})
#define RTC_ALWAYS_BCD \
rtc_ops->rtc_bcd_mode()

/*
 * This structure defines how to access various features of
 * different machine types and how to access them.
 */
struct rtc_ops {
	/* How to access the RTC register in a DS1287.  */
	unsigned char (*rtc_read_data)(unsigned long addr);
	void (*rtc_write_data)(unsigned char data, unsigned long addr);
	int (*rtc_bcd_mode)(void);
};

extern struct rtc_ops *rtc_ops;

#define RTC_IRQ 8

#endif /* _ASM_MC146818RTC_H */
