/*
 *  $Id: powernow-k7.c,v 1.34 2003/02/22 10:23:46 db Exp $
 *
 *  (C) 2003 Dave Jones <davej@suse.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by AMD.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 *
 * Errata 5: Processor may fail to execute a FID/VID change in presence of interrupt.
 * - We cli/sti on stepping A0 CPUs around the FID/VID transition.
 * Errata 15: Processors with half frequency multipliers may hang upon wakeup from disconnect.
 * - We disable half multipliers if ACPI is used on A0 stepping CPUs.
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

#include "powernow-k7.h"

#define DEBUG

#ifdef DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

#define PFX "powernow: "


struct psb_s {
	u8 signature[10];
	u8 tableversion;
	u8 flags;
	u16 settlingtime;
	u8 reserved1;
	u8 numpst;
};

struct pst_s {
	u32 cpuid;
	u8 fsbspeed;
	u8 maxfid;
	u8 startvid;
	u8 numpstates;
};


/* divide by 1000 to get VID. */
static int mobile_vid_table[32] = {
    2000, 1950, 1900, 1850, 1800, 1750, 1700, 1650,
    1600, 1550, 1500, 1450, 1400, 1350, 1300, 0,
    1275, 1250, 1225, 1200, 1175, 1150, 1125, 1100,
    1075, 1050, 1024, 1000, 975, 950, 925, 0,
};

/* divide by 10 to get FID. */
static int fid_codes[32] = {
    110, 115, 120, 125, 50, 55, 60, 65,
    70, 75, 80, 85, 90, 95, 100, 105, 
    30, 190, 40, 200, 130, 135, 140, 210,
    150, 225, 160, 165, 170, 180, -1, -1,
};

static struct cpufreq_frequency_table *powernow_table;

static unsigned int can_scale_bus;
static unsigned int can_scale_vid;
static unsigned int minimum_speed=-1;
static unsigned int maximum_speed;
static unsigned int number_scales;
static unsigned int fsb;
static unsigned int latency;
static char have_a0;


#ifndef rdmsrl
#define rdmsrl(msr,val) do {unsigned long l__,h__; \
	rdmsr (msr, l__, h__);	\
	val = l__;	\
	val |= ((u64)h__<<32);	\
} while(0)
#endif

#ifndef wrmsrl
static void wrmsrl (u32 msr, u64 val)
{
	u32 lo, hi;

	lo = (u32) val;
	hi = val >> 32;
	wrmsr (msr, lo, hi);
}
#endif



static int check_powernow(void)
{
	struct cpuinfo_x86 *c = cpu_data;
	unsigned int maxei, eax, ebx, ecx, edx;

	if (c->x86_vendor != X86_VENDOR_AMD) {
		printk (KERN_INFO PFX "AMD processor not detected.\n");
		return 0;
	}

	if (c->x86 !=6) {
		printk (KERN_INFO PFX "This module only works with AMD K7 CPUs\n");
		return 0;
	}

	printk (KERN_INFO PFX "AMD K7 CPU detected.\n");

	if ((c->x86_model == 6) && (c->x86_mask == 0)) {
		printk (KERN_INFO PFX "K7 660[A0] core detected, enabling errata workarounds\n");
		have_a0 = 1;
	}

	/* Get maximum capabilities */
	maxei = cpuid_eax (0x80000000);
	if (maxei < 0x80000007) {	/* Any powernow info ? */
		printk (KERN_INFO PFX "No powernow capabilities detected\n");
		return 0;
	}

	cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
	printk (KERN_INFO PFX "PowerNOW! Technology present. Can scale: ");

	if (edx & 1 << 1) {
		printk ("frequency");
		can_scale_bus=1;
	}

	if ((edx & (1 << 1 | 1 << 2)) == 0x6)
		printk (" and ");

	if (edx & 1 << 2) {
		printk ("voltage");
		can_scale_vid=1;
	}

	if (!(edx & (1 << 1 | 1 << 2))) {
		printk ("nothing.\n");
		return 0;
	}

	printk (".\n");
	return 1;
}


