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
#define VERSION "version 1.00.08 - September 26, 2003"
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
struct pst_s *ppst;	/* array of p states, valid for this part */
static u32 currvid;	/* keep track of the current fid / vid */
static u32 currfid;

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

static struct cpufreq_driver cpufreq_amd64_driver = {
	.verify = powernowk8_verify,
	.target = powernowk8_target,
	.init = powernowk8_cpu_init,
	.name = "cpufreq-amd64",
	.owner = THIS_MODULE,
};

#define SEARCH_UP     1
#define SEARCH_DOWN   0

/* Return a frequency in MHz, given an input fid */
u32
find_freq_from_fid(u32 fid)
{
	return 800 + (fid * 100);
}

/* Return a fid matching an input frequency in MHz */
static u32
find_fid_from_freq(u32 freq)
{
	return (freq - 800) / 100;
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

/* Sort the fid/vid frequency table into ascending order by fid. The spec */
/* implies that it will be sorted by BIOS, but, it only implies it, and I */
/* prefer not to trust when I can check.                                  */
/* Yes, it is a simple bubble sort, but the PST is really small, so the   */
/* choice of algorithm is pretty irrelevant.                              */
static inline void
sort_pst(struct pst_s *ppst, u32 numpstates)
{
	u32 i;
	u8 tempfid;
	u8 tempvid;
	int swaps = 1;

	while (swaps) {
		swaps = 0;
		for (i = 0; i < (numpstates - 1); i++) {
			if (ppst[i].fid > ppst[i + 1].fid) {
				swaps = 1;
				tempfid = ppst[i].fid;
				tempvid = ppst[i].vid;
				ppst[i].fid = ppst[i + 1].fid;
				ppst[i].vid = ppst[i + 1].vid;
				ppst[i + 1].fid = tempfid;
				ppst[i + 1].vid = tempvid;
			}
		}
	}

	return;
}

/* Return 1 if the pending bit is set. Unless we are actually just told the */
/* processor to transition a state, seeing this bit set is really bad news. */
static inline int
pending_bit_stuck(void)
{
	u32 lo;
	u32 hi;

	rdmsr(MSR_FIDVID_STATUS, lo, hi);
	return lo & MSR_S_LO_CHANGE_PENDING ? 1 : 0;
}

/* Update the global current fid / vid values from the status msr. Returns 1 */
/* on error.                                                                 */
static int
query_current_values_with_pending_wait(void)
{
	u32 lo;
	u32 hi;
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

/* Reduce the vid by the max of step or reqvid.                   */
/* Decreasing vid codes represent increasing voltages :           */
/* vid of 0 is 1.550V, vid of 0x1e is 0.800V, vid of 0x1f is off. */
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

/* Phase 1 - core voltage transition ... setup appropriate voltage for the */
/* fid transition.                                                         */
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
		printk(KERN_INFO PFX "Not an AMD processor\n");
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

	printk(KERN_INFO PFX "Found AMD Athlon 64 / Opteron processor "
	       "supporting p-state transitions\n");

	return 1;
}

/* Find and validate the PSB/PST table in BIOS. */
static inline int
find_psb_table(void)
{
	struct psb_s *psb;
	struct pst_s *pst;
	unsigned i, j;
	u32 lastfid;
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
		printk(KERN_INFO PFX "voltage stable time: %d (units 20us)\n",
		       vstable);

		dprintk(KERN_DEBUG PFX "flags2: 0x%x\n", psb->flags2);
		rvo = psb->flags2 & 3;
		irt = ((psb->flags2) >> 2) & 3;
		mvs = ((psb->flags2) >> 4) & 3;
		vidmvs = 1 << mvs;
		batps = ((psb->flags2) >> 6) & 3;
		printk(KERN_INFO PFX "p states on battery: %d ", batps);
		switch (batps) {
		case 0:
			printk("- all available\n");
			break;
		case 1:
			printk("- only the minimum\n");
			break;
		case 2:
			printk("- only the 2 lowest\n");
			break;
		case 3:
			printk("- only the 3 lowest\n");
			break;
		}
		printk(KERN_INFO PFX "ramp voltage offset: %d\n", rvo);
		printk(KERN_INFO PFX "isochronous relief time: %d\n", irt);
		printk(KERN_INFO PFX "maximum voltage step: %d\n", mvs);

		dprintk(KERN_DEBUG PFX "numpst: 0x%x\n", psb->numpst);
		if (psb->numpst != 1) {
			printk(KERN_ERR BFX "numpst must be 1\n");
			return -ENODEV;
		}

		dprintk(KERN_DEBUG PFX "cpuid: 0x%x\n", psb->cpuid);

		plllock = psb->plllocktime;
		printk(KERN_INFO PFX "pll lock time: 0x%x\n", plllock);

		maxvid = psb->maxvid;
		printk(KERN_INFO PFX "maxfid: 0x%x\n", psb->maxfid);
		printk(KERN_INFO PFX "maxvid: 0x%x\n", maxvid);

		numps = psb->numpstates;
		printk(KERN_INFO PFX "numpstates: 0x%x\n", numps);
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

		if ((numps <= 1) || (batps <= 1)) {
			printk(KERN_ERR PFX "only 1 p-state to transition\n");
			return -ENODEV;
		}

		ppst = kmalloc(sizeof (struct pst_s) * numps, GFP_KERNEL);
		if (!ppst) {
			printk(KERN_ERR PFX "ppst memory alloc failure\n");
			return -ENOMEM;
		}

		pst = (struct pst_s *) (psb + 1);
		for (j = 0; j < numps; j++) {
			ppst[j].fid = pst[j].fid;
			ppst[j].vid = pst[j].vid;
			printk(KERN_INFO PFX
			       "   %d : fid 0x%x, vid 0x%x\n", j,
			       ppst[j].fid, ppst[j].vid);
		}
		sort_pst(ppst, numps);

		lastfid = ppst[0].fid;
		if (lastfid > LO_FID_TABLE_TOP)
			printk(KERN_INFO BFX "first fid not in lo freq tbl\n");

		if ((lastfid > MAX_FID) || (lastfid & 1) || (ppst[0].vid > LEAST_VID)) {
			printk(KERN_ERR BFX "first fid/vid bad (0x%x - 0x%x)\n",
			       lastfid, ppst[0].vid);
			kfree(ppst);
			return -ENODEV;
		}

		for (j = 1; j < numps; j++) {
			if ((lastfid >= ppst[j].fid)
			    || (ppst[j].fid & 1)
			    || (ppst[j].fid < HI_FID_TABLE_BOTTOM)
			    || (ppst[j].fid > MAX_FID)
			    || (ppst[j].vid > LEAST_VID)) {
				printk(KERN_ERR BFX
				       "invalid fid/vid in pst(%x %x)\n",
				       ppst[j].fid, ppst[j].vid);
				kfree(ppst);
				return -ENODEV;
			}
			lastfid = ppst[j].fid;
		}

		for (j = 0; j < numps; j++) {
			if (ppst[j].vid < rvo) {	/* vid+rvo >= 0 */
				printk(KERN_ERR BFX
				       "0 vid exceeded with pstate %d\n", j);
				return -ENODEV;
			}
			if (ppst[j].vid < maxvid+rvo) { /* vid+rvo >= maxvid */
				printk(KERN_ERR BFX
				       "maxvid exceeded with pstate %d\n", j);
				return -ENODEV;
			}
		}

		if (query_current_values_with_pending_wait()) {
			kfree(ppst);
			return -EIO;
		}

		printk(KERN_INFO PFX "currfid 0x%x, currvid 0x%x\n",
		       currfid, currvid);

		for (j = 0; j < numps; j++)
			if ((ppst[j].fid==currfid) && (ppst[j].vid==currvid))
				return (0);

		printk(KERN_ERR BFX "currfid/vid do not match PST, ignoring\n");
		return 0;
	}

	printk(KERN_ERR BFX "no PSB\n");
	return -ENODEV;
}

