/*
 *   (c) 2003, 2004 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 *
 *  Support : paul.devriendt@amd.com
 *
 *  Based on the powernow-k7.c module written by Dave Jones.
 *  (C) 2003 Dave Jones <davej@codemonkey.org.uk> on behalf of SuSE Labs
 *  (C) 2004 Dominik Brodowski <linux@brodo.de>
 *  (C) 2004 Pavel Machek <pavel@suse.cz>
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by AMD.
 *
 *  Processor information obtained from Chapter 9 (Power and Thermal Management)
 *  of the "BIOS and Kernel Developer's Guide for the AMD Athlon 64 and AMD
 *  Opteron Processors", revision 3.03, available for download from www.amd.com
 *
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <acpi/acpi_bus.h> 	/* For acpi_disabled */

#include <asm/msr.h>
#include <asm/io.h>
#include <asm/delay.h>

#define PFX "powernow-k8: "
#define BFX PFX "BIOS error: "
#define VERSION "version 1.00.13"
#include "powernow-k8.h"

/*
 * Version 1.4 of the PSB table. This table is constructed by BIOS and is
 * to tell the OS's power management driver which VIDs and FIDs are
 * supported by this particular processor. This information is obtained from
 * the data sheets for each processor model by the system vendor and
 * incorporated into the BIOS.
 * If the data in the PSB / PST is wrong, then this driver will program the
 * wrong values into hardware, which is very likely to lead to a crash.
 */

#define PSB_ID_STRING      "AMDK7PNOW!"
#define PSB_ID_STRING_LEN  10

#define PSB_VERSION_1_4  0x14

struct psb_s {
	u8 signature[10];
	u8 tableversion;
	u8 flags1;
	u16 voltagestabilizationtime;
	u8 flags2;
	u8 numpst;
	u32 cpuid;
	u8 plllocktime;
	u8 maxfid;
	u8 maxvid;
	u8 numpstates;
};

/* Pairs of fid/vid values are appended to the version 1.4 PSB table. */
struct pst_s {
	u8 fid;
	u8 vid;
};

static u32 vstable;	/* voltage stabilization time, from PSB, units 20 us */
static u32 plllock;	/* pll lock time, from PSB, units 1 us */
static u32 rvo;		/* ramp voltage offset, from PSB */
static u32 irt;		/* isochronous relief time, from PSB */
static u32 vidmvs;	/* usable value calculated from mvs, from PSB */

/* We have only one processor, but this way code can be shared */
static struct cpu_power single_cpu_power;
static struct cpu_power *perproc = &single_cpu_power;

static struct cpufreq_frequency_table *powernow_table;

/*
The PSB table supplied by BIOS allows for the definition of the number of
p-states that can be used when running on a/c, and the number of p-states
that can be used when running on battery. This allows laptop manufacturers
to force the system to save power when running from battery. The relationship 
is :
   1 <= number_of_battery_p_states <= maximum_number_of_p_states

This driver does NOT have the support in it to detect transitions from
a/c power to battery power, and thus trigger the transition to a lower
p-state if required. This is because I need ACPI and the 2.6 kernel to do 
this, and this is a 2.4 kernel driver. Check back for a new improved driver
for the 2.6 kernel soon.

This code therefore assumes it is on battery at all times, and thus
restricts performance to number_of_battery_p_states. For desktops, 
  number_of_battery_p_states == maximum_number_of_pstates, 
so this is not actually a restriction.
*/

static u32 batps;	/* limit on the number of p states when on battery */
			/* - set by BIOS in the PSB/PST                    */

