/*
 *  (C) 2001-2003  Dave Jones. <davej@codemonkey.org.uk>
 *  (C) 2002  Padraig Brady. <padraig@antefacto.com>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by VIA.
 *
 *  VIA have currently 3 different versions of Longhaul.
 *
 *  +---------------------+----------+---------------------------------+
 *  | Marketing name      | Codename | longhaul version / features.    |
 *  +---------------------+----------+---------------------------------+
 *  |  Samuel/CyrixIII    |   C5A    | v1 : multipliers only           |
 *  |  Samuel2/C3         | C3E/C5B  | v1 : multiplier only            |
 *  |  Ezra               |   C5C    | v2 : multipliers & voltage      |
 *  |  Ezra-T             | C5M/C5N  | v3 : multipliers, voltage & FSB |
 *  +---------------------+----------+---------------------------------+
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>

#include "longhaul.h"

#define DEBUG

#ifdef DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

#define PFX "longhaul: "

static unsigned int numscales=16, numvscales;
static int minvid, maxvid;
static int can_scale_voltage;
static int vrmrev;


/* Module parameters */
static int dont_scale_voltage;
static unsigned int fsb;

#define __hlt()     __asm__ __volatile__("hlt": : :"memory")

/*
 * Clock ratio tables.
 * The eblcr ones specify the ratio read from the CPU.
 * The clock_ratio ones specify what to write to the CPU.
 */

/* VIA C3 Samuel 1  & Samuel 2 (stepping 0)*/
static int __initdata longhaul1_clock_ratio[16] = {
	-1, /* 0000 -> RESERVED */
	30, /* 0001 ->  3.0x */
	40, /* 0010 ->  4.0x */
	-1, /* 0011 -> RESERVED */
	-1, /* 0100 -> RESERVED */
	35, /* 0101 ->  3.5x */
	45, /* 0110 ->  4.5x */
	55, /* 0111 ->  5.5x */
	60, /* 1000 ->  6.0x */
	70, /* 1001 ->  7.0x */
	80, /* 1010 ->  8.0x */
	50, /* 1011 ->  5.0x */
	65, /* 1100 ->  6.5x */
	75, /* 1101 ->  7.5x */
	-1, /* 1110 -> RESERVED */
	-1, /* 1111 -> RESERVED */
};

static int __initdata samuel1_eblcr[16] = {
	50, /* 0000 -> RESERVED */
	30, /* 0001 ->  3.0x */
	40, /* 0010 ->  4.0x */
	-1, /* 0011 -> RESERVED */
	55, /* 0100 ->  5.5x */
	35, /* 0101 ->  3.5x */
	45, /* 0110 ->  4.5x */
	-1, /* 0111 -> RESERVED */
	-1, /* 1000 -> RESERVED */
	70, /* 1001 ->  7.0x */
	80, /* 1010 ->  8.0x */
	60, /* 1011 ->  6.0x */
	-1, /* 1100 -> RESERVED */
	75, /* 1101 ->  7.5x */
	-1, /* 1110 -> RESERVED */
	65, /* 1111 ->  6.5x */
};

/* VIA C3 Samuel2 Stepping 1->15 & VIA C3 Ezra */
static int __initdata longhaul2_clock_ratio[16] = {
	100, /* 0000 -> 10.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	90,  /* 0011 ->  9.0x */
	95,  /* 0100 ->  9.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	55,  /* 0111 ->  5.5x */
	60,  /* 1000 ->  6.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	50,  /* 1011 ->  5.0x */
	65,  /* 1100 ->  6.5x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	120, /* 1111 -> 12.0x */
};

static int __initdata samuel2_eblcr[16] = {
	50,  /* 0000 ->  5.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	100, /* 0011 -> 10.0x */
	55,  /* 0100 ->  5.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	110, /* 0111 -> 11.0x */
	90,  /* 1000 ->  9.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	60,  /* 1011 ->  6.0x */
	120, /* 1100 -> 12.0x */
	75,  /* 1101 ->  7.5x */
	130, /* 1110 -> 13.0x */
	65,  /* 1111 ->  6.5x */
};

