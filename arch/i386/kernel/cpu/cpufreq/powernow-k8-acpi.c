/*
 *   (c) 2003, 2004 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See COPYING or
 *  http://www.gnu.org/licenses/gpl.html
 *
 *  This is the ACPI version of the cpu frequency driver. There is a
 *  less functional version of this driver that does not
 *  use ACPI, and also does not support SMP.
 *
 *  Support : paul.devriendt@amd.com
 *
 *  Based on the powernow-k7.c module written by Dave Jones.
 *  (c) 2003 Dave Jones <davej@codemonkey.org.uk> on behalf of SuSE Labs
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by AMD.
 *
 *  Valuable input gratefully received from Dave Jones, Pavel Machek, Dominik
 *  Brodowski, and others.
 *
 *  Processor information obtained from Chapter 9 (Power and Thermal Management)
 *  of the "BIOS and Kernel Developer's Guide for the AMD Athlon 64 and AMD
 *  Opteron Processors", available for download from www.amd.com
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/acpi.h>

#include <asm/msr.h>
#include <asm/io.h>
#include <asm/delay.h>

#define PFX "powernow-k8-acpi: "

#undef DEBUG
#define VERSION "Version 1.20.02a"
#include "powernow-k8.h"

/*
 * Each processor may have
 * a different number of entries in its array. I.e., processor 0 may have
 * 3 pstates, processor 1 may have 5 pstates.
 */

struct proc_pss {  /* the acpi _PSS structure */
	acpi_integer freq;
	acpi_integer pow;
	acpi_integer tlat;
	acpi_integer blat;
	acpi_integer cntl;
	acpi_integer stat;
};

#define PSS_FMT_STR "NNNNNN"

#define IRT_SHIFT      30
#define RVO_SHIFT      28
#define PLL_L_SHIFT    20
#define MVS_SHIFT      18
#define VST_SHIFT      11
#define VID_SHIFT       6
#define IRT_MASK        3
#define RVO_MASK        3
#define PLL_L_MASK   0x7f
#define MVS_MASK        3
#define VST_MASK     0x7f
#define VID_MASK     0x1f
#define FID_MASK     0x3f

#define POW_AC  0  /* The power supply states we care about - mains, battery, */
#define POW_BAT 1  /* or unknown, which presumably means that there is no     */
#define POW_UNK 2  /* acpi support for the psr object, so there is no battery.*/

#define POLLER_NOT_RUNNING 0  /* The state of the poller (which watches for   */
#define POLLER_RUNNING     1  /* power transitions). It is only running if we */
#define POLLER_UNLOAD      2  /* are on mains power, at a high frequency, and */
#define POLLER_DEAD        3  /* if there are battery restrictions.           */

static void start_ac_poller(int frompoller);
static int powernowk8_verify(struct cpufreq_policy *p);
static int powernowk8_target(struct cpufreq_policy *p, unsigned t, unsigned r);
static int __init powernowk8_cpu_init(struct cpufreq_policy *p);

static struct cpu_power **procs;    /* per processor data structure     */
static u32 rstps;                   /* pstates allowed restrictions     */
static u32 seenrst;                 /* remember old bat restrictions    */
static int pollflg;	            /* remember the state of the poller, protected by poll_sem */
static int acpierr;                 /* retain acpi error across walker  */
static acpi_handle ppch;	    /* handle of the ppc object         */
static acpi_handle psrh;            /* handle of the acpi power object  */
static DECLARE_MUTEX(fidvid_sem);   /* serialize freq changes           */
static DECLARE_MUTEX(poll_sem);     /* serialize poller state changes   */
static struct timer_list ac_timer;  /* timer for the poller             */

static struct cpufreq_driver cpufreq_amd64_driver = {
	.verify = powernowk8_verify,
	.target = powernowk8_target,
	.init = powernowk8_cpu_init,
	.name = "powernow-k8",
	.owner = THIS_MODULE,
};

static inline u32 kfreq_from_fid(u8 fid)
{
	return KHZ * freq_from_fid(fid);
}

static inline u32 fid_from_freq(u32 freq)
{
	return (freq - 800) / 100;
}