/* write the new fid value along with the other control fields to the msr */
static int
write_new_fid(struct cpu_power *perproc, u32 idx, u8 fid)
{
	u32 lo;
	u32 savevid = perproc->cvid;

	if ((fid & INVALID_FID_MASK) || (perproc->cvid & INVALID_VID_MASK)) {
		printk(KERN_ERR PFX "internal error - overflow on fid write\n");
		return 1;
	}

	lo = fid | (perproc->cvid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT;

	dprintk(KERN_DEBUG PFX "writing fid %x, lo %x, hi %x\n",
		fid, lo, plllock * PLL_LOCK_CONVERSION);

	wrmsr(MSR_FIDVID_CTL, lo, plllock * PLL_LOCK_CONVERSION);

	if (query_current_values_with_pending_wait(perproc))
		return 1;

	count_off_irt(irt);

	if (savevid != perproc->cvid) {
		printk(KERN_ERR PFX
		       "vid changed on fid transition, save %x, perproc->cvid %x\n",
		       savevid, perproc->cvid);
		return 1;
	}

	if (fid != perproc->cfid) {
		printk(KERN_ERR PFX
		       "fid transition failed, fid %x, perproc->cfid %x\n",
		        fid, perproc->cfid);
		return 1;
	}

	return 0;
}

/*
 * Reduce the vid by the max of step or reqvid.
 * Decreasing vid codes represent increasing voltages:
 * vid of 0 is 1.550V, vid of 0x1e is 0.800V, vid of 0x1f is off.
 */
static int
decrease_vid_code_by_step(u32 reqvid, u32 step)
{
	if ((perproc->cvid - reqvid) > step)
		reqvid = perproc->cvid - step;

	if (write_new_vid(perproc, reqvid))
		return 1;

	count_off_vst(vstable);

	return 0;
}


/*
 * Phase 1 - core voltage transition ... setup appropriate voltage for the
 * fid transition.
 */
static inline int
core_voltage_pre_transition(struct cpu_power *perproc, u32 idx, u8 reqvid)
{
	u32 rvosteps = rvo;
	u32 savefid = perproc->cfid;

	dprintk(KERN_DEBUG PFX
		"ph1: start, perproc->cfid 0x%x, perproc->cvid 0x%x, reqvid 0x%x, rvo %x\n",
		perproc->cfid, perproc->cvid, reqvid, rvo);

	while (perproc->cvid > reqvid) {
		dprintk(KERN_DEBUG PFX "ph1: curr 0x%x, requesting vid 0x%x\n",
			perproc->cvid, reqvid);
		if (decrease_vid_code_by_step(reqvid, vidmvs))
			return 1;
	}

	while (rvosteps) {
		if (perproc->cvid == 0) {
			rvosteps = 0;
		} else {
			dprintk(KERN_DEBUG PFX
				"ph1: changing vid for rvo, requesting 0x%x\n",
				perproc->cvid - 1);
			if (decrease_vid_code_by_step(perproc->cvid - 1, 1))
				return 1;
			rvosteps--;
		}
	}

	if (query_current_values_with_pending_wait(perproc))
		return 1;

	if (savefid != perproc->cfid) {
		printk(KERN_ERR PFX "ph1 err, perproc->cfid changed 0x%x\n", perproc->cfid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph1 complete, perproc->cfid 0x%x, perproc->cvid 0x%x\n",
		perproc->cfid, perproc->cvid);

	return 0;
}

static int check_pst_table(struct pst_s *pst, u8 maxvid)
{
	unsigned int j;
	u8 lastfid = 0xFF;

	for (j = 0; j < perproc->numps; j++) {
		if (pst[j].vid > LEAST_VID) {
			printk(KERN_ERR PFX "vid %d invalid : 0x%x\n", j, pst[j].vid);
			return -EINVAL;
		}
		if (pst[j].vid < rvo) {	/* vid + rvo >= 0 */
			printk(KERN_ERR PFX
			       "BIOS error - 0 vid exceeded with pstate %d\n",
			       j);
			return -ENODEV;
		}
		if (pst[j].vid < maxvid + rvo) {	/* vid + rvo >= maxvid */
			printk(KERN_ERR PFX
			       "BIOS error - maxvid exceeded with pstate %d\n",
			       j);
			return -ENODEV;
		}
		if ((pst[j].fid > MAX_FID)
		    || (pst[j].fid & 1)
		    || (j && (pst[j].fid < HI_FID_TABLE_BOTTOM))) {
			/* Only first fid is allowed to be in "low" range */
			printk(KERN_ERR PFX "fid %d invalid : 0x%x\n", j, pst[j].fid);
			return -EINVAL;
		}
		if (pst[j].fid < lastfid)
			lastfid = pst[j].fid;
	}
	if (lastfid & 1) {
		printk(KERN_ERR PFX "lastfid invalid\n");
		return -EINVAL;
	}
	if (lastfid > LO_FID_TABLE_TOP) {
		printk(KERN_INFO PFX  "first fid not from lo freq table\n");
	}

	return 0;
}

/* Find and validate the PSB/PST table in BIOS. */
static inline int
find_psb_table(void)
{
	struct psb_s *psb;
	struct pst_s *pst;
	unsigned int i, j;
	u32 mvs;
	u8 maxvid;
	int arima = 0;

	for (i = 0xc0000; i < 0xffff0; i += 0x10) {
		/* Scan BIOS looking for the signature. */
		/* It can not be at ffff0 - it is too big. */

		psb = phys_to_virt(i);
		if (memcmp(psb, PSB_ID_STRING, PSB_ID_STRING_LEN) != 0)
			continue;

		dprintk(KERN_DEBUG PFX "found PSB header at 0x%p\n", psb);

		dprintk(KERN_DEBUG PFX "table vers: 0x%x\n", psb->tableversion);
		if (psb->tableversion != PSB_VERSION_1_4) {
			printk(KERN_INFO BFX "PSB table is not v1.4\n");
			return -ENODEV;
		}

		dprintk(KERN_DEBUG PFX "flags: 0x%x\n", psb->flags1);
		if (psb->flags1) {
			printk(KERN_ERR BFX "unknown flags\n");
			return -ENODEV;
		}

		vstable = psb->voltagestabilizationtime;
		dprintk(KERN_DEBUG PFX "flags2: 0x%x\n", psb->flags2);
		rvo = psb->flags2 & 3;
		irt = ((psb->flags2) >> 2) & 3;
		mvs = ((psb->flags2) >> 4) & 3;
		vidmvs = 1 << mvs;
		batps = ((psb->flags2) >> 6) & 3;

		printk(KERN_INFO PFX "voltage stable in %d usec", vstable * 20);
		if (batps)
			printk(", only %d lowest states on battery", batps);
		printk(", ramp voltage offset: %d", rvo);
		printk(", isochronous relief time: %d", irt);
		printk(", maximum voltage step: %d\n", mvs);

		dprintk(KERN_DEBUG PFX "numpst: 0x%x\n", psb->perproc->numpst);
		if (psb->numpst != 1) {
			printk(KERN_ERR BFX "numpst must be 1\n");
			return -ENODEV;
		}

		dprintk(KERN_DEBUG PFX "cpuid: 0x%x\n", psb->cpuid);

		plllock = psb->plllocktime;
		printk(KERN_INFO PFX "pll lock time: 0x%x, ", plllock);

		maxvid = psb->maxvid;
		printk("maxfid 0x%x (%d MHz), maxvid 0x%x\n", 
		       psb->maxfid, freq_from_fid(psb->maxfid), maxvid);

		perproc->numps = arima ? 3 : psb->numpstates;
		if (perproc->numps < 2) {
			printk(KERN_ERR BFX "no p states to transition\n");
			return -ENODEV;
		}

		if (batps == 0) {
			batps = perproc->numps;
		} else if (batps > perproc->numps) {
			printk(KERN_ERR BFX "batterypstates > perproc->numpstates\n");
			batps = perproc->numps;
		} else {
			printk(KERN_ERR PFX
			       "Restricting operation to %d p-states\n", batps);
			printk(KERN_ERR PFX
			       "Use the ACPI driver to access all %d p-states\n",
			       perproc->numps);
		}

		if (perproc->numps <= 1) {
			printk(KERN_ERR PFX "only 1 p-state to transition\n");
			return -ENODEV;
		}

		pst = (struct pst_s *) (psb + 1);
		if (check_pst_table(pst, maxvid)) {
			if (!arima)
				return -EINVAL;
		}

		powernow_table = kmalloc((sizeof(struct cpufreq_frequency_table) * (perproc->numps + 1)), GFP_KERNEL);
		if (!powernow_table) {
			printk(KERN_ERR PFX "powernow_table memory alloc failure\n");
			return -ENOMEM;
		}

		for (j = 0; j < psb->numpstates; j++) {
			powernow_table[j].index = pst[j].fid; /* lower 8 bits */
			powernow_table[j].index |= (pst[j].vid << 8); /* upper 8 bits */
		}

		/* If you want to override your frequency tables, this
		   is right place. */

		if (arima) {
			powernow_table[1].index = 0x0608;
			powernow_table[2].index = 0x020a;
			powernow_table[3].index = 0x000c;
		}

		for (j = 0; j < perproc->numps; j++) {
			powernow_table[j].frequency = freq_from_fid(powernow_table[j].index & 0xff)*1000;
			printk(KERN_INFO PFX "   %d : fid 0x%x (%d MHz), vid 0x%x\n", j,
			       powernow_table[j].index & 0xff,
			       powernow_table[j].frequency/1000,
			       powernow_table[j].index >> 8);
		}

		powernow_table[perproc->numps].frequency = CPUFREQ_TABLE_END;
		powernow_table[perproc->numps].index = 0;

		if (query_current_values_with_pending_wait(perproc)) {
			kfree(powernow_table);
			return -EIO;
		}

		printk(KERN_INFO PFX "perproc->cfid 0x%x (%d MHz), perproc->cvid 0x%x\n",
		       perproc->cfid, freq_from_fid(perproc->cfid), perproc->cvid);

		for (j = 0; j < perproc->numps; j++)
			if ((pst[j].fid==perproc->cfid) && (pst[j].vid==perproc->cvid))
				return 0;

		printk(KERN_ERR BFX "perproc->cfid/vid do not match PST, ignoring\n");
		return 0;
	}

	printk(KERN_ERR BFX "no PSB\n");
	return -ENODEV;
}

/* Take a frequency, and issue the fid/vid transition command */
static inline int
transition_frequency(unsigned int index)
{
	u32 fid;
	u32 vid;
	int res;
	struct cpufreq_freqs freqs;

	/* fid are the lower 8 bits of the index we stored into
	 * the cpufreq frequency table in find_psb_table, vid are 
	 * the upper 8 bits.
	 */

	fid = powernow_table[index].index & 0xFF;
	vid = (powernow_table[index].index & 0xFF00) >> 8;

	dprintk(KERN_DEBUG PFX "table matched fid 0x%x, giving vid 0x%x\n",
		fid, vid);

	if (query_current_values_with_pending_wait(perproc))
		return 1;

	if ((perproc->cvid == vid) && (perproc->cfid == fid)) {
		dprintk(KERN_DEBUG PFX
			"target matches current values (fid 0x%x, vid 0x%x)\n",
			fid, vid);
		return 0;
	}

	if ((fid < HI_FID_TABLE_BOTTOM) && (perproc->cfid < HI_FID_TABLE_BOTTOM)) {
		printk(KERN_ERR PFX
		       "ignoring illegal change in lo freq table-%x to %x\n",
		       perproc->cfid, fid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "changing to fid 0x%x, vid 0x%x\n", fid, vid);

	freqs.cpu = 0;	/* only true because SMP not supported */

	freqs.old = freq_from_fid(perproc->cfid);
	freqs.new = freq_from_fid(fid);
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	res = transition_fid_vid(perproc, 0, fid, vid);

	freqs.new = freq_from_fid(perproc->cfid);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return res;
}

/* Driver entry point to switch to the target frequency */
static int
powernowk8_target(struct cpufreq_policy *pol, unsigned targfreq, unsigned relation)
{
	u32 checkfid = perproc->cfid;
	u32 checkvid = perproc->cvid;
	unsigned int newstate;

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "drv targ fail: change pending bit set\n");
		return -EIO;
	}

	dprintk(KERN_DEBUG PFX "targ: %d kHz, min %d, max %d, relation %d\n",
		targfreq, pol->min, pol->max, relation);

	if (query_current_values_with_pending_wait(perproc))
		return -EIO;

	dprintk(KERN_DEBUG PFX "targ: curr fid 0x%x, vid 0x%x\n",
		perproc->cfid, perproc->cvid);

	if ((checkvid != perproc->cvid) || (checkfid != perproc->cfid)) {
		printk(KERN_ERR PFX
		       "error - out of sync, fid 0x%x 0x%x, vid 0x%x 0x%x\n",
		       checkfid, perproc->cfid, checkvid, perproc->cvid);
	}

	if (cpufreq_frequency_table_target(pol, powernow_table, targfreq, relation, &newstate))
		return -EINVAL;
	
	if (transition_frequency(newstate))
	{
		printk(KERN_ERR PFX "transition frequency failed\n");
		return 1;
	}

	pol->cur = 1000 * freq_from_fid(perproc->cfid);

	return 0;
}

/* Driver entry point to verify the policy and range of frequencies */
static int
powernowk8_verify(struct cpufreq_policy *pol)
{
	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "failing verify, change pending bit set\n");
		return -EIO;
	}

	return cpufreq_frequency_table_verify(pol, powernow_table);
}