static int __initdata ezra_eblcr[16] = {
	50,  /* 0000 ->  5.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	100, /* 0011 -> 10.0x */
	55,  /* 0100 ->  5.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	95,  /* 0111 ->  9.5x */
	90,  /* 1000 ->  9.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	60,  /* 1011 ->  6.0x */
	120, /* 1100 -> 12.0x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	65,  /* 1111 ->  6.5x */
};

/* VIA C5M. */
static int __initdata longhaul3_clock_ratio[32] = {
	100, /* 0000 -> 10.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	90,  /* 0011 ->  9.0x */
	95,  /* 0100 ->  9.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	55,  /* 0111 ->  5.5x */
	60,  /* 1000 ->  6.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	50,  /* 1011 ->  5.0x */
	65,  /* 1100 ->  6.5x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	120, /* 1111 ->  12.0x */

	-1,  /* 0000 -> RESERVED (10.0x) */
	110, /* 0001 -> 11.0x */
	120, /* 0010 -> 12.0x */
	-1,  /* 0011 -> RESERVED (9.0x)*/
	105, /* 0100 -> 10.5x */
	115, /* 0101 -> 11.5x */
	125, /* 0110 -> 12.5x */
	135, /* 0111 -> 13.5x */
	140, /* 1000 -> 14.0x */
	150, /* 1001 -> 15.0x */
	160, /* 1010 -> 16.0x */
	130, /* 1011 -> 13.0x */
	145, /* 1100 -> 14.5x */
	155, /* 1101 -> 15.5x */
	-1,  /* 1110 -> RESERVED (13.0x) */
	-1,  /* 1111 -> RESERVED (12.0x) */
};

static int __initdata c5m_eblcr[32] = {
	50,  /* 0000 ->  5.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	100, /* 0011 -> 10.0x */
	55,  /* 0100 ->  5.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	95,  /* 0111 ->  9.5x */
	90,  /* 1000 ->  9.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	60,  /* 1011 ->  6.0x */
	120, /* 1100 -> 12.0x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	65,  /* 1111 ->  6.5x */

	-1,  /* 0000 -> RESERVED (9.0x) */
	110, /* 0001 -> 11.0x */
	120, /* 0010 -> 12.0x */
	-1,  /* 0011 -> RESERVED (10.0x)*/
	135, /* 0100 -> 13.5x */
	115, /* 0101 -> 11.5x */
	125, /* 0110 -> 12.5x */
	105, /* 0111 -> 10.5x */
	130, /* 1000 -> 13.0x */
	150, /* 1001 -> 15.0x */
	160, /* 1010 -> 16.0x */
	140, /* 1011 -> 14.0x */
	-1,  /* 1100 -> RESERVED (12.0x) */
	155, /* 1101 -> 15.5x */
	-1,  /* 1110 -> RESERVED (13.0x) */
	145, /* 1111 -> 14.5x */
};

/* Voltage scales. Div by 1000 to get actual voltage. */
static int __initdata vrm85scales[32] = {
	1250, 1200, 1150, 1100, 1050, 1800, 1750, 1700,
	1650, 1600, 1550, 1500, 1450, 1400, 1350, 1300,
	1275, 1225, 1175, 1125, 1075, 1825, 1775, 1725,
	1675, 1625, 1575, 1525, 1475, 1425, 1375, 1325,
};

static int __initdata mobilevrmscales[32] = {
	2000, 1950, 1900, 1850, 1800, 1750, 1700, 1650,
	1600, 1550, 1500, 1450, 1500, 1350, 1300, -1,
	1275, 1250, 1225, 1200, 1175, 1150, 1125, 1100,
	1075, 1050, 1025, 1000, 975, 950, 925, -1,
};

