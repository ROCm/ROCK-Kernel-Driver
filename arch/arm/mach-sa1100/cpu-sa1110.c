/*
 *  linux/arch/arm/mach-sa1100/cpu-sa1110.c
 *
 *  Copyright (C) 2001 Russell King
 *
 *  $Id: cpu-sa1110.c,v 1.5 2001/09/10 13:25:58 rmk Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Note: there are two erratas that apply to the SA1110 here:
 *  7 - SDRAM auto-power-up failure (rev A0)
 * 13 - Corruption of internal register reads/writes following
 *      SDRAM reads (rev A0, B0, B1)
 *
 * We ignore rev. A0 and B0 devices; I don't think they're worth supporting.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/system.h>

#undef DEBUG

extern u_int processor_id;

#define CPU_REVISION	(processor_id & 15)
#define CPU_SA1110_A0	(0)
#define CPU_SA1110_B0	(4)
#define CPU_SA1110_B1	(5)
#define CPU_SA1110_B2	(6)
#define CPU_SA1110_B4	(8)

struct sdram_params {
	u_char  rows;		/* bits				 */
	u_char  cas_latency;	/* cycles			 */
	u_char  tck;		/* clock cycle time (ns)	 */
	u_char  trcd;		/* activate to r/w (ns)		 */
	u_char  trp;		/* precharge to activate (ns)	 */
	u_char  twr;		/* write recovery time (ns)	 */
	u_short refresh;	/* refresh time for array (us)	 */
};

static struct sdram_params tc59sm716_cl2_params __initdata = {
	rows:		    12,
	tck:		    10,
	trcd:		    20,
	trp:		    20,
	twr:		    10,
	refresh:	 64000,
	cas_latency:	     2,
};

static struct sdram_params tc59sm716_cl3_params __initdata = {
	rows:		    12,
	tck:		     8,
	trcd:		    20,
	trp:		    20,
	twr:		     8,
	refresh:	 64000,
	cas_latency:	     3,
};

static struct sdram_params sdram_params;

/*
 * Given a period in ns and frequency in khz, calculate the number of
 * cycles of frequency in period.  Note that we round up to the next
 * cycle, even if we are only slightly over.
 */
static inline u_int ns_to_cycles(u_int ns, u_int khz)
{
	return (ns * khz + 999999) / 1000000;
}

/*
 * Create the MDCAS register bit pattern.
 */
static inline void set_mdcas(u_int *mdcas, int delayed, u_int rcd)
{
	u_int shift;

	rcd = 2 * rcd - 1;
	shift = delayed + 1 + rcd;

	mdcas[0]  = (1 << rcd) - 1;
	mdcas[0] |= 0x55555555 << shift;
	mdcas[1]  = mdcas[2] = 0x55555555 << (shift & 1);
}