/* need to init the control msr to a safe value (for each cpu) */
static void fidvid_msr_init(void)
{
	u32 lo;
	u32 hi;
	u8 fid;
	u8 vid;

	rdmsr(MSR_FIDVID_STAT, lo, hi);
	vid = hi & MSR_S_HI_CURRENT_VID;
	fid = lo & MSR_S_LO_CURRENT_FID;
	lo = fid | (vid << MSR_C_LO_VID_SHIFT);
	hi = MSR_C_HI_STP_GNT_BENIGN;
	dprintk(DFX "cpu%d, init lo %x, hi %x\n", smp_processor_id(), lo, hi);
	wrmsr(MSR_FIDVID_CTL, lo, hi);
}

static int write_new_fid(struct cpu_power *perproc, u32 idx, u8 fid)
{
	u32 lo;
	u32 hi;
	struct pstate *pst;
	u8 savevid = perproc->cvid;

	if (idx >= perproc->numps) {
		printk(EFX "idx overflow fid write\n");
		return 1;
	}
	pst = &perproc->pst[idx];

	if ((fid & INVALID_FID_MASK) || (savevid & INVALID_VID_MASK)) {
		printk(EFX "overflow on fid write\n");
		return 1;
	}
	lo = fid | (savevid << MSR_C_LO_VID_SHIFT) | MSR_C_LO_INIT;
	hi = pst->plllock * PLL_LOCK_CONVERSION;
	dprintk(DFX "cpu%d, writing fid %x, lo %x, hi %x\n",
		smp_processor_id(), fid, lo, hi);
	wrmsr(MSR_FIDVID_CTL, lo, hi);
	if (query_current_values_with_pending_wait(perproc))
		return 1;
	count_off_irt(pst->irt);

	if (savevid != perproc->cvid) {
		printk(EFX "vid change on fid trans, old %x, new %x\n",
		       savevid, perproc->cvid);
		return 1;
	}
	if (perproc->cfid != fid) {
		printk(EFX "fid trans failed, targ %x, new %x\n",
		       fid, perproc->cfid);
		return 1;
	}
	return 0;
}

static int decrease_vid_code_by_step(struct cpu_power *perproc, u32 idx, u8 reqvid, u8 step)
{
	struct pstate *pst;

	if (idx >= perproc->numps) {
		printk(EFX "idx overflow vid step\n");
		return 1;
	}
	pst = &perproc->pst[idx];

	if (step == 0)  /* BIOS error if this is the case, but continue */
		step = 1;

	if ((perproc->cvid - reqvid) > step)
		reqvid = perproc->cvid - step;
	if (write_new_vid(perproc, reqvid))
		return 1;
	count_off_vst(pst->vstable);
	return 0;
}

