/*
 *  (C) 2001-2004  Dave Jones. <davej@codemonkey.org.uk>
 *  (C) 2002  Padraig Brady. <padraig@antefacto.com>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by VIA.
 *
 *  VIA have currently 2 different versions of Longhaul.
 *  Version 1 (Longhaul) uses the BCR2 MSR at 0x1147.
 *   It is present only in Samuel 1, Samuel 2 and Ezra.
 *  Version 2 (Powersaver) uses the POWERSAVER MSR at 0x110a.
 *   It is present in Ezra-T, Nehemiah and above.
 *   In addition to scaling multiplier, it can also scale voltage.
 *   There is provision for scaling FSB too, but this doesn't work
 *   too well in practice.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>

#include "longhaul.h"

#define PFX "longhaul: "

static unsigned int numscales=16, numvscales;
static unsigned int fsb;
static int minvid, maxvid;
static int can_scale_voltage;
static int vrmrev;

/* Module parameters */
static int dont_scale_voltage;
static int debug;

static void dprintk(const char *msg, ...)
{
	if (debug == 1)
		printk(msg);
}


#define __hlt()     __asm__ __volatile__("hlt": : :"memory")

/* Clock ratios multiplied by 10 */
static int clock_ratio[32];
static int eblcr_table[32];
static int voltage_table[32];
static unsigned int highest_speed, lowest_speed; /* kHz */
static int longhaul_version;
static struct cpufreq_frequency_table *longhaul_table;


static unsigned int calc_speed (int mult, int fsb)
{
	int khz;
	khz = (mult/10)*fsb;
	if (mult%10)
		khz += fsb/2;
	khz *= 1000;
	return khz;
}


static int longhaul_get_cpu_mult (void)
{
	unsigned long invalue=0,lo, hi;

	rdmsr (MSR_IA32_EBL_CR_POWERON, lo, hi);
	invalue = (lo & (1<<22|1<<23|1<<24|1<<25)) >>22;
	if (longhaul_version==2 || longhaul_version==4) {
		if (lo & (1<<27))
			invalue+=16;
	}
	return eblcr_table[invalue];
}


/**
 * longhaul_set_cpu_frequency()
 * @clock_ratio_index : bitpattern of the new multiplier.
 *
 * Sets a new clock ratio, and -if applicable- a new Front Side Bus
 */

