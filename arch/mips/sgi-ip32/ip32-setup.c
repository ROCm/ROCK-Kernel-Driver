/*
 * IP32 basic setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/param.h>
#include <linux/init.h>

#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/mmu_context.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>
#include <asm/sgialib.h>
#include <asm/traps.h>

extern struct rtc_ops ip32_rtc_ops;
extern u32 cc_interval;

#ifdef CONFIG_SGI_O2MACE_ETH

/*
 * This is taken care of in here 'cause they say using Arc later on is
 * problematic
 */
extern char o2meth_eaddr[8];
static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0; /* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if(*str == ':')
			str++;
		num = str2hexnum(*str++) << 4;
		num |= (str2hexnum(*str++));
		ea[i] = num;
	}
}
#endif

extern void ip32_time_init(void);
extern void ip32_be_init(void);                                                
extern void __init ip32_timer_setup (struct irqaction *irq);                   
extern void __init crime_init (void);                                          


void __init ip32_setup(void)
{
#ifdef CONFIG_SERIAL_CONSOLE
	char *ctype;
#endif
	TLBMISS_HANDLER_SETUP ();

	mips_io_port_base = UNCACHEDADDR(MACEPCI_HI_IO);;

#ifdef CONFIG_SERIAL_CONSOLE
	ctype = ArcGetEnvironmentVariable("console");
	if (*ctype == 'd') {
		if (ctype[1] == '2')
			console_setup ("ttyS1");
		else
			console_setup ("ttyS0");
	}
#endif
#ifdef CONFIG_SGI_O2MACE_ETH
	{
		char *mac=ArcGetEnvironmentVariable("eaddr");
		str2eaddr(o2meth_eaddr, mac);
	}
#endif

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif

	rtc_ops = &ip32_rtc_ops;
	board_be_init = ip32_be_init;
	board_time_init = ip32_time_init;
	board_timer_setup = ip32_timer_setup;

	crime_init();
}