static inline int core_voltage_pre_transition(struct cpu_power *perproc, u32 idx, u8 rvid)
{
	struct pstate *pst;
	u8 rvosteps;
	u8 savefid = perproc->cfid;

	pst = &perproc->pst[idx];

	rvosteps = pst->rvo;
	dprintk(DFX "ph1 start%d, cfid 0x%x, cvid 0x%x, rvid 0x%x, rvo %x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid, rvid, pst->rvo);

	while (perproc->cvid > rvid) {
		dprintk(DFX "ph1 curr %x, req vid %x\n",
			    perproc->cvid, rvid);
		if (decrease_vid_code_by_step(perproc, idx, rvid, pst->vidmvs))
			return 1;
	}

	while (rvosteps) {
		if (perproc->cvid == 0) {
			rvosteps = 0;
		} else {
			dprintk(DFX "ph1 changing vid for rvo, req 0x%x\n",
				perproc->cvid - 1);
			if (decrease_vid_code_by_step(perproc, idx,
						perproc->cvid - 1, 1))
				return 1;
			rvosteps--;
		}
	}
	if (query_current_values_with_pending_wait(perproc))
		return 1;

	if (savefid != perproc->cfid) {
		printk(EFX "ph1 err, cfid changed %x\n", perproc->cfid);
		return 1;
	}
	dprintk(DFX "ph1 done%d, cfid 0x%x, cvid 0x%x\n",
		smp_processor_id(),
		perproc->cfid, perproc->cvid);
	return 0;
}


/* evaluating this object tells us whether we are using mains or battery */
static inline int process_psr(acpi_handle objh)
{
	if (num_online_cpus() == 1) /* ignore BIOS claiming battery MP boxes */
		psrh = objh;
	return 0;
}

static inline void
extract_pss_package(struct pstate *pst, struct proc_pss *proc)
{
	pst->freq = proc->freq;
	pst->fid = proc->cntl & FID_MASK;
	pst->vid = (proc->cntl >> VID_SHIFT) & VID_MASK;
	pst->irt = (proc->cntl >> IRT_SHIFT) & IRT_MASK;
	pst->rvo = (proc->cntl >> RVO_SHIFT) & RVO_MASK;
	pst->plllock = (proc->cntl >> PLL_L_SHIFT) & PLL_L_MASK;
	pst->vidmvs = 1 << ((proc->cntl >> MVS_SHIFT) & MVS_MASK);
	pst->vstable = (proc->cntl >> VST_SHIFT) & VST_MASK;
}

/* per cpu perf states */
static int process_pss(acpi_handle objh, unsigned cpunumb)
{
	struct proc_pss proc;
	struct cpu_power *perproc;
	struct pstate *pst;
	u32 pstc;
	acpi_status rc;
	char pss_arr[1000];  /* big buffer on the stack rather than dyn alloc */
	struct acpi_buffer buf = { sizeof(pss_arr), pss_arr };
	unsigned int i;
	union acpi_object *obj;
	union acpi_object *data;
	struct acpi_buffer format = { sizeof(PSS_FMT_STR), PSS_FMT_STR };
	struct acpi_buffer state;

	dprintk(DFX "processing _PSS for cpu%d\n", cpunumb);

	if (procs[cpunumb]) {
		printk(EFX "duplicate cpu data in acpi _pss\n");
		return -ENODEV;
	}

	memset(pss_arr, 0, sizeof(pss_arr));
	rc = acpi_evaluate_object(objh, NULL, NULL, &buf);
	if (ACPI_FAILURE(rc)) {
		printk(EFX "evaluate pss failed: %x\n", rc);
		return -ENODEV;
	}

	obj = (union acpi_object *) &pss_arr[0];
	if (obj->package.type != ACPI_TYPE_PACKAGE) {
		printk(EFX "pss is not a package\n");
		return -ENODEV;
	}
	pstc = obj->package.count;
	if (pstc < 2) {
		printk(EFX "insufficient pstates (%d)\n", pstc);
		return -ENODEV;
	}

	i = sizeof(struct cpu_power) + (sizeof(struct pstate) * pstc);
	perproc = kmalloc(i, GFP_KERNEL);
	if (!perproc) {
		printk(EFX "perproc memory alloc failure\n");
		return -ENOMEM;
	}
	memset(perproc, 0, i);
	pst = &perproc->pst[0];
	perproc->numps = pstc;

	data = obj->package.elements;
	for (i = 0; i < pstc; i++) {
		if (data[i].package.type != ACPI_TYPE_PACKAGE) {
			printk(EFX "%d: type %d\n", i, data[i].package.type);
			kfree(perproc);
			return -ENODEV;
		}
		state.length = sizeof(struct proc_pss);
		state.pointer = &proc;
		rc = acpi_extract_package(&obj->package.elements[i],
					  &format, &state);
		if (rc) {
			printk(EFX "extract err %x\n", rc);
			kfree(perproc);
			return -ENODEV;
		}
		extract_pss_package(pst + i, &proc);
		if (pst[i].freq > MAX_FREQ) {
			printk(EFX "frequency out of range %x, stopping extract\n",
				pst[i].freq );
			perproc->numps = i;
			break;
		}
	}

	procs[cpunumb] = perproc;
	return 0;
}

static u32 query_ac(void)
{
	acpi_status rc;
	unsigned long state;

	if (psrh) {
		rc = acpi_evaluate_integer(psrh, NULL, NULL, &state);
		if (ACPI_SUCCESS(rc)) {
			if (state == 1)
				return POW_AC;
			else if (state == 0)
				return POW_BAT;
			else
				printk(EFX "psr state %lx\n", state);
		}
		else {
			printk(EFX "error %x evaluating psr\n", rc );
		}
	}
	return POW_UNK;
}

/* gives us the (optional) battery/thermal restrictions */
static int process_ppc(acpi_handle objh)
{
	acpi_status rc;
	unsigned long state;

	if (objh) {
		ppch = objh;
	} else {
		if (ppch) {
			objh = ppch;
		} else {
			rstps = 0;
			return 0;   
		}
	}

	if (num_online_cpus() > 1) {
		/* For future thermal support (next release?), rstps needs   */
		/* to be per processor, and handled for the SMP case. Later. */
		dprintk(EFX "ignoring attempt to restrict pstates for SMP\n");
	}
	else {
		rc = acpi_evaluate_integer(objh, NULL, NULL, &state);
		if (ACPI_SUCCESS(rc)) {
			rstps = state & 0x0f;
			//dprintk(DFX "pstate restrictions %x\n", rstps);
			if (!seenrst)
				seenrst = rstps;
		}
		else {
			rstps = 0;
			printk(EFX "error %x processing ppc\n", rc);
			return -ENODEV;
		}
	}
	return 0;
}

static int powernow_find_objects(acpi_handle objh, char *nspace)
{
	acpi_status rc;
	char name[80] = { '?', '\0' };
	struct acpi_buffer buf = { sizeof(name), name };
	unsigned cpuobj;
	unsigned len = strlen(nspace);
	unsigned dotoff = len + 1;
	unsigned objoff = len + 2;

	rc = acpi_get_name(objh, ACPI_FULL_PATHNAME, &buf);
	if (ACPI_SUCCESS(rc)) {
		if (!psrh) {
			if (!strcmp(name + strlen(name) - 4, "_PSR")) {
				dprintk(IFX "_psr found in %s\n", name);
				return process_psr(objh);
			}
		}

		if ((!(strncmp(name, nspace, len))) && (name[dotoff] == '.')) {
			dprintk(IFX "searching %s\n", nspace);
			cpuobj = name[len] - '0';
			dprintk(IFX "cpu%u, %s\n", cpuobj, name);
			if (cpuobj >= num_online_cpus()) {
				printk(EFX "cpu count mismatch: %d, %d\n",
                                       cpuobj, num_online_cpus());
				acpierr = -ENODEV;
				return 0;
			}

			if (!(strcmp(name + objoff, "_PSS"))) {
				dprintk(IFX "_pss found in %s\n", nspace);
				acpierr = process_pss(objh, cpuobj);
				return 0;
			} else if (!(strcmp(name + objoff, "_PPC"))) {
				dprintk(IFX "_ppc found in %s\n", nspace);
				acpierr = process_ppc(objh);
				return 0;
			}
		}
	}
	return 1;
}

static acpi_status
powernow_walker(acpi_handle objh, u32 nestl, void *ctx, void **wrc)
{
	int notfound = powernow_find_objects(objh, "\\_SB_.CPU");
	if (notfound)
		powernow_find_objects(objh, "\\_PR_.CPU");
	return AE_OK;
}

static inline int find_acpi_table(void)
{
	acpi_status rc;
	acpi_status wrc;
	void *pwrc = &wrc;
	unsigned i;
	unsigned j;
	struct pstate *pst;

	rc = acpi_subsystem_status();
	if (ACPI_FAILURE(rc)) {
		printk(EFX "acpi subsys rc: %x\n", rc);
		return -ENODEV;
	}
	rc = acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, 6,
				 powernow_walker, 0, &pwrc);
	if (rc)
		return rc;
	if (acpierr)
		return acpierr;

	for (i = 0; i < num_online_cpus(); i++) {
		if (procs[i]) {
			pst = (&procs[i]->pst[0]);
			for (j = 0; j < procs[i]->numps; j++)
				dprintk(IFX
			            "cpu%d: freq %d: fid %x, vid %x, irt %x, "
				    "rvo %x, plllock %x, vidmvs %x, vstbl %x\n",
				    i, pst[j].freq, pst[j].fid, pst[j].vid,
				    pst[j].irt, pst[j].rvo, pst[j].plllock,
				    pst[j].vidmvs, pst[j].vstable);
		} else {
			printk(EFX "Missing pstates for cpu%d\n", i);
			return -ENODEV;
		}
	}

	i = query_ac();
	dprintk(IFX "mains power %s\n", POW_AC == i ? "online"
		       : POW_BAT == i ? "offline" : "unknown");

	return 0;
}