/* Converts a frequency (that might not necessarily be a multiple of 200) */
/* to a fid.                                                              */
static u32
find_closest_fid(u32 freq, int searchup)
{
	if (searchup == SEARCH_UP)
		freq += MIN_FREQ_RESOLUTION - 1;

	freq = (freq / MIN_FREQ_RESOLUTION) * MIN_FREQ_RESOLUTION;

	if (freq < MIN_FREQ)
		freq = MIN_FREQ;
	else if (freq > MAX_FREQ)
		freq = MAX_FREQ;

	return find_fid_from_freq(freq);
}

static int
find_match(u32 * ptargfreq, u32 * pmin, u32 * pmax, int searchup, u32 * pfid,
	   u32 * pvid)
{
	u32 availpstates = batps;
	u32 targfid = find_closest_fid(*ptargfreq, searchup);
	u32 minfid = find_closest_fid(*pmin, SEARCH_DOWN);
	u32 maxfid = find_closest_fid(*pmax, SEARCH_UP);
	u32 minidx = 0;
	u32 maxidx = availpstates - 1;
	u32 targidx = 0xffffffff;
	int i;

	dprintk(KERN_DEBUG PFX "find match: freq %d MHz, min %d, max %d\n",
		*ptargfreq, *pmin, *pmax);

	/* Restrict values to the frequency choices in the PST */
	if (minfid < ppst[0].fid)
		minfid = ppst[0].fid;
	if (maxfid > ppst[maxidx].fid)
		maxfid = ppst[maxidx].fid;

	/* Find appropriate PST index for the minimim fid */
	for (i = 0; i < (int) availpstates; i++) {
		if (minfid >= ppst[i].fid)
			minidx = i;
	}

	/* Find appropriate PST index for the maximum fid */
	for (i = availpstates - 1; i >= 0; i--) {
		if (maxfid <= ppst[i].fid)
			maxidx = i;
	}

	if (minidx > maxidx)
		maxidx = minidx;

	/* Frequency ids are now constrained by limits matching PST entries */
	minfid = ppst[minidx].fid;
	maxfid = ppst[maxidx].fid;

	/* Limit the target frequency to these limits */
	if (targfid < minfid)
		targfid = minfid;
	else if (targfid > maxfid)
		targfid = maxfid;

	/* Find the best target index into the PST, contrained by the range */
	if (searchup == SEARCH_UP) {
		for (i = maxidx; i >= (int) minidx; i--) {
			if (targfid <= ppst[i].fid)
				targidx = i;
		}
	} else {
		for (i = minidx; i <= (int) maxidx; i++) {
			if (targfid >= ppst[i].fid)
				targidx = i;
		}
	}

	if (targidx == 0xffffffff) {
		printk(KERN_ERR PFX "could not find target\n");
		return 1;
	}

	*pmin = find_freq_from_fid(minfid);
	*pmax = find_freq_from_fid(maxfid);
	*ptargfreq = find_freq_from_fid(ppst[targidx].fid);

	if (pfid)
		*pfid = ppst[targidx].fid;
	if (pvid)
		*pvid = ppst[targidx].vid;

	return 0;
}

