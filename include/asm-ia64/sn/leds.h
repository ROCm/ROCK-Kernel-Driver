#ifndef _ASM_IA64_SN_LEDS_H
#define _ASM_IA64_SN_LEDS_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * Copyright (C) 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/config.h>
#include <asm/smp.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pda.h>

#ifdef CONFIG_IA64_SGI_SN1
#define LED0		0xc0000b00100000c0LL	/* ZZZ fixme */
#define LED_CPU_SHIFT	3
#else
#include <asm/sn/sn2/shub.h>
#define LED0		(LOCAL_MMR_ADDR(SH_REAL_JUNK_BUS_LED0))
#define LED_CPU_SHIFT	16
#endif

#define LED_CPU_HEARTBEAT	0x01
#define LED_CPU_ACTIVITY	0x02
#define LED_MASK_AUTOTEST	0xfe

/*
 * Basic macros for flashing the LEDS on an SGI, SN1.
 */

static __inline__ void
set_led_bits(u8 value, u8 mask)
{
	pda.led_state = (pda.led_state & ~mask) | (value & mask);
	*pda.led_address = (long) pda.led_state;
}

#endif /* _ASM_IA64_SN_LEDS_H */