/* destroy the global table of per processor data */
static void cleanup_procs(void)  
{
	unsigned i;
	if (procs)
		for (i = 0; i < num_online_cpus(); i++)
			kfree(procs[i]);
	kfree(procs);
}

static u8 find_closest_fid(u16 freq)
{
	freq += MIN_FREQ_RESOLUTION - 1;
	freq = (freq / MIN_FREQ_RESOLUTION) * MIN_FREQ_RESOLUTION;
	if (freq < MIN_FREQ)
		freq = MIN_FREQ;
	else if (freq > MAX_FREQ)
		freq = MAX_FREQ;
	return fid_from_freq(freq);
}

static int find_match(struct cpu_power *perproc, u16 *ptargfreq, u16 *pmin, u16 *pmax,
			u8 *pfid, u8 *pvid, u32 *idx)
{
	u32 availpstates = perproc->numps;
	u8 targfid = find_closest_fid(*ptargfreq);
	u8 minfid = find_closest_fid(*pmin);
	u8 maxfid = find_closest_fid(*pmax);
	u32 maxidx = 0;
	u32 minidx = availpstates - 1;
	u32 targidx = 0xffffffff;
	int i;
	struct pstate *pst = &perproc->pst[0];

	dprintk(DFX "find match: freq %d MHz (%x), min %d (%x), max %d (%x)\n",
		*ptargfreq, targfid, *pmin, minfid, *pmax, maxfid);

	/* restrict to permitted pstates (battery/thermal) */
        process_ppc(0);
	if (rstps > availpstates)
		rstps = 0;
	if (rstps && (POW_BAT == query_ac())) { /* not restricting for thermal */
		maxidx = availpstates - rstps;
		dprintk(DFX "bat: idx restr %d-%d\n", maxidx, minidx);
	}

	/* Restrict values to the frequency choices in the pst */
	if (minfid < pst[minidx].fid)
		minfid = pst[minidx].fid;
	if (maxfid > pst[maxidx].fid)
		maxfid = pst[maxidx].fid;

	/* Find appropriate pst index for the max fid */
	for (i = 0; i < (int) availpstates; i++) {
		if (maxfid <= pst[i].fid)
			maxidx = i;
	}

	/* Find appropriate pst index for the min fid */
	for (i = availpstates - 1; i >= 0; i--) {
		if (minfid >= pst[i].fid)
			minidx = i;
	}

	if (minidx < maxidx)
		minidx = maxidx;

	dprintk(DFX "minidx %d, maxidx %d\n", minidx, maxidx);

	/* Frequency ids are now constrained by limits matching PST entries */
	minfid = pst[minidx].fid;
	maxfid = pst[maxidx].fid;

	/* Limit the target frequency to these limits */
	if (targfid < minfid)
		targfid = minfid;
	else if (targfid > maxfid)
		targfid = maxfid;

	/* Find the best target index into the PST, contrained by the range */
	for (i = minidx; i >= (int) maxidx; i--) {
		if (targfid >= pst[i].fid)
			targidx = i;
	}

	if (targidx == 0xffffffff) {
		printk(EFX "could not find target\n");
		return 1;
	}
	*pmin = freq_from_fid(minfid);
	*pmax = freq_from_fid(maxfid);
	*ptargfreq = freq_from_fid(pst[targidx].fid);
	*pfid = pst[targidx].fid;
	*pvid = pst[targidx].vid;
	*idx = targidx;
	return 0;
}

