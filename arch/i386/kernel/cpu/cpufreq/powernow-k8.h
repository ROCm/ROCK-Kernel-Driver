/*
 *   (c) 2003, 2004 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 */

/* processor's cpuid instruction support */
#define CPUID_PROCESSOR_SIGNATURE             1	/* function 1               */
#define CPUID_XFAM_MOD               0x0ff00ff0	/* xtended fam, fam + model */
#define ATHLON64_XFAM_MOD            0x00000f40	/* xtended fam, fam + model */
#define OPTERON_XFAM_MOD             0x00000f50	/* xtended fam, fam + model */
#define CPUID_GET_MAX_CAPABILITIES   0x80000000
#define CPUID_FREQ_VOLT_CAPABILITIES 0x80000007
#define P_STATE_TRANSITION_CAPABLE            6

#define MSR_FIDVID_CTL      0xc0010041
#define MSR_FIDVID_STAT     0xc0010042

/* control MSR - low part */
#define MSR_C_LO_INIT             0x00010000
#define MSR_C_LO_NEW_VID          0x00001f00
#define MSR_C_LO_NEW_FID          0x0000003f
#define MSR_C_LO_VID_SHIFT        8

/* control MSR - high part */
#define MSR_C_HI_STP_GNT_TO       0x000fffff
#define MSR_C_HI_STP_GNT_BENIGN   1

/* status MSR - low part */
#define MSR_S_LO_CHANGE_PENDING   0x80000000   /* cleared when completed */
#define MSR_S_LO_MAX_RAMP_VID     0x1f000000
#define MSR_S_LO_MAX_FID          0x003f0000
#define MSR_S_LO_START_FID        0x00003f00
#define MSR_S_LO_CURRENT_FID      0x0000003f

/* status MSR - high part */
#define MSR_S_HI_MAX_WORKING_VID  0x001f0000
#define MSR_S_HI_START_VID        0x00001f00
#define MSR_S_HI_CURRENT_VID      0x0000001f

/* fids (frequency identifiers) are arranged in 2 tables - lo and hi */
#define LO_FID_TABLE_TOP        6
#define HI_FID_TABLE_BOTTOM     8
#define LO_VCOFREQ_TABLE_TOP    1400  /* corresponding vco frequency values */
#define HI_VCOFREQ_TABLE_BOTTOM 1600

#define MIN_FREQ_RESOLUTION  200 /* fids jump by 2 matching freq jumps by 200 */
#define FSTEP                  2
#define KHZ                 1000

#define MAX_FID 0x2a	/* Spec only gives FID values as far as 5 GHz */
#define LEAST_VID 0x1e	/* Lowest (numerically highest) useful vid value */

#define MIN_FREQ 800	/* Min and max freqs, per spec */
#define MAX_FREQ 5000

#define INVALID_FID_MASK 0xffffffc1  /* not a valid fid if these bits are set */
#define INVALID_VID_MASK 0xffffffe0  /* not a valid vid if these bits are set */


#define STOP_GRANT_5NS    1 /* min memory access latency for voltage change   */
#define MAXIMUM_VID_STEPS 1 /* Current cpus only allow a single step of 25mV  */
#define VST_UNITS_20US   20 /* Voltage Stabilization Time is in units of 20us */

#define PLL_LOCK_CONVERSION (1000/5) /* ms to ns, then divide by clock period */

#ifdef DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

#define DFX KERN_DEBUG PFX
#define IFX KERN_INFO  PFX
#define EFX KERN_ERR   PFX

/* Return a frequency in MHz, given an input fid */
static inline u32 freq_from_fid(u8 fid)
{
 	return 800 + (fid * 100);
}

/* Return the vco fid for an input fid */
static inline u32 convert_fid_to_vfid(u8 fid)
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

	rdmsr(MSR_FIDVID_STAT, lo, hi);
	return lo & MSR_S_LO_CHANGE_PENDING ? 1 : 0;
}