static void sdram_update_timing(u_int cpu_khz, struct sdram_params *sdram)
{
	u_int mdcnfg, mdrefr, mdcas[3], mem_khz, sd_khz, trp, twr;
	unsigned long flags;

	mem_khz = cpu_khz / 2;
	sd_khz = mem_khz;

	/*
	 * If SDCLK would invalidate the SDRAM timings,
	 * run SDCLK at half speed.
	 *
	 * CPU steppings prior to B2 must either run the memory at
	 * half speed or use delayed read latching (errata 13).
	 */
	if ((ns_to_cycles(sdram->tck, sd_khz) > 1) ||
	    (CPU_REVISION < CPU_SA1110_B2 && sd_khz < 62000))
		sd_khz /= 2;

	mdcnfg = MDCNFG & 0x007f007f;

	twr = ns_to_cycles(sdram->twr, mem_khz);

	/* trp should always be >1 */
	trp = ns_to_cycles(sdram->trp, mem_khz) - 1;
	if (trp < 1)
		trp = 1;

	mdcnfg |= trp << 8;
	mdcnfg |= trp << 24;
	mdcnfg |= sdram->cas_latency << 12;
	mdcnfg |= sdram->cas_latency << 28;
	mdcnfg |= twr << 14;
	mdcnfg |= twr << 30;

	mdrefr = MDREFR & 0xffbffff0;
	mdrefr |= 7;

	if (sd_khz != mem_khz)
		mdrefr |= MDREFR_K1DB2;

	/* initial number of '1's in MDCAS + 1 */
	set_mdcas(mdcas, sd_khz >= 62000, ns_to_cycles(sdram->trcd, mem_khz));

#ifdef DEBUG
	mdelay(250);
	printk("MDCNFG: %08x MDREFR: %08x MDCAS0: %08x MDCAS1: %08x MDCAS2: %08x\n",
		mdcnfg, mdrefr, mdcas[0], mdcas[1], mdcas[2]);
#endif

	/*
	 * Reprogram the DRAM timings with interrupts disabled, and
	 * ensure that we are doing this within a complete cache line.
	 * This means that we won't access SDRAM for the duration of
	 * the programming.
	 */
	local_irq_save(flags);
	asm("mcr p15, 0, %0, c10, c4" : : "r" (0));
	udelay(10);
	__asm__("
		b	1f
		.align	5
1:		str	%3, [%1, #28]		@ MDREFR
		str	%4, [%1, #4]		@ MDCAS0
		str	%5, [%1, #8]		@ MDCAS1
		str	%6, [%1, #12]		@ MDCAS2
		str	%2, [%1, #0]		@ MDCNFG
		ldr	%0, [%1, #0]
		nop
		nop"
		: "=&r" (mdcnfg)
		: "r" (io_p2v(_MDCNFG)),
		  "0" (mdcnfg), "r" (mdrefr),
		  "r" (mdcas[0]), "r" (mdcas[1]), "r" (mdcas[2]));
	local_irq_restore(flags);
}

/*
 * Set the SDRAM refresh rate.
 */
static inline void sdram_set_refresh(u_int dri)
{
	MDREFR = (MDREFR & 0xffff000f) | (dri << 4);
	(void) MDREFR;
}

/*
 * Update the refresh period.  We do this such that we always refresh
 * the SDRAMs within their permissible period.  The refresh period is
 * always a multiple of the memory clock (fixed at cpu_clock / 2).
 *
 * FIXME: we don't currently take account of burst accesses here,
 * but neither do Intels DM nor Angel.
 */
static void
sdram_update_refresh(u_int cpu_khz, struct sdram_params *sdram)
{
	u_int ns_row = (sdram->refresh * 1000) >> sdram->rows;
	u_int dri = ns_to_cycles(ns_row, cpu_khz / 2) / 32;

#ifdef DEBUG
	mdelay(250);
	printk("new dri value = %d\n", dri);
#endif

	sdram_set_refresh(dri);
}

static int
sdram_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_info *ci = data;
	struct sdram_params *sdram = &sdram_params;

	/* were we initialised? */
	if (sdram->cas_latency == 0) {
		struct cpufreq_minmax *m = data;
		m->min_freq = m->max_freq = m->cur_freq;
		return 0;
	}

	switch (val) {
	case CPUFREQ_MINMAX:
		/*
		 * until we work out why the assabet
		 * crashes below 147.5MHz...
		 */
		cpufreq_updateminmax(data, 147500, -1);
		break;

	case CPUFREQ_PRECHANGE:
		/*
		 * The clock could be going away for some time.
		 * Set the SDRAMs to refresh rapidly (every 64
		 * memory clock cycles).  To get through the
		 * whole array, we need to wait 262144 mclk cycles.
		 * We wait 20ms to be safe.
		 */
		sdram_set_refresh(2);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(20 * HZ / 1000);

		if (ci->old_freq < ci->new_freq)
			sdram_update_timing(ci->new_freq, sdram);
		break;

	case CPUFREQ_POSTCHANGE:
		if (ci->old_freq > ci->new_freq)
			sdram_update_timing(ci->new_freq, sdram);
		sdram_update_refresh(ci->new_freq, sdram);
		break;
	}
	return 0;
}

static struct notifier_block sa1110_clkchg_block = {
	notifier_call:	sdram_notifier,
};

static int __init sa1110_sdram_init(void)
{
	struct sdram_params *sdram = NULL;
	unsigned int cur_freq = cpufreq_get(smp_processor_id());

	if (machine_is_assabet())
		sdram = &tc59sm716_cl3_params;

	if (sdram) {
		printk(KERN_DEBUG "SDRAM: tck: %d trcd: %d trp: %d"
			" twr: %d refresh: %d cas_latency: %d\n",
			sdram->tck, sdram->trcd, sdram->trp,
			sdram->twr, sdram->refresh, sdram->cas_latency);

		memcpy(&sdram_params, sdram, sizeof(sdram_params));

		sdram_update_timing(cur_freq, &sdram_params);
		sdram_update_refresh(cur_freq, &sdram_params);
	}

	return cpufreq_register_notifier(&sa1110_clkchg_block);
}

__initcall(sa1110_sdram_init);