static inline int
transition_frequency(struct cpu_power *perproc, u16 *preq, u16 *pmin, u16 *pmax)
{
	u32 idx;
	int res;
	struct cpufreq_freqs freqs;
	u8 fid;
	u8 vid;

	if (find_match(perproc, preq, pmin, pmax, &fid, &vid, &idx))
		return 1;
	dprintk(DFX "matched idx %x: fid 0x%x vid 0x%x\n", idx, fid, vid);

	if (query_current_values_with_pending_wait(perproc))
		return 1;
	if ((perproc->cvid == vid) && (perproc->cfid == fid)) {
		dprintk(DFX "targ matches curr (fid %x, vid %x)\n", fid, vid);
		return 0;
	}

	if ((fid < HI_FID_TABLE_BOTTOM)
	    && (perproc->cfid < HI_FID_TABLE_BOTTOM)) {
		printk(EFX "ignoring change in lo freq table: %x to %x\n",
		       perproc->cfid, fid);
		return 1;
	}

	dprintk(DFX "cpu%d to fid %x vid %x\n", smp_processor_id(), fid, vid);

	freqs.cpu = smp_processor_id();
	freqs.old = freq_from_fid(perproc->cfid);
	freqs.new = freq_from_fid(fid);
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	down(&fidvid_sem);
	res = transition_fid_vid(perproc, idx, fid, vid);
	up(&fidvid_sem);

	freqs.new = freq_from_fid(perproc->cfid);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return res;
}

