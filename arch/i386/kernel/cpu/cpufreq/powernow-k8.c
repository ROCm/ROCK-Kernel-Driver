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
 *  Valuable input gratefully received from Dave Jones, Pavel Machek,
 *  Dominik Brodowski, and others.
 *  Processor information obtained from Chapter 9 (Power and Thermal Management)
 *  of the "BIOS and Kernel Developer's Guide for the AMD Athlon 64 and AMD
 *  Opteron Processors" available for download from www.amd.com
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

/* serialize freq changes  */
static DECLARE_MUTEX(fidvid_sem);

static struct powernow_k8_data *powernow_data[NR_CPUS];

/* Return a frequency in MHz, given an input fid */
static u32 find_freq_from_fid(u32 fid)
{
	return 800 + (fid * 100);
}

/* Return a frequency in KHz, given an input fid */
static u32 find_khz_freq_from_fid(u32 fid)
{
	return 1000 * find_freq_from_fid(fid);
}

/* Return a voltage in miliVolts, given an input vid */
static u32 find_millivolts_from_vid(struct powernow_k8_data *data, u32 vid)
{
	return 1550-vid*25;
}

/* Return the vco fid for an input fid */
static u32 convert_fid_to_vco_fid(u32 fid)
{
	if (fid < HI_FID_TABLE_BOTTOM) {
		return 8 + (2 * fid);
	} else {
		return fid;
	}
}

/*
 * Return 1 if the pending bit is set. Unless we just instructed the processor
 * to transition to a new state, seeing this bit set is really bad news.
 */
static int pending_bit_stuck(void)
{
	u32 lo, hi;

	rdmsr(MSR_FIDVID_STATUS, lo, hi);
	return lo & MSR_S_LO_CHANGE_PENDING ? 1 : 0;
}

/*
 * Update the global current fid / vid values from the status msr.
 * Returns 1 on error.
 */
static int query_current_values_with_pending_wait(struct powernow_k8_data *data)
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

	data->currvid = hi & MSR_S_HI_CURRENT_VID;
	data->currfid = lo & MSR_S_LO_CURRENT_FID;

	return 0;
}

/* the isochronous relief time */
static void count_off_irt(struct powernow_k8_data *data)
{
	udelay((1 << data->irt) * 10);
	return;
}

/* the voltage stabalization time */
static void count_off_vst(struct powernow_k8_data *data)
{
	udelay(data->vstable * VST_UNITS_20US);
	return;
}

/* need to init the control msr to a safe value (for each cpu) */
static void fidvid_msr_init(void)
{
	u32 lo, hi;
	u8 fid, vid;

	rdmsr(MSR_FIDVID_STATUS, lo, hi);
	vid = hi & MSR_S_HI_CURRENT_VID;
	fid = lo & MSR_S_LO_CURRENT_FID;
	lo = fid | (vid << MSR_C_LO_VID_SHIFT);
	hi = MSR_C_HI_STP_GNT_BENIGN;
	dprintk(PFX "cpu%d, init lo %x, hi %x\n", smp_processor_id(), lo, hi);
	wrmsr(MSR_FIDVID_CTL, lo, hi);
}


