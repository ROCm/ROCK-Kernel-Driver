/*
 *  arch/ppc64/kernel/cputable.c
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 * 
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <asm/cputable.h>

struct cpu_spec* cur_cpu_spec = NULL;

extern void __setup_cpu_power3(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_power4(unsigned long offset, struct cpu_spec* spec);


/* We only set the altivec features if the kernel was compiled with altivec
 * support
 */
#ifdef CONFIG_ALTIVEC
#define CPU_FTR_ALTIVEC_COMP	CPU_FTR_ALTIVEC
#else
#define CPU_FTR_ALTIVEC_COMP	0
#endif

struct cpu_spec	cpu_specs[] = {
    {	/* Power3 */
	    0xffff0000, 0x00400000, "Power3 (630)",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
		    CPU_FTR_DABR | CPU_FTR_IABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power3,
	    COMMON_PPC64_FW
    },
    {	/* Power3+ */
	    0xffff0000, 0x00410000, "Power3 (630+)",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
		    CPU_FTR_DABR | CPU_FTR_IABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power3,
	    COMMON_PPC64_FW
    },
    {	/* Northstar */
	    0xffff0000, 0x00330000, "Northstar",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
		    CPU_FTR_DABR | CPU_FTR_IABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power3,
	    COMMON_PPC64_FW
    },
    {	/* Pulsar */
	    0xffff0000, 0x00340000, "Pulsar",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
		    CPU_FTR_DABR | CPU_FTR_IABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power3,
	    COMMON_PPC64_FW
    },
    {	/* I-star */
	    0xffff0000, 0x00360000, "I-star",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
		    CPU_FTR_DABR | CPU_FTR_IABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power3,
	    COMMON_PPC64_FW
    },
    {	/* S-star */
	    0xffff0000, 0x00370000, "S-star",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
		    CPU_FTR_DABR | CPU_FTR_IABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power3,
	    COMMON_PPC64_FW
    },
    {	/* Power4 */
	    0xffff0000, 0x00350000, "Power4",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_DABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power4,
	    COMMON_PPC64_FW
    },
    {	/* Power4+ */
	    0xffff0000, 0x00380000, "Power4+",
	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_DABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power4,
	    COMMON_PPC64_FW
    },
    {	/* default match */
	    0x00000000, 0x00000000, "(Power4-Compatible)",
  	    CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_DABR,
	    COMMON_USER_PPC64,
	    128, 128,
	    __setup_cpu_power4,
	    COMMON_PPC64_FW
    }
};

firmware_feature_t firmware_features_table[FIRMWARE_MAX_FEATURES] = {
    {FW_FEATURE_PFT,		"hcall-pft"},
    {FW_FEATURE_TCE,		"hcall-tce"},
    {FW_FEATURE_SPRG0,		"hcall-sprg0"},
    {FW_FEATURE_DABR,		"hcall-dabr"},
    {FW_FEATURE_COPY,		"hcall-copy"},
    {FW_FEATURE_ASR,		"hcall-asr"},
    {FW_FEATURE_DEBUG,		"hcall-debug"},
    {FW_FEATURE_PERF,		"hcall-perf"},
    {FW_FEATURE_DUMP,		"hcall-dump"},
    {FW_FEATURE_INTERRUPT,	"hcall-interrupt"},
    {FW_FEATURE_MIGRATE,	"hcall-migrate"},
};