static void longhaul_setstate (unsigned int clock_ratio_index)
{
	int speed, mult;
	struct cpufreq_freqs freqs;
	union msr_longhaul longhaul;
	union msr_bcr2 bcr2;

	mult = clock_ratio[clock_ratio_index];
	if (mult == -1)
		return;

	speed = calc_speed (mult, fsb);
	if ((speed > highest_speed) || (speed < lowest_speed))
		return;

	freqs.old = calc_speed (longhaul_get_cpu_mult(), fsb);
	freqs.new = speed;
	freqs.cpu = 0; /* longhaul.c is UP only driver */

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	dprintk (KERN_INFO PFX "FSB:%d Mult:%d.%dx\n", fsb, mult/10, mult%10);

	switch (longhaul_version) {
	case 1:
		rdmsrl (MSR_VIA_BCR2, bcr2.val);
		/* Enable software clock multiplier */
		bcr2.bits.ESOFTBF = 1;
		bcr2.bits.CLOCKMUL = clock_ratio_index;
		wrmsrl (MSR_VIA_BCR2, bcr2.val);

		__hlt();

		/* Disable software clock multiplier */
		rdmsrl (MSR_VIA_BCR2, bcr2.val);
		bcr2.bits.ESOFTBF = 0;
		wrmsrl (MSR_VIA_BCR2, bcr2.val);
		break;

	/*
	 * Powersaver. (Ezra-T [C5M], Nehemiah [C5N])
	 * We can scale voltage with this too, but that's currently
	 * disabled until we come up with a decent 'match freq to voltage'
	 * algorithm.
	 * We also need to do the voltage/freq setting in order depending
	 * on the direction of scaling (like we do in powernow-k7.c)
	 * Ezra-T was alleged to do FSB scaling too, but it never worked in practice.
	 */
	case 2:
		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		longhaul.bits.SoftBusRatio = clock_ratio_index & 0xf;
		longhaul.bits.SoftBusRatio4 = (clock_ratio_index & 0x10) >> 4;
		longhaul.bits.EnableSoftBusRatio = 1;
		/* We must program the revision key only with values we
		 * know about, not blindly copy it from 0:3 */
		longhaul.bits.RevisionKey = 3;	/* SoftVID & SoftBSEL */
		wrmsrl(MSR_VIA_LONGHAUL, longhaul.val);
		__hlt();

		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		longhaul.bits.EnableSoftBusRatio = 0;
		longhaul.bits.RevisionKey = 3;
		wrmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		break;
	case 4:
		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		longhaul.bits.SoftBusRatio = clock_ratio_index & 0xf;
		longhaul.bits.SoftBusRatio4 = (clock_ratio_index & 0x10) >> 4;
		longhaul.bits.EnableSoftBusRatio = 1;

		longhaul.bits.RevisionKey = 0x0;

		wrmsrl(MSR_VIA_LONGHAUL, longhaul.val);
		__hlt();

		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		longhaul.bits.EnableSoftBusRatio = 0;
		longhaul.bits.RevisionKey = 0xf;
		wrmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		break;
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
}

/*
 * Centaur decided to make life a little more tricky.
 * Only longhaul v1 is allowed to read EBLCR BSEL[0:1].
 * Samuel2 and above have to try and guess what the FSB is.
 * We do this by assuming we booted at maximum multiplier, and interpolate
 * between that value multiplied by possible FSBs and cpu_mhz which
 * was calculated at boot time. Really ugly, but no other way to do this.
 */

#define ROUNDING	0xf

static int _guess (int guess, int maxmult)
{
	int target;

	target = ((maxmult/10)*guess);
	if (maxmult%10 != 0)
		target += (guess/2);
	target += ROUNDING/2;
	target &= ~ROUNDING;
	return target;
}


static int guess_fsb(int maxmult)
{
	int speed = (cpu_khz/1000);
	int i;
	int speeds[3] = { 66, 100, 133 };

	speed += ROUNDING/2;
	speed &= ~ROUNDING;

	for (i=0; i<3; i++) {
		if (_guess(speeds[i],maxmult) == speed)
			return speeds[i];
	}
	return 0;
}


static int __init longhaul_get_ranges (void)
{
	struct cpuinfo_x86 *c = cpu_data;
	unsigned long invalue;
	unsigned int minmult=0, maxmult=0;
	unsigned int multipliers[32]= {
		50,30,40,100,55,35,45,95,90,70,80,60,120,75,85,65,
		-1,110,120,-1,135,115,125,105,130,150,160,140,-1,155,-1,145 };
	unsigned int j, k = 0;
	union msr_longhaul longhaul;
	unsigned long lo, hi;
	unsigned int eblcr_fsb_table_v1[] = { 66, 133, 100, -1 };
	unsigned int eblcr_fsb_table_v2[] = { 133, 100, -1, 66 };

	switch (longhaul_version) {
	case 1:
		/* Ugh, Longhaul v1 didn't have the min/max MSRs.
		   Assume min=3.0x & max = whatever we booted at. */
		minmult = 30;
		maxmult = longhaul_get_cpu_mult();
		rdmsr (MSR_IA32_EBL_CR_POWERON, lo, hi);
		invalue = (lo & (1<<18|1<<19)) >>18;
		if (c->x86_model==6)
			fsb = eblcr_fsb_table_v1[invalue];
		else
			fsb = guess_fsb(maxmult);
		break;

	case 2:
		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);

		invalue = longhaul.bits.MaxMHzBR;
		if (longhaul.bits.MaxMHzBR4)
			invalue += 16;
		maxmult=multipliers[invalue];

		invalue = longhaul.bits.MinMHzBR;
		if (longhaul.bits.MinMHzBR4 == 1)
			minmult = 30;
		else
			minmult = multipliers[invalue];

		fsb = eblcr_fsb_table_v2[longhaul.bits.MaxMHzFSB];
		break;

	case 4:
		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);

		/*
		 * TODO: This code works, but raises a lot of questions.
		 * - Some Nehemiah's seem to have broken Min/MaxMHzBR's.
		 *   We get around this by using a hardcoded multiplier of 5.0x
		 *   for the minimimum speed, and the speed we booted up at for the max.
		 *   This is done in longhaul_get_cpu_mult() by reading the EBLCR register.
		 * - According to some VIA documentation EBLCR is only
		 *   in pre-Nehemiah C3s. How this still works is a mystery.
		 *   We're possibly using something undocumented and unsupported,
		 *   But it works, so we don't grumble.
		 */
		minmult=50;
		maxmult=longhaul_get_cpu_mult();

		fsb = eblcr_fsb_table_v2[longhaul.bits.MaxMHzFSB];
		break;
	}

	if (fsb == -1) {
		printk (KERN_INFO PFX "Invalid (reserved) FSB!\n");
		return -EINVAL;
	}

	dprintk (KERN_INFO PFX "MinMult=%d.%dx MaxMult=%d.%dx\n",
		 minmult/10, minmult%10, maxmult/10, maxmult%10);
	highest_speed = calc_speed (maxmult, fsb);
	lowest_speed = calc_speed (minmult,fsb);
	dprintk (KERN_INFO PFX "FSB: %dMHz Lowestspeed=%dMHz Highestspeed=%dMHz\n",
		 fsb, lowest_speed/1000, highest_speed/1000);

	if (lowest_speed == highest_speed) {
		printk (KERN_INFO PFX "highestspeed == lowest, aborting.\n");
		return -EINVAL;
	}
	if (lowest_speed > highest_speed) {
		printk (KERN_INFO PFX "nonsense! lowest (%d > %d) !\n",
			lowest_speed, highest_speed);
		return -EINVAL;
	}

	longhaul_table = kmalloc((numscales + 1) * sizeof(struct cpufreq_frequency_table), GFP_KERNEL);
	if(!longhaul_table)
		return -ENOMEM;

	for (j=0; j < numscales; j++) {
		unsigned int ratio;
		ratio = clock_ratio[j];
		if (ratio == -1)
			continue;
		if (ratio > maxmult || ratio < minmult)
			continue;
		longhaul_table[k].frequency = calc_speed (ratio, fsb);
		longhaul_table[k].index	= j;
		k++;
	}

	longhaul_table[k].frequency = CPUFREQ_TABLE_END;
	if (!k) {
		kfree (longhaul_table);
		return -EINVAL;
	}

	return 0;
}