/* per CPU init entry point to the driver */
static int __init
powernowk8_cpu_init(struct cpufreq_policy *pol)
{
	if (pol->cpu != 0) {
		printk(KERN_ERR PFX "init not cpu 0\n");
		return -ENODEV;
	}

	pol->governor = CPUFREQ_DEFAULT_GOVERNOR;

	/* Take a crude guess here. 
	 * That guess was in microseconds, so multiply with 1000 */
	pol->cpuinfo.transition_latency = (((rvo + 8) * vstable * VST_UNITS_20US)
	    + (3 * (1 << irt) * 10)) * 1000;

	if (query_current_values_with_pending_wait(perproc))
		return -EIO;

	pol->cur = 1000 * freq_from_fid(perproc->cfid);
	dprintk(KERN_DEBUG PFX "policy current frequency %d kHz\n", pol->cur);

	/* min/max the cpu is capable of */
	if (cpufreq_frequency_table_cpuinfo(pol, powernow_table)) {
		printk(KERN_ERR PFX "invalid powernow_table\n");
		kfree(powernow_table);
		return -EINVAL;
	}

	cpufreq_frequency_table_get_attr(powernow_table, pol->cpu);

	printk(KERN_INFO PFX "cpu_init done, current fid 0x%x, vid 0x%x\n",
	       perproc->cfid, perproc->cvid);

	return 0;
}

