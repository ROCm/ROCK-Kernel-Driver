/*
 *   (c) 2003 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "../../../COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 *
 *  Support : paul.devriendt@amd.com
 *
 *  Based on the powernow-k7.c module written by Dave Jones.
 *  (C) 2003 Dave Jones <davej@codemonkey.ork.uk> on behalf of SuSE Labs
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

#include <asm/msr.h>
#include <asm/io.h>
#include <asm/delay.h>

#define PFX "powernow-k8: "
#define BFX PFX "BIOS error: "
#define VERSION "version 1.00.08a"
#include "powernow-k8.h"

#ifdef CONFIG_PREEMPT
#warning this driver has not been tested on a preempt system
#endif

static u32 vstable;	/* voltage stabalization time, from PSB, units 20 us */
static u32 plllock;	/* pll lock time, from PSB, units 1 us */
static u32 numps;	/* number of p-states, from PSB */
static u32 rvo;		/* ramp voltage offset, from PSB */
static u32 irt;		/* isochronous relief time, from PSB */
static u32 vidmvs;	/* usable value calculated from mvs, from PSB */
static u32 currvid;	/* keep track of the current fid / vid */
static u32 currfid;

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

 /* Return a frequency in MHz, given an input fid */
static u32 find_freq_from_fid(u32 fid)
{
 	return 800 + (fid * 100);
}


/* Return the vco fid for an input fid */
static u32
convert_fid_to_vco_fid(u32 fid)
{
	if (fid < HI_FID_TABLE_BOTTOM) {
		return 8 + (2 * fid);
	} else {
		return fid;
	}
}

/*
 * Return 1 if the pending bit is set. Unless we are actually just told the
 * processor to transition a state, seeing this bit set is really bad news.
 */
static inline int
pending_bit_stuck(void)
{
	u32 lo, hi;

	rdmsr(MSR_FIDVID_STATUS, lo, hi);
	return lo & MSR_S_LO_CHANGE_PENDING ? 1 : 0;
}

/*
 * Update the global current fid / vid values from the status msr. Returns 1
 * on error.
 */
static int
query_current_values_with_pending_wait(void)
{
	u32 lo, hi;
	u32 i = 0;

	lo = MSR_S_LO_CHANGE_PENDING;
	while (lo & MSR_S_LO_CHANGE_PENDING) {
		if (i++ > 0x1000000) {
			printk(KERN_ERR PFX "detected change pending stuck\n");
			return 1;
		}
		rdmsr(MSR_FIDVID_STATUS, lo, hi);
	}

	currvid = hi & MSR_S_HI_CURRENT_VID;
	currfid = lo & MSR_S_LO_CURRENT_FID;

	return 0;
}

/* the isochronous relief time */
static inline void
count_off_irt(void)
{
	udelay((1 << irt) * 10);
	return;
}

/* the voltage stabalization time */
static inline void
count_off_vst(void)
{
	udelay(vstable * VST_UNITS_20US);
	return;
}