static int need_poller(void)   /* if running at a freq only allowed for a/c */
{
	struct cpu_power *perproc = procs[0];
	struct pstate *pst = &perproc->pst[0];
        u32 maxidx;

        if (num_online_cpus() > 1)
		return 0;

        process_ppc(0);
	if (rstps > perproc->numps)
		return 0;
	maxidx = perproc->numps - rstps;
	pst += maxidx;
	if (rstps && (perproc->cfid > pst->fid ))
		return 1;
	return 0;
}

/* transition if needed, restart if needed */
static void ac_poller(unsigned long x)  
{
	int pow;
	struct cpu_power *perproc = procs[0];
	struct pstate *pst = &perproc->pst[0];
        u32 maxidx = perproc->numps - rstps;
	u16 rf = pst[maxidx].freq;
	u16 minfreq = pst[perproc->numps-1].freq;
	u16 maxfreq = pst[maxidx].freq;

	down(&poll_sem);
	if (pollflg == POLLER_UNLOAD) {
		pollflg = POLLER_DEAD;
		up(&poll_sem);
		return;
	}
        process_ppc(0);
	if (rstps > perproc->numps) {
		pollflg = POLLER_NOT_RUNNING;
		up(&poll_sem);
		return;
	}
	if (pollflg != POLLER_RUNNING)
		panic("k8-pn pollflg %x\n", pollflg);
	up(&poll_sem);

	pow = query_ac();
	if (POW_AC == pow) {                 /* only poll if cpu is at high */
		if (need_poller()) {         /* speed and on mains power    */
			start_ac_poller(1);
			return;
		}
	}
	else if (POW_BAT == pow) {
		if (need_poller()) {
			dprintk(DFX "battery emergency transition\n" );
                        transition_frequency(perproc, &rf, &minfreq, &maxfreq);
		}
	}
	down(&poll_sem);
	pollflg = POLLER_NOT_RUNNING;
	up(&poll_sem);
}

static void start_ac_poller(int frompoller)
{
	down(&poll_sem);
	if ( (frompoller) || (pollflg == POLLER_NOT_RUNNING) ) {
		init_timer(&ac_timer);
		ac_timer.function = ac_poller;
		ac_timer.data = 0;
		ac_timer.expires = jiffies + HZ;
		add_timer( &ac_timer );
		pollflg = POLLER_RUNNING;
		//dprintk(DFX "timer added\n");
	}
	up(&poll_sem);
}