static int get_ranges (unsigned char *pst)
{
	unsigned int j, speed;
	u8 fid, vid;

	powernow_table = kmalloc((sizeof(struct cpufreq_frequency_table) * (number_scales + 1)), GFP_KERNEL);
	if (!powernow_table)
		return -ENOMEM;
	memset(powernow_table, 0, (sizeof(struct cpufreq_frequency_table) * (number_scales + 1)));

	for (j=0 ; j < number_scales; j++) {
		fid = *pst++;

		powernow_table[j].frequency = fsb * fid_codes[fid] * 100;
		powernow_table[j].index = fid; /* lower 8 bits */

		speed = fsb * (fid_codes[fid]/10);
		if ((fid_codes[fid] % 10)==5) {
			speed += fsb/2;
#if defined(CONFIG_ACPI_PROCESSOR) || defined(CONFIG_ACPI_PROCESSOR_MODULE)
			if (have_a0 == 1)
				powernow_table[j].frequency = CPUFREQ_ENTRY_INVALID;
#endif
		}

		dprintk (KERN_INFO PFX "   FID: 0x%x (%d.%dx [%dMHz])\t", fid,
			fid_codes[fid] / 10, fid_codes[fid] % 10, speed);

		if (speed < minimum_speed)
			minimum_speed = speed;
		if (speed > maximum_speed)
			maximum_speed = speed;

		vid = *pst++;
		powernow_table[j].index |= (vid << 8); /* upper 8 bits */
		dprintk ("VID: 0x%x (%d.%03dV)\n", vid,	mobile_vid_table[vid]/1000,
			mobile_vid_table[vid]%1000);
	}
	dprintk ("\n");

	powernow_table[number_scales].frequency = CPUFREQ_TABLE_END;
	powernow_table[number_scales].index = 0;

	return 0;
}


static void change_speed (unsigned int index)
{
	u8 fid, vid;
	struct cpufreq_freqs freqs;
	union msr_fidvidstatus fidvidstatus;
	union msr_fidvidctl fidvidctl;


	/* fid are the lower 8 bits of the index we stored into
	 * the cpufreq frequency table in powernow_decode_bios,
	 * vid are the upper 8 bits.
	 */

	fid = powernow_table[index].index & 0xFF;
	vid = (powernow_table[index].index & 0xFF00) >> 8;

	freqs.cpu = 0;

	rdmsrl (MSR_K7_FID_VID_STATUS, fidvidstatus.val);
	freqs.old = fsb * fid_codes[fidvidstatus.bits.CFID] * 100;

	freqs.new = powernow_table[index].frequency;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* Now do the magic poking into the MSRs.  */

	if (have_a0 == 1)	/* A0 errata 5 */
		__asm__("\tcli\n");

	/* First change the frequency. */
	if (fidvidctl.bits.FID != fid) {
		rdmsrl (MSR_K7_FID_VID_CTL, fidvidctl.val);
		fidvidctl.bits.SGTC = latency;	/* Stop grant timeout counter */
		fidvidctl.bits.FID = fid;
		fidvidctl.bits.FIDC = 1;
		wrmsrl (MSR_K7_FID_VID_CTL, fidvidctl.val);
	}

	/* Now change voltage. */
	if (fidvidctl.bits.VID != vid) {
		rdmsrl (MSR_K7_FID_VID_CTL, fidvidctl.val);
		fidvidctl.bits.VID = vid;
		fidvidctl.bits.VIDC = 1;
		wrmsrl (MSR_K7_FID_VID_CTL, fidvidctl.val);
	}

	if (have_a0 == 1)
		__asm__("\tsti\n");

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
}


