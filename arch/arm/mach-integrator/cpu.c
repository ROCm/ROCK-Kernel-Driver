/*
 *  linux/arch/arm/mach-integrator/cpu.c
 *
 *  Copyright (C) 2001-2002 Deep Blue Solutions Ltd.
 *
 *  $Id: cpu.c,v 1.6 2002/07/18 13:58:51 rmk Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * CPU support functions
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>

#define CM_ID  	(IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_ID_OFFSET)
#define CM_OSC	(IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_OSC_OFFSET)
#define CM_STAT (IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_STAT_OFFSET)
#define CM_LOCK (IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_LOCK_OFFSET)

struct vco {
	unsigned char vdw;
	unsigned char od;
};

/*
 * Divisors for each OD setting.
 */
static unsigned char cc_divisor[8] = { 10, 2, 8, 4, 5, 7, 9, 6 };

static unsigned int vco_to_freq(struct vco vco, int factor)
{
	return 2000 * (vco.vdw + 8) / cc_divisor[vco.od] / factor;
}

#ifdef CONFIG_CPU_FREQ
/*
 * Divisor indexes in ascending divisor order
 */
static unsigned char s2od[] = { 1, 3, 4, 7, 5, 2, 6, 0 };

static struct vco freq_to_vco(unsigned int freq_khz, int factor)
{
	struct vco vco = {0, 0};
	unsigned int i, f;

	freq_khz *= factor;

	for (i = 0; i < 8; i++) {
		f = freq_khz * cc_divisor[s2od[i]];
		/* f must be between 10MHz and 320MHz */
		if (f > 10000 && f <= 320000)
			break;
	}

	vco.od  = s2od[i];
	vco.vdw = f / 2000 - 8;

	return vco;
}

/*
 * Validate the speed in khz.  If it is outside our
 * range, then return the lowest.
 */
static unsigned int
integrator_validatespeed(unsigned int cpu, unsigned int freq_khz)
{
	struct vco vco;

	if (freq_khz < 12000)
		freq_khz = 12000;
	if (freq_khz > 160000)
		freq_khz = 160000;

	vco = freq_to_vco(freq_khz, 1);

	if (vco.vdw < 4 || vco.vdw > 152)
		return -EINVAL;

	return vco_to_freq(vco, 1);
}

static void integrator_setspeed(unsigned int cpu, unsigned int freq_khz)
{
	struct vco vco = freq_to_vco(freq_khz, 1);
	unsigned long cpus_allowed;
	u_int cm_osc;

	/*
	 * Save this threads cpus_allowed mask, and bind to the
	 * specified CPU.  When this call returns, we should be
	 * running on the right CPU.
	 */
	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, 1 << cpu);
	BUG_ON(cpu != smp_processor_id());

	cm_osc = __raw_readl(CM_OSC);
	cm_osc &= 0xfffff800;
	cm_osc |= vco.vdw | vco.od << 8;

	__raw_writel(0xa05f, CM_LOCK);
	__raw_writel(cm_osc, CM_OSC);
	__raw_writel(0, CM_LOCK);

	/*
	 * Restore the CPUs allowed mask.
	 */
	set_cpus_allowed(current, cpus_allowed);
}

static struct cpufreq_driver integrator_driver = {
	.validate	= integrator_validatespeed,
	.setspeed	= integrator_setspeed,
	.sync		= 1,
};
#endif

static int __init integrator_cpu_init(void)
{
	struct cpufreq_freqs *freqs;
	unsigned long cpus_allowed;
	int cpu;

	freqs = kmalloc(sizeof(struct cpufreq_freqs) * NR_CPUS,
			GFP_KERNEL);
	if (!freqs) {
		printk(KERN_ERR "CPU: unable to allocate cpufreqs structure\n");
		return -ENOMEM;
	}

	cpus_allowed = current->cpus_allowed;
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		u_int cm_osc, cm_stat, mem_freq_khz;
		struct vco vco;

		if (!cpu_online(cpu))
			continue;

		set_cpus_allowed(current, 1 << cpu);
		BUG_ON(cpu != smp_processor_id());

		cm_stat = __raw_readl(CM_STAT);
		cm_osc = __raw_readl(CM_OSC);
		vco.od  = (cm_osc >> 20) & 7;
		vco.vdw = (cm_osc >> 12) & 255;
		mem_freq_khz = vco_to_freq(vco, 2);

		printk(KERN_INFO "CPU%d: Module id: %d\n", cpu, cm_stat & 255);
		printk(KERN_INFO "CPU%d: Memory clock = %d.%03d MHz\n",
			cpu, mem_freq_khz / 1000, mem_freq_khz % 1000);

		vco.od = (cm_osc >> 8) & 7;
		vco.vdw = cm_osc & 255;

		freqs[cpu].min = 12000;
		freqs[cpu].max = 160000;
		freqs[cpu].cur = vco_to_freq(vco, 1);
	}

	set_cpus_allowed(current, cpus_allowed);

#ifdef CONFIG_CPU_FREQ
	integrator_driver.freq = freqs;
	cpufreq_register(&integrator_driver);
#else
	kfree(freqs);
#endif

	return 0;
}

core_initcall(integrator_cpu_init);