static inline void count_off_irt(u8 irt)
{
	udelay((1 << irt) * 10);
}

static inline void count_off_vst(u8 vstable)
{
	udelay(vstable * VST_UNITS_20US);
}

static inline int
check_supported_cpu(void)
{
	struct cpuinfo_x86 *c = cpu_data;
	u32 eax, ebx, ecx, edx;

	if (c->x86_vendor != X86_VENDOR_AMD) {
#ifdef MODULE
		printk(KERN_INFO PFX "Not an AMD processor\n");
#endif
		return 0;
	}

	eax = cpuid_eax(CPUID_PROCESSOR_SIGNATURE);
	if ((eax & CPUID_XFAM_MOD) == ATHLON64_XFAM_MOD) {
		dprintk(KERN_DEBUG PFX "AMD Althon 64 Processor found\n");
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

	printk(KERN_INFO PFX "Found AMD64 processor (" VERSION ")\n");
	return 1;
}

struct pstate {    /* info on each performance state, per processor */
	u16 freq;  /* frequency is in megahertz */
	u8 fid;
	u8 vid;
	u8 irt;
	u8 rvo;
	u8 plllock;
	u8 vidmvs;
	u8 vstable;
	u8 pad1;
	u16 pad2;
};

struct cpu_power {
	int numps;
	int cvid;
	int cfid;
	struct pstate pst[0];
};

static int write_new_fid(struct cpu_power *perproc, u32 idx, u8 fid);
static inline int core_voltage_pre_transition(struct cpu_power *perproc, u32 idx, u8 rvid);

/*
 * Update the global current fid / vid values from the status msr. Returns 1
 * on error.
 */
static int query_current_values_with_pending_wait(struct cpu_power *perproc)
{
	u32 lo = MSR_S_LO_CHANGE_PENDING;
	u32 hi;
	u32 i = 0;

	while (lo & MSR_S_LO_CHANGE_PENDING) {
		if (i++ > 0x1000000) {
			printk(EFX "change pending stuck\n");
			return 1;
		}
		rdmsr(MSR_FIDVID_STAT, lo, hi);
	}
	perproc->cvid = hi & MSR_S_HI_CURRENT_VID;
	perproc->cfid = lo & MSR_S_LO_CURRENT_FID;
	return 0;
}

/* Write a new vid to the hardware */
static int write_new_vid(struct cpu_power *perproc, u8 vid)
{
	u32 lo;
	u8 savefid = perproc->cfid;

	if ((savefid & INVALID_FID_MASK) || (vid & INVALID_VID_MASK)) {
		printk(EFX "overflow on vid write\n");
		return 1;
	}

	lo = perproc->cfid | (vid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT;
	dprintk(DFX "cpu%d, writing vid %x, lo %x, hi %x\n",
		smp_processor_id(), vid, lo, STOP_GRANT_5NS);
	wrmsr(MSR_FIDVID_CTL, lo, STOP_GRANT_5NS);
	if (query_current_values_with_pending_wait(perproc))
		return 1;

	if (savefid != perproc->cfid) {
		printk(EFX "fid change on vid trans, old %x new %x\n",
		       savefid, perproc->cfid);
		return 1;
	}
	if (vid != perproc->cvid) {
		printk(EFX "vid trans failed, vid %x, cvid %x\n",
		       vid, perproc->cfid);
		return 1;
	}
	return 0;
}

/* Phase 2 */
static inline int core_frequency_transition(struct cpu_power *perproc, u32 idx, u8 reqfid)
{
	u8 vcoreqfid;
	u8 vcocurrfid;
	u8 vcofiddiff;
	u8 savevid = perproc->cvid;

	if ((reqfid < HI_FID_TABLE_BOTTOM)
	    && (perproc->cfid < HI_FID_TABLE_BOTTOM)) {
		printk(EFX "ph2 illegal lo-lo transition %x %x\n",
		       reqfid, perproc->cfid);
		return 1;
	}

	if (perproc->cfid == reqfid) {
		printk(EFX "ph2 null fid transition %x\n", reqfid );
		return 0;
	}

	dprintk(DFX "ph2 start%d, cfid %x, cvid %x, rfid %x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid, reqfid);

	vcoreqfid = convert_fid_to_vfid(reqfid);
	vcocurrfid = convert_fid_to_vfid(perproc->cfid);
	vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
						: vcoreqfid - vcocurrfid;

	while (vcofiddiff > FSTEP) {
		if (reqfid > perproc->cfid) {
			if (perproc->cfid > LO_FID_TABLE_TOP) {
				if (write_new_fid(perproc, idx,
						  perproc->cfid + FSTEP))
					return 1;
			} else {
				if (write_new_fid(perproc, idx, FSTEP +
				     convert_fid_to_vfid(perproc->cfid)))
					return 1;
			}
		} else {
			if (write_new_fid(perproc, idx,
					  perproc->cfid-FSTEP))
				return 1;
		}

		vcocurrfid = convert_fid_to_vfid(perproc->cfid);
		vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
						    : vcoreqfid - vcocurrfid;
	}
	if (write_new_fid(perproc, idx, reqfid))
		return 1;
	if (query_current_values_with_pending_wait(perproc))
		return 1;

	if (perproc->cfid != reqfid) {
		printk(EFX "ph2 mismatch, failed transn, curr %x, req %x\n",
		       perproc->cfid, reqfid);
		return 1;
	}

	if (savevid != perproc->cvid) {
		printk(EFX "ph2 vid changed, save %x, curr %x\n", savevid,
		       perproc->cvid);
		return 1;
	}

	dprintk(DFX "ph2 complete%d, currfid 0x%x, currvid 0x%x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid);
	return 0;
}

/* Phase 3 - core voltage transition flow ... jump to the final vid. */
static inline int core_voltage_post_transition(struct cpu_power *perproc, u32 idx, u8 reqvid)
{
	u8 savefid = perproc->cfid;
	u8 savereqvid = reqvid;

	dprintk(DFX "ph3 starting%d, cfid 0x%x, cvid 0x%x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid);

	if (reqvid != perproc->cvid) {
		if (write_new_vid(perproc, reqvid))
			return 1;

		if (savefid != perproc->cfid) {
			printk(EFX "ph3: bad fid change, save %x, curr %x\n",
			       savefid, perproc->cfid);
			return 1;
		}

		if (perproc->cvid != reqvid) {
			printk(EFX "ph3: failed vid trans\n, req %x, curr %x",
			       reqvid, perproc->cvid);
			return 1;
		}
	}
	if (query_current_values_with_pending_wait(perproc))
		return 1;

	if (savereqvid != perproc->cvid) {
		dprintk(EFX "ph3 failed, currvid 0x%x\n", perproc->cvid);
		return 1;
	}

	if (savefid != perproc->cfid) {
		dprintk(EFX "ph3 failed, currfid changed 0x%x\n",
			perproc->cfid);
		return 1;
	}

	dprintk(DFX "ph3 done%d, cfid 0x%x, cvid 0x%x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid);
	return 0;
}

/* Change the fid and vid, by the 3 phases. */
static inline int transition_fid_vid(struct cpu_power *perproc, u32 idx, u8 rfid, u8 rvid)
{
	if (core_voltage_pre_transition(perproc, idx, rvid))
		return 1;
	if (core_frequency_transition(perproc, idx, rfid))
		return 1;
	if (core_voltage_post_transition(perproc, idx, rvid))
		return 1;
	if (query_current_values_with_pending_wait(perproc))
		return 1;
	if ((rfid != perproc->cfid) || (rvid != perproc->cvid)) {
		printk(EFX "failed%d: req %x %x, curr %x %x\n",
		       smp_processor_id(), rfid, rvid,
		       perproc->cfid, perproc->cvid);
		return 1;
	}
	dprintk(IFX "transitioned%d: new fid 0x%x, vid 0x%x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid);
	return 0;
}