static int powernow_decode_bios (int maxfid, int startvid)
{
	struct psb_s *psb;
	struct pst_s *pst;
	struct cpuinfo_x86 *c = cpu_data;
	unsigned int i, j;
	unsigned char *p;
	unsigned int etuple;
	unsigned int ret;

	etuple = cpuid_eax(0x80000001);
	etuple &= 0xf00;
	etuple |= (c->x86_model<<4)|(c->x86_mask);

	for (i=0xC0000; i < 0xffff0 ; i+=16) {

		p = phys_to_virt(i);

		if (memcmp(p, "AMDK7PNOW!",  10) == 0){
			dprintk (KERN_INFO PFX "Found PSB header at %p\n", p);
			psb = (struct psb_s *) p;
			dprintk (KERN_INFO PFX "Table version: 0x%x\n", psb->tableversion);
			if (psb->tableversion != 0x12) {
				printk (KERN_INFO PFX "Sorry, only v1.2 tables supported right now\n");
				return -ENODEV;
			}

			dprintk (KERN_INFO PFX "Flags: 0x%x (", psb->flags);
			if ((psb->flags & 1)==0) {
				dprintk ("Mobile");
			} else {
				dprintk ("Desktop");
			}
			dprintk (" voltage regulator)\n");

			latency = psb->settlingtime;
			dprintk (KERN_INFO PFX "Settling Time: %d microseconds.\n", psb->settlingtime);
			dprintk (KERN_INFO PFX "Has %d PST tables. (Only dumping ones relevant to this CPU).\n", psb->numpst);

			p += sizeof (struct psb_s);

			pst = (struct pst_s *) p;

			for (i = 0 ; i <psb->numpst; i++) {
				pst = (struct pst_s *) p;
				number_scales = pst->numpstates;

				if ((etuple == pst->cpuid) && (maxfid==pst->maxfid) && (startvid==pst->startvid))
				{
					dprintk (KERN_INFO PFX "PST:%d (@%p)\n", i, pst);
					dprintk (KERN_INFO PFX " cpuid: 0x%x\t", pst->cpuid);
					dprintk ("fsb: %d\t", pst->fsbspeed);
					dprintk ("maxFID: 0x%x\t", pst->maxfid);
					dprintk ("startvid: 0x%x\n", pst->startvid);

					fsb = pst->fsbspeed;
					ret = get_ranges ((char *) pst + sizeof (struct pst_s));
					return ret;

				} else {
					p = (char *) pst + sizeof (struct pst_s);
					for (j=0 ; j < number_scales; j++)
						p+=2;
				}
			}
			return -EINVAL;
		}
		p++;
	}

	return -ENODEV;
}


static int powernow_target (struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int newstate;

	if (cpufreq_frequency_table_target(policy, powernow_table, target_freq, relation, &newstate))
		return -EINVAL;

	change_speed(newstate);

	return 0;
}


static int powernow_verify (struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, powernow_table);
}


static int __init powernow_cpu_init (struct cpufreq_policy *policy)
{
	union msr_fidvidstatus fidvidstatus;
	int result;

	if (policy->cpu != 0)
		return -ENODEV;

	rdmsrl (MSR_K7_FID_VID_STATUS, fidvidstatus.val);

	result = powernow_decode_bios(fidvidstatus.bits.MFID, fidvidstatus.bits.SVID);
	if (result)
		return result;

	printk (KERN_INFO PFX "Minimum speed %d MHz. Maximum speed %d MHz.\n",
				minimum_speed, maximum_speed);

	policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	policy->cpuinfo.transition_latency = latency;
	policy->cur = maximum_speed;

	return cpufreq_frequency_table_cpuinfo(policy, powernow_table);
}

static struct cpufreq_driver powernow_driver = {
	.verify 	= powernow_verify,
	.target 	= powernow_target,
	.init		= powernow_cpu_init,
	.name		= "powernow-k7",
	.owner		= THIS_MODULE,
};

static int __init powernow_init (void)
{
	if (dmi_broken & BROKEN_CPUFREQ) {
		printk (KERN_INFO PFX "Disabled at boot time by DMI,\n");
		return -ENODEV;
	}
	if (check_powernow()==0)
		return -ENODEV;
	return cpufreq_register_driver(&powernow_driver);
}


static void __exit powernow_exit (void)
{
	cpufreq_unregister_driver(&powernow_driver);
	if (powernow_table)
		kfree(powernow_table);
}

MODULE_AUTHOR ("Dave Jones <davej@suse.de>");
MODULE_DESCRIPTION ("Powernow driver for AMD K7 processors.");
MODULE_LICENSE ("GPL");

module_init(powernow_init);
module_exit(powernow_exit);