static int powernowk8_target(struct cpufreq_policy *pol, unsigned targfreq,
				unsigned relation)
{
	cpumask_t oldmask = CPU_MASK_ALL;
	unsigned thiscpu;
	int rc = 0;
	u16 reqfreq = (u16)(targfreq / KHZ);
	u16 minfreq = (u16)(pol->min / KHZ);
	u16 maxfreq = (u16)(pol->max / KHZ);
	struct cpu_power *perproc;
	u8 checkfid;
	u8 checkvid;

	dprintk(IFX "proc mask %lx, current %d\n", current->cpus_allowed,
		smp_processor_id());
	dprintk(DFX "targ%d: %d kHz, min %d, max %d, relation %d\n",
		pol->cpu, targfreq, pol->min, pol->max, relation);

	if (pol->cpu > num_online_cpus()) {
		printk(EFX "targ out of range\n");
		return -ENODEV;
	}
	if (procs == NULL) {
		printk(EFX "targ: procs 0\n");
		return -ENODEV;
	}
	perproc = procs[pol->cpu];
	if (perproc == NULL) {
		printk(EFX "targ: perproc 0 for cpu%d\n", pol->cpu);
		return -ENODEV;
	}

        thiscpu = smp_processor_id();
	if (num_online_cpus()>1) {
		oldmask = current->cpus_allowed;
		set_cpus_allowed(current, cpumask_of_cpu(pol->cpu));
		schedule();
	}

	/* from this point, do not exit without restoring preempt and cpu */
	preempt_disable();

	dprintk(DFX "targ cpu %d, curr cpu %d (mask %lx)\n", pol->cpu,
		    smp_processor_id(),	current->cpus_allowed);

	checkfid = perproc->cfid;
	checkvid = perproc->cvid;
	if (query_current_values_with_pending_wait(perproc)) {
		printk(EFX "drv targ fail: change pending bit set\n");
		rc = -EIO;
		goto targ_exit;
	}
	dprintk(DFX "targ%d: curr fid %x, vid %x\n", smp_processor_id(),
		perproc->cfid, perproc->cvid);
	if ((checkvid != perproc->cvid)
	    || (checkfid != perproc->cfid)) {
		printk(EFX "error - out of sync, fid %x %x, vid %x %x\n",
		       checkfid, perproc->cfid, checkvid,
		       perproc->cvid);
	}

	if (transition_frequency(perproc, &reqfreq, &minfreq, &maxfreq)) {
		printk(EFX "transition frequency failed\n");
		rc = -EIO;
		goto targ_exit;
	}

	pol->cur = kfreq_from_fid(perproc->cfid);

targ_exit:
	preempt_enable_no_resched();
	if (num_online_cpus()>1) {
		set_cpus_allowed(current, cpumask_of_cpu(thiscpu));
		schedule();			  
		set_cpus_allowed(current, oldmask);
	}
        if ((POW_AC == query_ac()) && (need_poller()))
		start_ac_poller(0);
	return rc;
}

static int powernowk8_verify(struct cpufreq_policy *pol)
{
	u16 min = (u16)(pol->min / KHZ);
	u16 max = (u16)(pol->max / KHZ);
	u16 targ = min;
	struct cpu_power *perproc;
	int res;
	u32 idx;
	u8 fid;
	u8 vid;

	dprintk(DFX "ver: cpu%d, min %d, max %d, cur %d, pol %d\n",
		pol->cpu, pol->min, pol->max, pol->cur, pol->policy);

	if (pol->cpu > num_online_cpus()) {
		printk(EFX "ver cpu out of range\n");
		return -ENODEV;
	}
	if (procs == NULL) {
		printk(EFX "verify - procs 0\n");
		return -ENODEV;
	}
	perproc = procs[pol->cpu];
	if (perproc == NULL) {
		printk(EFX "verify: perproc 0 for cpu%d\n", pol->cpu);
		return -ENODEV;
	}

	res = find_match(perproc, &targ, &min, &max, &fid, &vid, &idx);
	if (!res) {
		pol->min = min * KHZ;
		pol->max = max * KHZ;
	}
	return res;
}

static int __init powernowk8_cpu_init(struct cpufreq_policy *pol)
{
	struct cpu_power *perproc = procs[smp_processor_id()];
	struct pstate *pst = &perproc->pst[0];

	pol->governor = CPUFREQ_DEFAULT_GOVERNOR;
	pol->cpuinfo.transition_latency =             /* crude guess */
		((pst[0].rvo + 8) * pst[0].vstable * VST_UNITS_20US)
		+ (3 * (1 << pst[0].irt) * 10);

	pol->cur = kfreq_from_fid(perproc->cfid);
	dprintk(DFX "policy cfreq %d kHz\n", pol->cur);

	/* min/max this cpu is capable of */
	pol->cpuinfo.min_freq =kfreq_from_fid(pst[perproc->numps-1].fid);
	pol->cpuinfo.max_freq = kfreq_from_fid(pst[0].fid);
	pol->min = pol->cpuinfo.min_freq;
	pol->max = pol->cpuinfo.max_freq;
	return 0;
}