static int __exit powernowk8_cpu_exit (struct cpufreq_policy *pol)
{
	if (pol->cpu != 0)
		return -EINVAL;

	cpufreq_frequency_table_put_attr(pol->cpu);

	if (powernow_table)
		kfree(powernow_table);

	return 0;
}

static struct freq_attr* powernow_k8_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver cpufreq_amd64_driver = {
	.verify = powernowk8_verify,
	.target = powernowk8_target,
	.init = powernowk8_cpu_init,
	.exit = powernowk8_cpu_exit,
	.name = "powernow-k8",
	.owner = THIS_MODULE,
	.attr = powernow_k8_attr,
};


/* driver entry point for init */
static int __init
powernowk8_init(void)
{
	int rc;

#ifdef CONFIG_X86_POWERNOW_K8_ACPI
	if (!acpi_disabled) {
		printk(KERN_INFO PFX "Turning off powernow-k8, powernow-k8-acpi will take over\n");
		return -EINVAL;
	}
#endif

	if (num_online_cpus() != 1) {
		printk(KERN_INFO PFX "multiprocessor systems not supported\n");
		return -ENODEV;
	}

	if (check_supported_cpu() == 0)
		return -ENODEV;

	rc = find_psb_table();
	if (rc)
		return rc;

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "powernowk8_init fail, change pending bit set\n");
		return -EIO;
	}

	return cpufreq_register_driver(&cpufreq_amd64_driver);
}

/* driver entry point for term */
static void __exit
powernowk8_exit(void)
{
	dprintk(KERN_INFO PFX "powernowk8_exit\n");

	cpufreq_unregister_driver(&cpufreq_amd64_driver);
}

MODULE_AUTHOR("Paul Devriendt <paul.devriendt@amd.com>");
MODULE_DESCRIPTION("AMD Athlon 64 and Opteron processor frequency driver.");
MODULE_LICENSE("GPL");

module_init(powernowk8_init);
module_exit(powernowk8_exit);