static void __init longhaul_setup_voltagescaling(void)
{
	union msr_longhaul longhaul;

	rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);

	if (!(longhaul.bits.RevisionID & 1))
		return;

	minvid = longhaul.bits.MinimumVID;
	maxvid = longhaul.bits.MaximumVID;
	vrmrev = longhaul.bits.VRMRev;

	if (minvid == 0 || maxvid == 0) {
		printk (KERN_INFO PFX "Bogus values Min:%d.%03d Max:%d.%03d. "
					"Voltage scaling disabled.\n",
					minvid/1000, minvid%1000, maxvid/1000, maxvid%1000);
		return;
	}

	if (minvid == maxvid) {
		printk (KERN_INFO PFX "Claims to support voltage scaling but min & max are "
				"both %d.%03d. Voltage scaling disabled\n",
				maxvid/1000, maxvid%1000);
		return;
	}

	if (vrmrev==0) {
		dprintk (KERN_INFO PFX "VRM 8.5 : ");
		memcpy (voltage_table, vrm85scales, sizeof(voltage_table));
		numvscales = (voltage_table[maxvid]-voltage_table[minvid])/25;
	} else {
		dprintk (KERN_INFO PFX "Mobile VRM : ");
		memcpy (voltage_table, mobilevrmscales, sizeof(voltage_table));
		numvscales = (voltage_table[maxvid]-voltage_table[minvid])/5;
	}

	/* Current voltage isn't readable at first, so we need to
	   set it to a known value. The spec says to use maxvid */
	longhaul.bits.RevisionKey = longhaul.bits.RevisionID;	/* FIXME: This is bad. */
	longhaul.bits.EnableSoftVID = 1;
	longhaul.bits.SoftVID = maxvid;
	wrmsrl (MSR_VIA_LONGHAUL, longhaul.val);

	minvid = voltage_table[minvid];
	maxvid = voltage_table[maxvid];

	dprintk ("Min VID=%d.%03d Max VID=%d.%03d, %d possible voltage scales\n",
		maxvid/1000, maxvid%1000, minvid/1000, minvid%1000, numvscales);

	can_scale_voltage = 1;
}


static int longhaul_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, longhaul_table);
}


static int longhaul_target (struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int table_index = 0;
	unsigned int new_clock_ratio = 0;

	if (cpufreq_frequency_table_target(policy, longhaul_table, target_freq, relation, &table_index))
		return -EINVAL;

	new_clock_ratio = longhaul_table[table_index].index & 0xFF;

	longhaul_setstate(new_clock_ratio);

	return 0;
}

static unsigned int longhaul_get(unsigned int cpu)
{
	if (cpu)
		return 0;
	return (calc_speed (longhaul_get_cpu_mult(), fsb));
}