#ifdef CONFIG_SMP
static void smp_k8_init( void *retval )
{
	struct cpu_power *perproc = procs[smp_processor_id()];
	int *rc = (int *)retval;
	rc += smp_processor_id();

	dprintk(DFX "smp init on %d\n", smp_processor_id());
	if (check_supported_cpu() == 0) {
		*rc = -ENODEV;
		return;
	}
	if (pending_bit_stuck()) {
		printk(EFX "change pending bit set\n");
		*rc = -EIO;
		return;
	}
	if (query_current_values_with_pending_wait(perproc)) {
		*rc = -EIO;
		return;
	}
	fidvid_msr_init();
}
#endif

static int __init powernowk8_init(void)
{
	int smprc[num_online_cpus()];
	int rc;
	int i;

	printk(IFX VERSION " (%d cpus)\n", num_online_cpus());

	if (check_supported_cpu() == 0)
		return -ENODEV;
	if (pending_bit_stuck()) {
		printk(EFX "change pending bit set\n");
		return -EIO;
	}

	procs = kmalloc(sizeof(u8 *) * num_online_cpus(), GFP_KERNEL);
	if (!procs) {
		printk(EFX "procs memory alloc failure\n");
		return -ENOMEM;
	}
	memset(procs, 0, sizeof(u8 *) * num_online_cpus());
	rc = find_acpi_table();
	if (rc) {
		cleanup_procs();
		return rc;
	}

	for (i=0; i<num_online_cpus(); i++) {
		if (procs[i]==0) {
			printk(EFX "Error procs 0 for %d\n", i);
			cleanup_procs();
			return -ENOMEM;
		}
	}

	if (query_current_values_with_pending_wait(procs[0])) {
		cleanup_procs();
		return -EIO;
	}
	fidvid_msr_init();
        if (num_online_cpus() > 1) {
		memset(smprc, 0, sizeof(smprc));
		smp_call_function(smp_k8_init, &smprc, 0, 1);
		for (i=0; i<num_online_cpus(); i++) {
			if (smprc[i]) {
				cleanup_procs();
				return smprc[i];
			}
		}
	}
	for (i=0; i<num_online_cpus(); i++)
		dprintk(DFX "at init%d : fid %x vid %x\n", i,
			procs[i]->cfid, procs[i]->cvid );

	return cpufreq_register_driver(&cpufreq_amd64_driver);
}

static void __exit powernowk8_exit(void)
{
	int pollwait = num_online_cpus() == 1 ? 1 : 0;
	struct cpu_power *perproc = procs[0];
	struct pstate *pst = &perproc->pst[0];
        u32 maxidx = perproc->numps - seenrst;
	u16 rf = pst[maxidx].freq;
	u16 minfreq = pst[perproc->numps-1].freq;
	u16 maxfreq = pst[maxidx].freq;

	dprintk(IFX "powernowk8_exit, pollflg=%x\n", pollflg);

	/* do not unload the driver until we are certain the poller is gone */
	while (pollwait) {
		down(&poll_sem);
		if ((pollflg == POLLER_RUNNING) || (pollflg == POLLER_UNLOAD)) {
			pollflg = POLLER_UNLOAD;
		}
		else {
			pollflg = POLLER_DEAD;
			pollwait = 0;
		}
		up(&poll_sem);
		schedule();
	}

	/* need to be on a battery frequency when the module is unloaded */
	pst += maxidx;
	if (seenrst && (perproc->cfid > pst->fid )) {
		if (POW_BAT == query_ac()) {
			dprintk(DFX "unload emergency transition\n" );
                        transition_frequency(perproc, &rf, &minfreq, &maxfreq);
		}
	}

	cpufreq_unregister_driver(&cpufreq_amd64_driver);
	cleanup_procs();
}

MODULE_AUTHOR("Paul Devriendt <paul.devriendt@amd.com>");
MODULE_DESCRIPTION("AMD Athlon 64 and Opteron processor frequency driver.");
MODULE_LICENSE("GPL");

module_init(powernowk8_init);
module_exit(powernowk8_exit);