/* Take a frequency, and issue the fid/vid transition command */
static inline int
transition_frequency(u32 * preq, u32 * pmin, u32 * pmax, u32 searchup)
{
	u32 fid;
	u32 vid;
	int res;
	struct cpufreq_freqs freqs;

	if (find_match(preq, pmin, pmax, searchup, &fid, &vid))
		return 1;

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
	u32 reqfreq = targfreq / 1000;
	u32 minfreq = pol->min / 1000;
	u32 maxfreq = pol->max / 1000;

	if (ppst == 0) {
		printk(KERN_ERR PFX "targ: ppst 0\n");
		return -ENODEV;
	}

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

	if (transition_frequency(&reqfreq, &minfreq, &maxfreq,
				 relation ==
				 CPUFREQ_RELATION_H ? SEARCH_UP : SEARCH_DOWN))
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
	u32 min = pol->min / 1000;
	u32 max = pol->max / 1000;
	u32 targ = min;
	int res;

	if (ppst == 0) {
		printk(KERN_ERR PFX "verify - ppst 0\n");
		return -ENODEV;
	}

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "failing verify, change pending bit set\n");
		return -EIO;
	}

	dprintk(KERN_DEBUG PFX
		"ver: cpu%d, min %d, max %d, cur %d, pol %d\n", pol->cpu,
		pol->min, pol->max, pol->cur, pol->policy);

	if (pol->cpu != 0) {
		printk(KERN_ERR PFX "verify - cpu not 0\n");
		return -ENODEV;
	}

#warning pol->policy is in undefined state here
	res = find_match(&targ, &min, &max,
			 pol->policy == CPUFREQ_POLICY_POWERSAVE ?
			 SEARCH_DOWN : SEARCH_UP, 0, 0);
	if (!res) {
		pol->min = min * 1000;
		pol->max = max * 1000;
	}
	return res;
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

	/* Take a crude guess here. */
	pol->cpuinfo.transition_latency = ((rvo + 8) * vstable * VST_UNITS_20US)
	    + (3 * (1 << irt) * 10);

	if (query_current_values_with_pending_wait())
		return -EIO;

	pol->cur = 1000 * find_freq_from_fid(currfid);
	dprintk(KERN_DEBUG PFX "policy current frequency %d kHz\n", pol->cur);

	/* min/max the cpu is capable of */
	pol->cpuinfo.min_freq = 1000 * find_freq_from_fid(ppst[0].fid);
	pol->cpuinfo.max_freq = 1000 * find_freq_from_fid(ppst[numps-1].fid);
	pol->min = 1000 * find_freq_from_fid(ppst[0].fid);
	pol->max = 1000 * find_freq_from_fid(ppst[batps - 1].fid);

	printk(KERN_INFO PFX "cpu_init done, current fid 0x%x, vid 0x%x\n",
	       currfid, currvid);

	return 0;
}

/* driver entry point for init */
static int __init
powernowk8_init(void)
{
	int rc;

	printk(KERN_INFO PFX VERSION "\n");

	if (check_supported_cpu() == 0)
		return -ENODEV;

	rc = find_psb_table();
	if (rc)
		return rc;

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "powernowk8_init fail, change pending bit set\n");
		kfree(ppst);
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
	kfree(ppst);
}

MODULE_AUTHOR("Paul Devriendt <paul.devriendt@amd.com>");
MODULE_DESCRIPTION("AMD Athlon 64 and Opteron processor frequency driver.");
MODULE_LICENSE("GPL");

module_init(powernowk8_init);
module_exit(powernowk8_exit);