/* write the new fid value along with the other control fields to the msr */
static int
write_new_fid(u32 fid)
{
	u32 lo;
	u32 savevid = currvid;

	if ((fid & INVALID_FID_MASK) || (currvid & INVALID_VID_MASK)) {
		printk(KERN_ERR PFX "internal error - overflow on fid write\n");
		return 1;
	}

	lo = fid | (currvid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT_FID_VID;

	dprintk(KERN_DEBUG PFX "writing fid %x, lo %x, hi %x\n",
		fid, lo, plllock * PLL_LOCK_CONVERSION);

	wrmsr(MSR_FIDVID_CTL, lo, plllock * PLL_LOCK_CONVERSION);

	if (query_current_values_with_pending_wait())
		return 1;

	count_off_irt();

	if (savevid != currvid) {
		printk(KERN_ERR PFX
		       "vid changed on fid transition, save %x, currvid %x\n",
		       savevid, currvid);
		return 1;
	}

	if (fid != currfid) {
		printk(KERN_ERR PFX
		       "fid transition failed, fid %x, currfid %x\n",
		        fid, currfid);
		return 1;
	}

	return 0;
}

/* Write a new vid to the hardware */
static int
write_new_vid(u32 vid)
{
	u32 lo;
	u32 savefid = currfid;

	if ((currfid & INVALID_FID_MASK) || (vid & INVALID_VID_MASK)) {
		printk(KERN_ERR PFX "internal error - overflow on vid write\n");
		return 1;
	}

	lo = currfid | (vid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT_FID_VID;

	dprintk(KERN_DEBUG PFX "writing vid %x, lo %x, hi %x\n",
		vid, lo, STOP_GRANT_5NS);

	wrmsr(MSR_FIDVID_CTL, lo, STOP_GRANT_5NS);

	if (query_current_values_with_pending_wait()) {
		return 1;
	}

	if (savefid != currfid) {
		printk(KERN_ERR PFX
		       "fid changed on vid transition, save %x currfid %x\n",
		       savefid, currfid);
		return 1;
	}

	if (vid != currvid) {
		printk(KERN_ERR PFX
		       "vid transition failed, vid %x, currvid %x\n",
		       vid, currvid);
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
	if ((currvid - reqvid) > step)
		reqvid = currvid - step;

	if (write_new_vid(reqvid))
		return 1;

	count_off_vst();

	return 0;
}

/* Change the fid and vid, by the 3 phases. */
static inline int
transition_fid_vid(u32 reqfid, u32 reqvid)
{
	if (core_voltage_pre_transition(reqvid))
		return 1;

	if (core_frequency_transition(reqfid))
		return 1;

	if (core_voltage_post_transition(reqvid))
		return 1;

	if (query_current_values_with_pending_wait())
		return 1;

	if ((reqfid != currfid) || (reqvid != currvid)) {
		printk(KERN_ERR PFX "failed: req 0x%x 0x%x, curr 0x%x 0x%x\n",
		       reqfid, reqvid, currfid, currvid);
		return 1;
	}

	dprintk(KERN_INFO PFX
		"transitioned: new fid 0x%x, vid 0x%x\n", currfid, currvid);

	return 0;
}

/*
 * Phase 1 - core voltage transition ... setup appropriate voltage for the
 * fid transition.
 */
static inline int
core_voltage_pre_transition(u32 reqvid)
{
	u32 rvosteps = rvo;
	u32 savefid = currfid;

	dprintk(KERN_DEBUG PFX
		"ph1: start, currfid 0x%x, currvid 0x%x, reqvid 0x%x, rvo %x\n",
		currfid, currvid, reqvid, rvo);

	while (currvid > reqvid) {
		dprintk(KERN_DEBUG PFX "ph1: curr 0x%x, requesting vid 0x%x\n",
			currvid, reqvid);
		if (decrease_vid_code_by_step(reqvid, vidmvs))
			return 1;
	}

	while (rvosteps > 0) {
		if (currvid == 0) {
			rvosteps = 0;
		} else {
			dprintk(KERN_DEBUG PFX
				"ph1: changing vid for rvo, requesting 0x%x\n",
				currvid - 1);
			if (decrease_vid_code_by_step(currvid - 1, 1))
				return 1;
			rvosteps--;
		}
	}

	if (query_current_values_with_pending_wait())
		return 1;

	if (savefid != currfid) {
		printk(KERN_ERR PFX "ph1 err, currfid changed 0x%x\n", currfid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph1 complete, currfid 0x%x, currvid 0x%x\n",
		currfid, currvid);

	return 0;
}

/* Phase 2 - core frequency transition */
static inline int
core_frequency_transition(u32 reqfid)
{
	u32 vcoreqfid;
	u32 vcocurrfid;
	u32 vcofiddiff;
	u32 savevid = currvid;

	if ((reqfid < HI_FID_TABLE_BOTTOM) && (currfid < HI_FID_TABLE_BOTTOM)) {
		printk(KERN_ERR PFX "ph2 illegal lo-lo transition 0x%x 0x%x\n",
		       reqfid, currfid);
		return 1;
	}

	if (currfid == reqfid) {
		printk(KERN_ERR PFX "ph2 null fid transition 0x%x\n", currfid);
		return 0;
	}

	dprintk(KERN_DEBUG PFX
		"ph2 starting, currfid 0x%x, currvid 0x%x, reqfid 0x%x\n",
		currfid, currvid, reqfid);

	vcoreqfid = convert_fid_to_vco_fid(reqfid);
	vcocurrfid = convert_fid_to_vco_fid(currfid);
	vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
	    : vcoreqfid - vcocurrfid;

	while (vcofiddiff > 2) {
		if (reqfid > currfid) {
			if (currfid > LO_FID_TABLE_TOP) {
				if (write_new_fid(currfid + 2)) {
					return 1;
				}
			} else {
				if (write_new_fid
				    (2 + convert_fid_to_vco_fid(currfid))) {
					return 1;
				}
			}
		} else {
			if (write_new_fid(currfid - 2))
				return 1;
		}

		vcocurrfid = convert_fid_to_vco_fid(currfid);
		vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
		    : vcoreqfid - vcocurrfid;
	}

	if (write_new_fid(reqfid))
		return 1;

	if (query_current_values_with_pending_wait())
		return 1;

	if (currfid != reqfid) {
		printk(KERN_ERR PFX
		       "ph2 mismatch, failed fid transition, curr %x, req %x\n",
		       currfid, reqfid);
		return 1;
	}

	if (savevid != currvid) {
		printk(KERN_ERR PFX
		       "ph2 vid changed, save %x, curr %x\n", savevid,
		       currvid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph2 complete, currfid 0x%x, currvid 0x%x\n",
		currfid, currvid);

	return 0;
}

/* Phase 3 - core voltage transition flow ... jump to the final vid. */
static inline int
core_voltage_post_transition(u32 reqvid)
{
	u32 savefid = currfid;
	u32 savereqvid = reqvid;

	dprintk(KERN_DEBUG PFX "ph3 starting, currfid 0x%x, currvid 0x%x\n",
		currfid, currvid);

	if (reqvid != currvid) {
		if (write_new_vid(reqvid))
			return 1;

		if (savefid != currfid) {
			printk(KERN_ERR PFX
			       "ph3: bad fid change, save %x, curr %x\n",
			       savefid, currfid);
			return 1;
		}

		if (currvid != reqvid) {
			printk(KERN_ERR PFX
			       "ph3: failed vid transition\n, req %x, curr %x",
			       reqvid, currvid);
			return 1;
		}
	}

	if (query_current_values_with_pending_wait())
		return 1;

	if (savereqvid != currvid) {
		dprintk(KERN_ERR PFX "ph3 failed, currvid 0x%x\n", currvid);
		return 1;
	}

	if (savefid != currfid) {
		dprintk(KERN_ERR PFX "ph3 failed, currfid changed 0x%x\n",
			currfid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph3 complete, currfid 0x%x, currvid 0x%x\n",
		currfid, currvid);

	return 0;
}

static inline int
check_supported_cpu(void)
{
	struct cpuinfo_x86 *c = cpu_data;
	u32 eax, ebx, ecx, edx;

	if (num_online_cpus() != 1) {
		printk(KERN_INFO PFX "multiprocessor systems not supported\n");
		return 0;
	}

	if (c->x86_vendor != X86_VENDOR_AMD) {
#ifdef MODULE
		printk(KERN_INFO PFX "Not an AMD processor\n");
#endif
		return 0;
	}

	eax = cpuid_eax(CPUID_PROCESSOR_SIGNATURE);
	if ((eax & CPUID_XFAM_MOD) == ATHLON64_XFAM_MOD) {
		dprintk(KERN_DEBUG PFX "AMD Althon 64 Processor found\n");
		if ((eax & CPUID_F1_STEP) < ATHLON64_REV_C0) {
			printk(KERN_INFO PFX "Revision C0 or better "
			       "AMD Athlon 64 processor required\n");
			return 0;
		}
	} else if ((eax & CPUID_XFAM_MOD) == OPTERON_XFAM_MOD) {
		dprintk(KERN_DEBUG PFX "AMD Opteron Processor found\n");
	} else {
		printk(KERN_INFO PFX
		       "AMD Athlon 64 or AMD Opteron processor required\n");
		return 0;
	}

	eax = cpuid_eax(CPUID_GET_MAX_CAPABILITIES);
	if (eax < CPUID_FREQ_VOLT_CAPABILITIES) {
		printk(KERN_INFO PFX
		       "No frequency change capabilities detected\n");
		return 0;
	}

	cpuid(CPUID_FREQ_VOLT_CAPABILITIES, &eax, &ebx, &ecx, &edx);
	if ((edx & P_STATE_TRANSITION_CAPABLE) != P_STATE_TRANSITION_CAPABLE) {
		printk(KERN_INFO PFX "Power state transitions not supported\n");
		return 0;
	}

	printk(KERN_INFO PFX "Found AMD64 processor supporting PowerNow (" VERSION ")\n");
	return 1;
}

static int check_pst_table(struct pst_s *pst, u8 maxvid)
{
	unsigned int j;
	u8 lastfid = 0xFF;

	for (j = 0; j < numps; j++) {
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

		dprintk(KERN_DEBUG PFX "numpst: 0x%x\n", psb->numpst);
		if (psb->numpst != 1) {
			printk(KERN_ERR BFX "numpst must be 1\n");
			return -ENODEV;
		}

		dprintk(KERN_DEBUG PFX "cpuid: 0x%x\n", psb->cpuid);

		plllock = psb->plllocktime;
		printk(KERN_INFO PFX "pll lock time: 0x%x, ", plllock);

		maxvid = psb->maxvid;
		printk("maxfid 0x%x (%d MHz), maxvid 0x%x\n", 
		       psb->maxfid, find_freq_from_fid(psb->maxfid), maxvid);

		numps = psb->numpstates;
		if (numps < 2) {
			printk(KERN_ERR BFX "no p states to transition\n");
			return -ENODEV;
		}

		if (batps == 0) {
			batps = numps;
		} else if (batps > numps) {
			printk(KERN_ERR BFX "batterypstates > numpstates\n");
			batps = numps;
		} else {
			printk(KERN_ERR PFX
			       "Restricting operation to %d p-states\n", batps);
			printk(KERN_ERR PFX
			       "Check for an updated driver to access all "
			       "%d p-states\n", numps);
		}

		if (numps <= 1) {
			printk(KERN_ERR PFX "only 1 p-state to transition\n");
			return -ENODEV;
		}

		pst = (struct pst_s *) (psb + 1);
		if (check_pst_table(pst, maxvid))
			return -EINVAL;

		powernow_table = kmalloc((sizeof(struct cpufreq_frequency_table) * (numps + 1)), GFP_KERNEL);
		if (!powernow_table) {
			printk(KERN_ERR PFX "powernow_table memory alloc failure\n");
			return -ENOMEM;
		}

		for (j = 0; j < numps; j++) {
			printk(KERN_INFO PFX "   %d : fid 0x%x (%d MHz), vid 0x%x\n", j,
			       pst[j].fid, find_freq_from_fid(pst[j].fid), pst[j].vid);
			powernow_table[j].index = pst[j].fid; /* lower 8 bits */
			powernow_table[j].index |= (pst[j].vid << 8); /* upper 8 bits */
			powernow_table[j].frequency = find_freq_from_fid(pst[j].fid);
		}
		powernow_table[numps].frequency = CPUFREQ_TABLE_END;
		powernow_table[numps].index = 0;

		if (query_current_values_with_pending_wait()) {
			kfree(powernow_table);
			return -EIO;
		}

		printk(KERN_INFO PFX "currfid 0x%x (%d MHz), currvid 0x%x\n",
		       currfid, find_freq_from_fid(currfid), currvid);

		for (j = 0; j < numps; j++)
			if ((pst[j].fid==currfid) && (pst[j].vid==currvid))
				return 0;

		printk(KERN_ERR BFX "currfid/vid do not match PST, ignoring\n");
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

	if (query_current_values_with_pending_wait())
		return 1;

	if ((currvid == vid) && (currfid == fid)) {
		dprintk(KERN_DEBUG PFX
			"target matches current values (fid 0x%x, vid 0x%x)\n",
			fid, vid);
		return 0;
	}

	if ((fid < HI_FID_TABLE_BOTTOM) && (currfid < HI_FID_TABLE_BOTTOM)) {
		printk(KERN_ERR PFX
		       "ignoring illegal change in lo freq table-%x to %x\n",
		       currfid, fid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "changing to fid 0x%x, vid 0x%x\n", fid, vid);

	freqs.cpu = 0;	/* only true because SMP not supported */

	freqs.old = find_freq_from_fid(currfid);
	freqs.new = find_freq_from_fid(fid);
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	res = transition_fid_vid(fid, vid);

	freqs.new = find_freq_from_fid(currfid);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return res;
}

/* Driver entry point to switch to the target frequency */
static int
powernowk8_target(struct cpufreq_policy *pol, unsigned targfreq, unsigned relation)
{
	u32 checkfid = currfid;
	u32 checkvid = currvid;
	unsigned int newstate;

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "drv targ fail: change pending bit set\n");
		return -EIO;
	}

	dprintk(KERN_DEBUG PFX "targ: %d kHz, min %d, max %d, relation %d\n",
		targfreq, pol->min, pol->max, relation);

	if (query_current_values_with_pending_wait())
		return -EIO;

	dprintk(KERN_DEBUG PFX "targ: curr fid 0x%x, vid 0x%x\n",
		currfid, currvid);

	if ((checkvid != currvid) || (checkfid != currfid)) {
		printk(KERN_ERR PFX
		       "error - out of sync, fid 0x%x 0x%x, vid 0x%x 0x%x\n",
		       checkfid, currfid, checkvid, currvid);
	}

	if (cpufreq_frequency_table_target(pol, powernow_table, targfreq, relation, &newstate))
		return -EINVAL;
	
	if (transition_frequency(newstate))
	{
		printk(KERN_ERR PFX "transition frequency failed\n");
		return 1;
	}

	pol->cur = 1000 * find_freq_from_fid(currfid);

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
	 * That guess was in microseconds, so multply with 1000 */
	pol->cpuinfo.transition_latency = (((rvo + 8) * vstable * VST_UNITS_20US)
	    + (3 * (1 << irt) * 10)) * 1000;

	if (query_current_values_with_pending_wait())
		return -EIO;

	pol->cur = 1000 * find_freq_from_fid(currfid);
	dprintk(KERN_DEBUG PFX "policy current frequency %d kHz\n", pol->cur);

	/* min/max the cpu is capable of */
	if (cpufreq_frequency_table_cpuinfo(pol, powernow_table)) {
		printk(KERN_ERR PFX "invalid powernow_table\n");
		kfree(powernow_table);
		return -EINVAL;
	}

	cpufreq_frequency_table_get_attr(powernow_table, pol->cpu);

	printk(KERN_INFO PFX "cpu_init done, current fid 0x%x, vid 0x%x\n",
	       currfid, currvid);

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