/* Clock ratios multiplied by 10 */
static int clock_ratio[32];
static int eblcr_table[32];
static int voltage_table[32];
static unsigned int highest_speed, lowest_speed; /* kHz */
static int longhaul_version;
static struct cpufreq_frequency_table *longhaul_table;


static unsigned int calc_speed (int mult, int fsb)
{
	return ((mult/10)*fsb) + ((mult%10)*(fsb/2));
}


static unsigned int longhaul_get_cpu_fsb (void)
{
	unsigned long lo, hi;
	unsigned int eblcr_fsb_table[] = { 66, 133, 100, -1 };
	unsigned int invalue=0;

	if (fsb == 0) {
		rdmsr (MSR_IA32_EBL_CR_POWERON, lo, hi);
		invalue = (lo & (1<<18|1<<19)) >>18;
		fsb = eblcr_fsb_table[invalue];
	}
	return fsb;
}


static int longhaul_get_cpu_mult (void)
{
	unsigned long invalue=0,lo, hi;

	rdmsr (MSR_IA32_EBL_CR_POWERON, lo, hi);
	invalue = (lo & (1<<22|1<<23|1<<24|1<<25)) >>22;
	if (longhaul_version==3) {
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
	int vidindex, i, speed, mult;
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

	dprintk (KERN_INFO PFX "FSB:%d Mult:%d.%dx\n", fsb,
				mult/10, mult%10);

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

	case 2:
		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		longhaul.bits.SoftBusRatio = clock_ratio_index & 0xf;
		longhaul.bits.SoftBusRatio4 = (clock_ratio_index & 0x10) >> 4;
		longhaul.bits.EnableSoftBusRatio = 1;
		/* We must program the revision key only with values we
		 * know about, not blindly copy it from 0:3 */
		longhaul.bits.RevisionKey = 1;

		if (can_scale_voltage) {
			/* PB: TODO fix this up */
			vidindex = (((highest_speed-lowest_speed) / (fsb/2)) -
					((highest_speed-((clock_ratio[clock_ratio_index] * fsb * 100)/1000)) / (fsb/2)));
			for (i=0;i<32;i++) {
				dprintk (KERN_INFO "VID hunting. Looking for %d, found %d\n",
						minvid+(vidindex*25), voltage_table[i]);
				if (voltage_table[i]==(minvid + (vidindex * 25)))
					break;
			}
			if (i==32)
				goto bad_voltage;

			dprintk (KERN_INFO PFX "Desired vid index=%d\n", i);
#if 0
			longhaul.bits.SoftVID = i;
			longhaul.bits.EnableSoftVID = 1;
#endif
		}
/* FIXME: Do voltage and freq seperatly like we do in powernow-k7 */
bad_voltage:
		wrmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		__hlt();

		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		longhaul.bits.EnableSoftBusRatio = 0;
		if (can_scale_voltage)
			longhaul.bits.EnableSoftVID = 0;
		longhaul.bits.RevisionKey = 1;
		wrmsrl (MSR_VIA_LONGHAUL, longhaul.val);
		break;

	case 3:
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
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
}


static int __init longhaul_get_ranges (void)
{
	unsigned long invalue;
	unsigned int minmult=0, maxmult=0;
	unsigned int multipliers[32]= {
		50,30,40,100,55,35,45,95,90,70,80,60,120,75,85,65,
		-1,110,120,-1,135,115,125,105,130,150,160,140,-1,155,-1,145 };
	unsigned int j, k = 0;
	union msr_longhaul longhaul;

	fsb = longhaul_get_cpu_fsb();

	switch (longhaul_version) {
	case 1:
		/* Ugh, Longhaul v1 didn't have the min/max MSRs.
		   Assume min=3.0x & max = whatever we booted at. */
		minmult = 30;
		maxmult = longhaul_get_cpu_mult();
		break;

	case 2 ... 3:
		rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);

		invalue = longhaul.bits.MaxMHzBR;
		if (longhaul.bits.MaxMHzBR4)
			invalue += 16;
		maxmult=multipliers[invalue];

#if 0
		invalue = longhaul.bits.MinMHzBR;
		if (longhaul.bits.MinMHzBR4);
			invalue += 16;
		minmult = multipliers[invalue];
#else
		minmult = 30; /* as per spec */
#endif
		break;
	}

	dprintk (KERN_INFO PFX "MinMult=%d.%dx MaxMult=%d.%dx\n",
		 minmult/10, minmult%10, maxmult/10, maxmult%10);
	highest_speed = calc_speed (maxmult, fsb);
	lowest_speed = calc_speed (minmult,fsb);
	dprintk (KERN_INFO PFX "Lowestspeed=%dMHz Highestspeed=%dMHz\n",
		 lowest_speed, highest_speed);

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
		longhaul_table[k].index	= (j << 8);
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

static int longhaul_cpu_init (struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = cpu_data;
	int ret;

	switch (c->x86_model) {
	case 6:		/* VIA C3 Samuel C5A */
		longhaul_version=1;
		memcpy (clock_ratio, longhaul1_clock_ratio, sizeof(longhaul1_clock_ratio));
		memcpy (eblcr_table, samuel1_eblcr, sizeof(samuel1_eblcr));
		break;

	case 7:		/* C5B / C5C */
		switch (c->x86_mask) {
		case 0:
			longhaul_version=1;
			memcpy (clock_ratio, longhaul1_clock_ratio, sizeof(longhaul1_clock_ratio));
			memcpy (eblcr_table, samuel2_eblcr, sizeof(samuel2_eblcr));
			break;
		case 1 ... 15:
			longhaul_version=2;
			memcpy (clock_ratio, longhaul2_clock_ratio, sizeof(longhaul2_clock_ratio));
			memcpy (eblcr_table, ezra_eblcr, sizeof(ezra_eblcr));
			break;
		}
		break;

	case 8:		/* C5M/C5N */
		return -ENODEV; // Waiting on updated docs from VIA before this is usable
		longhaul_version=3;
		numscales=32;
		memcpy (clock_ratio, longhaul3_clock_ratio, sizeof(longhaul3_clock_ratio));
		memcpy (eblcr_table, c5m_eblcr, sizeof(c5m_eblcr));
		break;
	}

	printk (KERN_INFO PFX "VIA CPU detected. Longhaul version %d supported\n",
					longhaul_version);

	if ((longhaul_version==2 || longhaul_version==3) && (dont_scale_voltage==0))
		longhaul_setup_voltagescaling();

	ret = longhaul_get_ranges();
	if (ret != 0)
		return ret;

 	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
 	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = calc_speed (longhaul_get_cpu_mult(), fsb);

	return cpufreq_frequency_table_cpuinfo(policy, longhaul_table);
}

static struct cpufreq_driver longhaul_driver = {
	.verify 	= longhaul_verify,
	.target 	= longhaul_target,
	.init		= longhaul_cpu_init,
	.name		= "longhaul",
	.owner		= THIS_MODULE,
};

static int __init longhaul_init (void)
{
	struct cpuinfo_x86 *c = cpu_data;

	if ((c->x86_vendor != X86_VENDOR_CENTAUR) || (c->x86 !=6) )
		return -ENODEV;

	switch (c->x86_model) {
	case 6 ... 7:
		return cpufreq_register_driver(&longhaul_driver);
	case 8:
		return -ENODEV;
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

MODULE_PARM (dont_scale_voltage, "i");

MODULE_AUTHOR ("Dave Jones <davej@codemonkey.org.uk>");
MODULE_DESCRIPTION ("Longhaul driver for VIA Cyrix processors.");
MODULE_LICENSE ("GPL");

module_init(longhaul_init);
module_exit(longhaul_exit);