static int __init longhaul_cpu_init (struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = cpu_data;
	char *cpuname=NULL;
	int ret;

	switch (c->x86_model) {
	case 6:
		cpuname = "C3 'Samuel' [C5A]";
		longhaul_version=1;
		memcpy (clock_ratio, samuel1_clock_ratio, sizeof(samuel1_clock_ratio));
		memcpy (eblcr_table, samuel1_eblcr, sizeof(samuel1_eblcr));
		break;

	case 7:		/* C5B / C5C */
		longhaul_version=1;
		switch (c->x86_mask) {
		case 0:
			cpuname = "C3 'Samuel 2' [C5B]";
			/* Note, this is not a typo, early Samuel2's had Samuel1 ratios. */
			memcpy (clock_ratio, samuel1_clock_ratio, sizeof(samuel1_clock_ratio));
			memcpy (eblcr_table, samuel2_eblcr, sizeof(samuel2_eblcr));
			break;
		case 1 ... 15:
			if (c->x86_mask < 8)
				cpuname = "C3 'Samuel 2' [C5B]";
			else
				cpuname = "C3 'Ezra' [C5C]";
			memcpy (clock_ratio, ezra_clock_ratio, sizeof(ezra_clock_ratio));
			memcpy (eblcr_table, ezra_eblcr, sizeof(ezra_eblcr));
			break;
		}
		break;

	case 8:
		cpuname = "C3 'Ezra-T' [C5M]";
		longhaul_version=2;
		numscales=32;
		memcpy (clock_ratio, ezrat_clock_ratio, sizeof(ezrat_clock_ratio));
		memcpy (eblcr_table, ezrat_eblcr, sizeof(ezrat_eblcr));
		break;

	case 9:
		longhaul_version=4;
		numscales=32;
		switch (c->x86_mask) {
		case 0 ... 1:
			cpuname = "C3 'Nehemiah A' [C5N]";
			memcpy (clock_ratio, nehemiah_a_clock_ratio, sizeof(nehemiah_a_clock_ratio));
			memcpy (eblcr_table, nehemiah_a_eblcr, sizeof(nehemiah_a_eblcr));
			break;
		case 2 ... 4:
			cpuname = "C3 'Nehemiah B' [C5N]";
			memcpy (clock_ratio, nehemiah_b_clock_ratio, sizeof(nehemiah_b_clock_ratio));
			memcpy (eblcr_table, nehemiah_b_eblcr, sizeof(nehemiah_b_eblcr));
			break;
		case 5 ... 15:
			cpuname = "C3 'Nehemiah C' [C5N]";
			memcpy (clock_ratio, nehemiah_c_clock_ratio, sizeof(nehemiah_c_clock_ratio));
			memcpy (eblcr_table, nehemiah_c_eblcr, sizeof(nehemiah_c_eblcr));
			break;
		}
		break;

	default:
		cpuname = "Unknown";
		break;
	}

	printk (KERN_INFO PFX "VIA %s CPU detected. Longhaul v%d supported.\n",
					cpuname, longhaul_version);

	ret = longhaul_get_ranges();
	if (ret != 0)
		return ret;

	if ((longhaul_version==2) && (dont_scale_voltage==0))
		longhaul_setup_voltagescaling();

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = calc_speed (longhaul_get_cpu_mult(), fsb);

	ret = cpufreq_frequency_table_cpuinfo(policy, longhaul_table);
	if (ret)
		return ret;

	cpufreq_frequency_table_get_attr(longhaul_table, policy->cpu);

	return 0;
}

static int __devexit longhaul_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr* longhaul_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver longhaul_driver = {
	.verify	= longhaul_verify,
	.target	= longhaul_target,
	.get	= longhaul_get,
	.init	= longhaul_cpu_init,
	.exit	= __devexit_p(longhaul_cpu_exit),
	.name	= "longhaul",
	.owner	= THIS_MODULE,
	.attr	= longhaul_attr,
};

static int __init longhaul_init (void)
{
	struct cpuinfo_x86 *c = cpu_data;

	if (c->x86_vendor != X86_VENDOR_CENTAUR || c->x86 != 6)
		return -ENODEV;

	switch (c->x86_model) {
	case 6 ... 9:
		return cpufreq_register_driver(&longhaul_driver);
	default:
		printk (KERN_INFO PFX "Unknown VIA CPU. Contact davej@codemonkey.org.uk\n");
	}

	return -ENODEV;
}

static void __exit longhaul_exit (void)
{
	cpufreq_unregister_driver(&longhaul_driver);
	kfree(longhaul_table);
}

module_param (dont_scale_voltage, int, 0644);
MODULE_PARM_DESC(dont_scale_voltage, "Don't scale voltage of processor");

module_param (debug, int, 0644);
MODULE_PARM_DESC(debug, "Dump debugging information.");

MODULE_AUTHOR ("Dave Jones <davej@codemonkey.org.uk>");
MODULE_DESCRIPTION ("Longhaul driver for VIA Cyrix processors.");
MODULE_LICENSE ("GPL");

module_init(longhaul_init);
module_exit(longhaul_exit);

