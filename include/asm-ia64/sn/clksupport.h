/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */


#ifndef  _ASM_KSYS_CLKSUPPORT_H
#define _ASM_KSYS_CLKSUPPORT_H

/* #include <sys/mips_addrspace.h> */

#if SN
#include <asm/sn/agent.h>
#include <asm/sn/intr_public.h>
typedef hubreg_t clkreg_t;
extern nasid_t master_nasid;

#define GET_LOCAL_RTC		(clkreg_t)LOCAL_HUB_L(PI_RT_COUNT)
#define DISABLE_TMO_INTR()	if  (cpuid_to_localslice(cpuid())) \
					REMOTE_HUB_PI_S(get_nasid(),\
						cputosubnode(cpuid()),\
						PI_RT_COMPARE_B, 0); \
				else \
					REMOTE_HUB_PI_S(get_nasid(),\
						cputosubnode(cpuid()),\
						PI_RT_COMPARE_A, 0);

/* This is a hack; we really need to figure these values out dynamically */
/* 
 * Since 800 ns works very well with various HUB frequencies, such as
 * 360, 380, 390 and 400 MHZ, we use 800 ns rtc cycle time.
 */
#define NSEC_PER_CYCLE		800
#define CYCLE_PER_SEC		(NSEC_PER_SEC/NSEC_PER_CYCLE)
/*
 * Number of cycles per profiling intr 
 */
#define CLK_FCLOCK_FAST_FREQ	1250
#define CLK_FCLOCK_SLOW_FREQ	0
/* The is the address that the user will use to mmap the cycle counter */
#define CLK_CYCLE_ADDRESS_FOR_USER LOCAL_HUB_ADDR(PI_RT_COUNT)

#elif IP30
#include <sys/cpu.h>
typedef heartreg_t clkreg_t;
#define NSEC_PER_CYCLE		80
#define CYCLE_PER_SEC		(NSEC_PER_SEC/NSEC_PER_CYCLE)
#define GET_LOCAL_RTC	*((volatile clkreg_t *)PHYS_TO_COMPATK1(HEART_COUNT))
#define DISABLE_TMO_INTR()
#define CLK_CYCLE_ADDRESS_FOR_USER PHYS_TO_K1(HEART_COUNT)
#define CLK_FCLOCK_SLOW_FREQ (CYCLE_PER_SEC / HZ)
#endif

/* Prototypes */
extern void init_timebase(void);
extern void fastick_maint(struct eframe_s *);
extern int audioclock;
extern int prfclk_enabled_cnt;
#endif  /* _ASM_KSYS_CLKSUPPORT_H */