/* write the new fid value along with the other control fields to the msr */
static int write_new_fid(struct powernow_k8_data *data, u32 fid)
{
	u32 lo;
	u32 savevid = data->currvid;

	if ((fid & INVALID_FID_MASK) || (data->currvid & INVALID_VID_MASK)) {
		printk(KERN_ERR PFX "internal error - overflow on fid write\n");
		return 1;
	}

	lo = fid | (data->currvid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT_FID_VID;

	dprintk(KERN_DEBUG PFX "writing fid %x, lo %x, hi %x\n",
		fid, lo, data->plllock * PLL_LOCK_CONVERSION);

	wrmsr(MSR_FIDVID_CTL, lo, data->plllock * PLL_LOCK_CONVERSION);

	if (query_current_values_with_pending_wait(data))
		return 1;

	count_off_irt(data);

	if (savevid != data->currvid) {
		printk(KERN_ERR PFX "vid change on fid trans, old %x, new %x\n",
		       savevid, data->currvid);
		return 1;
	}

	if (fid != data->currfid) {
		printk(KERN_ERR PFX "fid trans failed, fid %x, curr %x\n", fid,
		        data->currfid);
		return 1;
	}

	return 0;
}

/* Write a new vid to the hardware */
static int write_new_vid(struct powernow_k8_data *data, u32 vid)
{
	u32 lo;
	u32 savefid = data->currfid;

	if ((data->currfid & INVALID_FID_MASK) || (vid & INVALID_VID_MASK)) {
		printk(KERN_ERR PFX "internal error - overflow on vid write\n");
		return 1;
	}

	lo = data->currfid | (vid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT_FID_VID;

	dprintk(KERN_DEBUG PFX "writing vid %x, lo %x, hi %x\n",
		vid, lo, STOP_GRANT_5NS);

	wrmsr(MSR_FIDVID_CTL, lo, STOP_GRANT_5NS);

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (savefid != data->currfid) {
		printk(KERN_ERR PFX "fid changed on vid trans, old %x new %x\n",
		       savefid, data->currfid);
		return 1;
	}

	if (vid != data->currvid) {
		printk(KERN_ERR PFX "vid trans failed, vid %x, curr %x\n", vid,
				data->currvid);
		return 1;
	}

	return 0;
}

/*
 * Reduce the vid by the max of step or reqvid.
 * Decreasing vid codes represent increasing voltages:
 * vid of 0 is 1.550V, vid of 0x1e is 0.800V, vid of 0x1f is off.
 */
static int decrease_vid_code_by_step(struct powernow_k8_data *data, u32 reqvid, u32 step)
{
	if ((data->currvid - reqvid) > step)
		reqvid = data->currvid - step;

	if (write_new_vid(data, reqvid))
		return 1;

	count_off_vst(data);

	return 0;
}

/* Change the fid and vid, by the 3 phases. */
static int transition_fid_vid(struct powernow_k8_data *data, u32 reqfid, u32 reqvid)
{
	if (core_voltage_pre_transition(data, reqvid))
		return 1;

	if (core_frequency_transition(data, reqfid))
		return 1;

	if (core_voltage_post_transition(data, reqvid))
		return 1;

	if (query_current_values_with_pending_wait(data))
		return 1;

	if ((reqfid != data->currfid) || (reqvid != data->currvid)) {
		printk(KERN_ERR PFX "failed (cpu%d): req 0x%x 0x%x, curr 0x%x 0x%x\n",
				smp_processor_id(),
				reqfid, reqvid, data->currfid, data->currvid);
		return 1;
	}

	dprintk(KERN_INFO PFX "transitioned (cpu%d): new fid 0x%x, vid 0x%x\n",
		smp_processor_id(), data->currfid, data->currvid);

	return 0;
}

/* Phase 1 - core voltage transition ... setup voltage */
static int core_voltage_pre_transition(struct powernow_k8_data *data, u32 reqvid)
{
	u32 rvosteps = data->rvo;
	u32 savefid = data->currfid;

	dprintk(KERN_DEBUG PFX
		"ph1 (cpu%d): start, currfid 0x%x, currvid 0x%x, reqvid 0x%x, rvo 0x%x\n",
		smp_processor_id();
		data->currfid, data->currvid, reqvid, data->rvo);

	while (data->currvid > reqvid) {
		dprintk(KERN_DEBUG PFX "ph1: curr 0x%x, req vid 0x%x\n",
			data->currvid, reqvid);
		if (decrease_vid_code_by_step(data, reqvid, data->vidmvs))
			return 1;
	}

	while (rvosteps > 0) {
		if (data->currvid == 0) {
			rvosteps = 0;
		} else {
			dprintk(KERN_DEBUG PFX
				"ph1: changing vid for rvo, req 0x%x\n",
				data->currvid - 1);
			if (decrease_vid_code_by_step(data, data->currvid - 1, 1))
				return 1;
			rvosteps--;
		}
	}

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (savefid != data->currfid) {
		printk(KERN_ERR PFX "ph1 err, currfid changed 0x%x\n", data->currfid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph1 complete, currfid 0x%x, currvid 0x%x\n",
		data->currfid, data->currvid);

	return 0;
}

/* Phase 2 - core frequency transition */
static int core_frequency_transition(struct powernow_k8_data *data, u32 reqfid)
{
	u32 vcoreqfid;
	u32 vcocurrfid;
	u32 vcofiddiff;
	u32 savevid = data->currvid;

	if ((reqfid < HI_FID_TABLE_BOTTOM) && (data->currfid < HI_FID_TABLE_BOTTOM)) {
		printk(KERN_ERR PFX "ph2: illegal lo-lo transition 0x%x 0x%x\n",
			reqfid, data->currfid);
		return 1;
	}

	if (data->currfid == reqfid) {
		printk(KERN_ERR PFX "ph2 null fid transition 0x%x\n", data->currfid);
		return 0;
	}

	dprintk(KERN_DEBUG PFX
		"ph2 (cpu%d): starting, currfid 0x%x, currvid 0x%x, reqfid 0x%x\n",
		smp_processor_id(),
		data->currfid, data->currvid, reqfid);

	vcoreqfid = convert_fid_to_vco_fid(reqfid);
	vcocurrfid = convert_fid_to_vco_fid(data->currfid);
	vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
	    : vcoreqfid - vcocurrfid;

	while (vcofiddiff > 2) {
		if (reqfid > data->currfid) {
			if (data->currfid > LO_FID_TABLE_TOP) {
				if (write_new_fid(data, data->currfid + 2)) {
					return 1;
				}
			} else {
				if (write_new_fid
				    (data, 2 + convert_fid_to_vco_fid(data->currfid))) {
					return 1;
				}
			}
		} else {
			if (write_new_fid(data, data->currfid - 2))
				return 1;
		}

		vcocurrfid = convert_fid_to_vco_fid(data->currfid);
		vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
		    : vcoreqfid - vcocurrfid;
	}

	if (write_new_fid(data, reqfid))
		return 1;

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (data->currfid != reqfid) {
		printk(KERN_ERR PFX
			"ph2: mismatch, failed fid transition, curr 0x%x, req 0x%x\n",
			data->currfid, reqfid);
		return 1;
	}

	if (savevid != data->currvid) {
		printk(KERN_ERR PFX "ph2: vid changed, save %x, curr %x\n",
			savevid, data->currvid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph2 complete, currfid 0x%x, currvid 0x%x\n",
		data->currfid, data->currvid);

	return 0;
}

/* Phase 3 - core voltage transition flow ... jump to the final vid. */
static int core_voltage_post_transition(struct powernow_k8_data *data, u32 reqvid)
{
	u32 savefid = data->currfid;
	u32 savereqvid = reqvid;

	dprintk(KERN_DEBUG PFX "ph3 (cpu%d): starting, currfid 0x%x, currvid 0x%x\n",
		smp_processor_id(),
		data->currfid, data->currvid);

	if (reqvid != data->currvid) {
		if (write_new_vid(data, reqvid))
			return 1;

		if (savefid != data->currfid) {
			printk(KERN_ERR PFX
			       "ph3: bad fid change, save %x, curr %x\n",
			       savefid, data->currfid);
			return 1;
		}

		if (data->currvid != reqvid) {
			printk(KERN_ERR PFX
			       "ph3: failed vid transition\n, req %x, curr %x",
			       reqvid, data->currvid);
			return 1;
		}
	}

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (savereqvid != data->currvid) {
		dprintk(KERN_ERR PFX "ph3 failed, currvid 0x%x\n", data->currvid);
		return 1;
	}

	if (savefid != data->currfid) {
		dprintk(KERN_ERR PFX "ph3 failed, currfid changed 0x%x\n",
			data->currfid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "ph3 complete, currfid 0x%x, currvid 0x%x\n",
		data->currfid, data->currvid);

	return 0;
}

static int check_supported_cpu(unsigned int cpu)
{
	cpumask_t oldmask = CPU_MASK_ALL;
	u32 eax, ebx, ecx, edx;
	unsigned int rc = 0;

	oldmask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	schedule();

	if (smp_processor_id() != cpu) {
		printk(KERN_ERR "limiting to cpu %u failed\n", cpu);
		goto out;
	}

	if (current_cpu_data.x86_vendor != X86_VENDOR_AMD)
		goto out;

	eax = cpuid_eax(CPUID_PROCESSOR_SIGNATURE);
	if ((eax & CPUID_XFAM_MOD) == ATHLON64_XFAM_MOD) {
		dprintk(KERN_DEBUG PFX "AMD Althon 64 Processor found\n");
	} else if ((eax & CPUID_XFAM_MOD) == OPTERON_XFAM_MOD) {
		dprintk(KERN_DEBUG PFX "AMD Opteron Processor found\n");
	} else {
		printk(KERN_INFO PFX
		       "AMD Athlon 64 or AMD Opteron processor required\n");
		goto out;
	}

	eax = cpuid_eax(CPUID_GET_MAX_CAPABILITIES);
	if (eax < CPUID_FREQ_VOLT_CAPABILITIES) {
		printk(KERN_INFO PFX
		       "No frequency change capabilities detected\n");
		goto out;
	}

	cpuid(CPUID_FREQ_VOLT_CAPABILITIES, &eax, &ebx, &ecx, &edx);
	if ((edx & P_STATE_TRANSITION_CAPABLE) != P_STATE_TRANSITION_CAPABLE) {
		printk(KERN_INFO PFX "Power state transitions not supported\n");
		goto out;
	}

	rc = 1;

out:
	set_cpus_allowed(current, oldmask);
	schedule();
	return rc;

}

static int check_pst_table(struct powernow_k8_data *data, struct pst_s *pst, u8 maxvid)
{
	unsigned int j;
	u8 lastfid = 0xff;

	for (j = 0; j < data->numps; j++) {
		if (pst[j].vid > LEAST_VID) {
			printk(KERN_ERR PFX "vid %d invalid : 0x%x\n", j, pst[j].vid);
			return -EINVAL;
		}
		if (pst[j].vid < data->rvo) {	/* vid + rvo >= 0 */
			printk(KERN_ERR BFX "0 vid exceeded with pstate %d\n", j);
			return -ENODEV;
		}
		if (pst[j].vid < maxvid + data->rvo) {	/* vid + rvo >= maxvid */
			printk(KERN_ERR BFX "maxvid exceeded with pstate %d\n", j);
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

static void print_basics(struct powernow_k8_data *data)
{
	int j;
	for (j = 0; j < data->numps; j++) {
		printk(KERN_INFO PFX "   %d : fid 0x%x (%d MHz), vid 0x%x (%d mV)\n", j,
			data->powernow_table[j].index & 0xff,
			data->powernow_table[j].frequency/1000,
			data->powernow_table[j].index >> 8,
			find_millivolts_from_vid(data, data->powernow_table[j].index >> 8));
	}
	if (data->batps)
		printk(KERN_INFO PFX "Only %d pstates on battery\n", data->batps);
}

static int fill_powernow_table(struct powernow_k8_data *data, struct pst_s *pst, u8 maxvid)
{
	struct cpufreq_frequency_table *powernow_table;
	unsigned int j;

	if (data->batps) {    /* use ACPI support to get full speed on mains power */
		printk(KERN_WARNING PFX "Only %d pstates usable (use ACPI driver for full range\n", data->batps);
		data->numps = data->batps;
	}

	for ( j=1; j<data->numps; j++ ) {
		if (pst[j-1].fid >= pst[j].fid) {
			printk(KERN_ERR PFX "PST out of sequence\n");
			return -EINVAL;
		}
	}

	if (data->numps < 2) {
		printk(KERN_ERR PFX "no p states to transition\n");
		return -ENODEV;
	}
                                                                                                    
	if (check_pst_table(data, pst, maxvid))
		return -EINVAL;

	powernow_table = kmalloc((sizeof(struct cpufreq_frequency_table)
		* (data->numps + 1)), GFP_KERNEL);
	if (!powernow_table) {
		printk(KERN_ERR PFX "powernow_table memory alloc failure\n");
		return -ENOMEM;
	}

	for (j = 0; j < data->numps; j++) {
		powernow_table[j].index = pst[j].fid; /* lower 8 bits */
		powernow_table[j].index |= (pst[j].vid << 8); /* upper 8 bits */
		powernow_table[j].frequency = find_khz_freq_from_fid(pst[j].fid);
	}
	powernow_table[data->numps].frequency = CPUFREQ_TABLE_END;
	powernow_table[data->numps].index = 0;

	if (query_current_values_with_pending_wait(data)) {
		kfree(powernow_table);
		return -EIO;
	}

	dprintk(KERN_INFO PFX "cfid %x, cvid %x\n", data->currfid, data->currvid);
	data->powernow_table = powernow_table;
	print_basics(data);

	for (j = 0; j < data->numps; j++)
		if ((pst[j].fid==data->currfid) && (pst[j].vid==data->currvid))
			return 0;

	dprintk(KERN_ERR PFX "currfid/vid do not match PST, ignoring\n");
	return 0;
}

/* Find and validate the PSB/PST table in BIOS. */
static int find_psb_table(struct powernow_k8_data *data)
{
	struct cpufreq_frequency_table *powernow_table;
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

		data->vstable = psb->voltagestabilizationtime;
		dprintk(KERN_INFO PFX "voltage stabilization time: %d(*20us)\n", data->vstable);

		dprintk(KERN_DEBUG PFX "flags2: 0x%x\n", psb->flags2);
		data->rvo = psb->flags2 & 3;
		data->irt = ((psb->flags2) >> 2) & 3;
		mvs = ((psb->flags2) >> 4) & 3;
		data->vidmvs = 1 << mvs;
		data->batps = ((psb->flags2) >> 6) & 3;

		printk(KERN_INFO PFX "voltage stable in %d usec", data->vstable * 20);
		if (data->batps)
			printk(", only %d lowest states on battery", data->batps);
		printk(", ramp voltage offset: %d", data->rvo);
		printk(", isochronous relief time: %d", data->irt);
		printk(", maximum voltage step: %d\n", mvs);

		dprintk(KERN_DEBUG PFX "numpst: 0x%x\n", psb->numpst);
		if (psb->numpst != 1) {
			printk(KERN_ERR BFX "numpst must be 1\n");
			return -ENODEV;
		}

		dprintk(KERN_DEBUG PFX "cpuid: 0x%x\n", psb->cpuid);

		data->plllock = psb->plllocktime;
		printk(KERN_INFO PFX "pll lock time: 0x%x, ", data->plllock);

		maxvid = psb->maxvid;
		printk("maxfid 0x%x (%d MHz), maxvid 0x%x\n", 
		       psb->maxfid, find_freq_from_fid(psb->maxfid), maxvid);

		data->numps = psb->numpstates;
		if (data->numps < 2) {
			printk(KERN_ERR BFX "no p states to transition\n");
			return -ENODEV;
		}

		if (data->batps == 0) {
			data->batps = data->numps;
		} else if (data->batps > data->numps) {
			printk(KERN_ERR BFX "batterypstates > numpstates\n");
			data->batps = data->numps;
		} else {
			printk(KERN_ERR PFX
			       "Restricting operation to %d p-states\n", data->batps);
			printk(KERN_ERR PFX
			       "Check for an updated driver to access all "
			       "%d p-states\n", data->numps);
		}

		if (data->numps <= 1) {
			printk(KERN_ERR PFX "only 1 p-state to transition\n");
			return -ENODEV;
		}

		pst = (struct pst_s *) (psb + 1);
		if (check_pst_table(data, pst, maxvid))
			return -EINVAL;

		powernow_table = kmalloc((sizeof(struct cpufreq_frequency_table) * (data->numps + 1)), GFP_KERNEL);
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

		for (j = 0; j < data->numps; j++) {
			powernow_table[j].frequency = find_freq_from_fid(powernow_table[j].index & 0xff)*1000;
			printk(KERN_INFO PFX "   %d : fid 0x%x (%d MHz), vid 0x%x\n", j,
			       powernow_table[j].index & 0xff,
			       powernow_table[j].frequency/1000,
			       powernow_table[j].index >> 8);
		}

		powernow_table[data->numps].frequency = CPUFREQ_TABLE_END;
		powernow_table[data->numps].index = 0;

		if (query_current_values_with_pending_wait(data)) {
			kfree(powernow_table);
			return -EIO;
		}

		printk(KERN_INFO PFX "currfid 0x%x (%d MHz), currvid 0x%x\n",
		       data->currfid, find_freq_from_fid(data->currfid), data->currvid);

		for (j = 0; j < data->numps; j++)
			if ((pst[j].fid==data->currfid) && (pst[j].vid==data->currvid))
				return 0;

		printk(KERN_ERR BFX "currfid/vid do not match PST, ignoring\n");
		return 0;
	}

	printk(KERN_ERR BFX "no PSB\n");
	return -ENODEV;
}

/* Take a frequency, and issue the fid/vid transition command */
static int transition_frequency(struct powernow_k8_data *data, unsigned int index)
{
	u32 fid;
	u32 vid;
	int res;
	struct cpufreq_freqs freqs;

	/* fid are the lower 8 bits of the index we stored into
	 * the cpufreq frequency table in find_psb_table, vid are 
	 * the upper 8 bits.
	 */

	fid = data->powernow_table[index].index & 0xFF;
	vid = (data->powernow_table[index].index & 0xFF00) >> 8;

	dprintk(KERN_DEBUG PFX "table matched fid 0x%x, giving vid 0x%x\n",
		fid, vid);

	if (query_current_values_with_pending_wait(data))
		return 1;

	if ((data->currvid == vid) && (data->currfid == fid)) {
		dprintk(KERN_DEBUG PFX
			"target matches current values (fid 0x%x, vid 0x%x)\n",
			fid, vid);
		return 0;
	}

	if ((fid < HI_FID_TABLE_BOTTOM) && (data->currfid < HI_FID_TABLE_BOTTOM)) {
		printk(KERN_ERR PFX
		       "ignoring illegal change in lo freq table-%x to %x\n",
		       data->currfid, fid);
		return 1;
	}

	dprintk(KERN_DEBUG PFX "changing to fid 0x%x, vid 0x%x\n", fid, vid);

	freqs.cpu = data->cpu;

	freqs.old = find_freq_from_fid(data->currfid);
	freqs.new = find_freq_from_fid(fid);
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	down(&fidvid_sem);
	res = transition_fid_vid(data, fid, vid);
	up(&fidvid_sem);

	freqs.new = find_freq_from_fid(data->currfid);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return res;
}

/* Driver entry point to switch to the target frequency */
static int powernowk8_target(struct cpufreq_policy *pol, unsigned targfreq, unsigned relation)
{
	struct powernow_k8_data *data = powernow_data[pol->cpu];
	u32 checkfid = data->currfid;
	u32 checkvid = data->currvid;
	unsigned int newstate;

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "drv targ fail: change pending bit set\n");
		return -EIO;
	}

	dprintk(KERN_DEBUG PFX "targ: %d kHz, min %d, max %d, relation %d\n",
		targfreq, pol->min, pol->max, relation);

	if (query_current_values_with_pending_wait(data))
		return -EIO;

	dprintk(KERN_DEBUG PFX "targ: curr fid 0x%x, vid 0x%x\n",
		data->currfid, data->currvid);

	if ((checkvid != data->currvid) || (checkfid != data->currfid)) {
		printk(KERN_ERR PFX
		       "error - out of sync, fid 0x%x 0x%x, vid 0x%x 0x%x\n",
		       checkfid, data->currfid, checkvid, data->currvid);
	}

	if (cpufreq_frequency_table_target(pol, data->powernow_table, targfreq, relation, &newstate))
		return -EINVAL;
	
	if (transition_frequency(data, newstate)) {
		printk(KERN_ERR PFX "transition frequency failed\n");
		return 1;
	}

	pol->cur = 1000 * find_freq_from_fid(data->currfid);

	return 0;
}

/* Driver entry point to verify the policy and range of frequencies */
static int powernowk8_verify(struct cpufreq_policy *pol)
{
	struct powernow_k8_data *data = powernow_data[pol->cpu];

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "failing verify, change pending bit set\n");
		return -EIO;
	}

	return cpufreq_frequency_table_verify(pol, data->powernow_table);
}

/* per CPU init entry point to the driver */
static int __init powernowk8_cpu_init(struct cpufreq_policy *pol)
{
	struct powernow_k8_data *data;
	int rc;

	data = kmalloc(sizeof(struct powernow_k8_data), GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR PFX "unable to alloc powernow_k8_data");
		return -ENOMEM;
	}
	memset(data,0,sizeof(struct powernow_k8_data));

	data->cpu = pol->cpu;

	if (pol->cpu != 0) {
		printk(KERN_ERR PFX "init not cpu 0\n");
		kfree(data);
		return -ENODEV;
	}

	pol->governor = CPUFREQ_DEFAULT_GOVERNOR;

	rc = find_psb_table(data);
	if (rc) {
		kfree(data);
		return -ENODEV;
	}

	if (pending_bit_stuck()) {
		printk(KERN_ERR PFX "failing init, change pending bit set\n");
		goto err_out;
	}

	/* Take a crude guess here. 
	 * That guess was in microseconds, so multply with 1000 */
	pol->cpuinfo.transition_latency = (((data->rvo + 8) * data->vstable * VST_UNITS_20US)
	    + (3 * (1 << data->irt) * 10)) * 1000;

	if (query_current_values_with_pending_wait(data))
		return -EIO;

	pol->cur = 1000 * find_freq_from_fid(data->currfid);
	dprintk(KERN_DEBUG PFX "policy current frequency %d kHz\n", pol->cur);

	/* min/max the cpu is capable of */
	if (cpufreq_frequency_table_cpuinfo(pol, data->powernow_table)) {
		printk(KERN_ERR PFX "invalid powernow_table\n");
		kfree(data->powernow_table);
		kfree(data);
		return -EINVAL;
	}

	cpufreq_frequency_table_get_attr(data->powernow_table, pol->cpu);

	printk(KERN_INFO PFX "cpu_init done, current fid 0x%x, vid 0x%x\n",
	       data->currfid, data->currvid);

	powernow_data[pol->cpu] = data;

	return 0;

err_out:
	kfree(data);
	return -ENODEV;
}

static int __exit powernowk8_cpu_exit (struct cpufreq_policy *pol)
{
	struct powernow_k8_data *data = powernow_data[pol->cpu];

	if (!data)
		return -EINVAL;

	cpufreq_frequency_table_put_attr(pol->cpu);

	kfree(data->powernow_table);
	kfree(data);

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
static int __init powernowk8_init(void)
{
	unsigned int i, supported_cpus = 0;

	for (i=0; i<NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		if (check_supported_cpu(i))
			supported_cpus++;
	}

	if (supported_cpus == num_online_cpus()) {
		printk(KERN_INFO PFX "Found %d AMD Athlon 64 / Opteron processors (" VERSION ")\n",
			supported_cpus);
		return cpufreq_register_driver(&cpufreq_amd64_driver);
	}

	return -ENODEV;
}

/* driver entry point for term */
static void __exit powernowk8_exit(void)
{
	dprintk(KERN_INFO PFX "exit\n");

	cpufreq_unregister_driver(&cpufreq_amd64_driver);
}

MODULE_AUTHOR("Paul Devriendt <paul.devriendt@amd.com>");
MODULE_DESCRIPTION("AMD Athlon 64 and Opteron processor frequency driver.");
MODULE_LICENSE("GPL");

module_init(powernowk8_init);
module_exit(powernowk8_exit);
