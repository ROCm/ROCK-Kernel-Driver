#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/hardirq.h>
#include <asm/pmac_feature.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/cputable.h>
#include <asm/time.h>

#undef DEBUG_FREQ

extern void low_choose_750fx_pll(int pll);
extern void low_sleep_handler(void);
extern void openpic_sleep_save_intrs(void);
extern void openpic_sleep_restore_intrs(void);
extern void enable_kernel_altivec(void);
extern void enable_kernel_fp(void);

static unsigned int low_freq;
static unsigned int hi_freq;
static unsigned int cur_freq;
static int cpufreq_uses_pmu;

#define PMAC_CPU_LOW_SPEED	1
#define PMAC_CPU_HIGH_SPEED	0

static inline void
wakeup_decrementer(void)
{
	set_dec(tb_ticks_per_jiffy);
	/* No currently-supported powerbook has a 601,
	 * so use get_tbl, not native
	 */
	last_jiffy_stamp(0) = tb_last_stamp = get_tbl();
}

#ifdef DEBUG_FREQ
static inline void
debug_calc_bogomips(void)
{
	/* This will cause a recalc of bogomips and display the
	 * result. We backup/restore the value to avoid affecting the
	 * core cpufreq framework's own calculation.
	 */
	extern void calibrate_delay(void);

	unsigned long save_lpj = loops_per_jiffy;
	calibrate_delay();
	loops_per_jiffy = save_lpj;
}
#endif

/* Switch CPU speed under 750FX CPU control
 */
static int __pmac
cpu_750fx_cpu_speed(int low_speed)
{
#ifdef DEBUG_FREQ
	printk(KERN_DEBUG "HID1, before: %x\n", mfspr(SPRN_HID1));	
#endif
	low_choose_750fx_pll(low_speed);
#ifdef DEBUG_FREQ
	printk(KERN_DEBUG "HID1, after: %x\n", mfspr(SPRN_HID1));	
	debug_calc_bogomips();
#endif

	return 0;
}

/* Switch CPU speed under PMU control
 */
static int __pmac
pmu_set_cpu_speed(unsigned int low_speed)
{
	struct adb_request req;
	unsigned long save_l2cr;
	unsigned long save_l3cr;
	
#ifdef DEBUG_FREQ
	printk(KERN_DEBUG "HID1, before: %x\n", mfspr(SPRN_HID1));	
#endif
	/* Disable all interrupt sources on openpic */
	openpic_sleep_save_intrs();

	/* Make sure the PMU is idle */
	pmu_suspend();

	/* Make sure the decrementer won't interrupt us */
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	/* Make sure any pending DEC interrupt occuring while we did
	 * the above didn't re-enable the DEC */
	mb();
	asm volatile("mtdec %0" : : "r" (0x7fffffff));

	/* We can now disable MSR_EE */
	local_irq_disable();

	/* Giveup the FPU & vec */
	enable_kernel_fp();

#ifdef CONFIG_ALTIVEC
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_ALTIVEC)
		enable_kernel_altivec();
#endif /* CONFIG_ALTIVEC */

	/* Save & disable L2 and L3 caches */
	save_l3cr = _get_L3CR();	/* (returns -1 if not available) */
	save_l2cr = _get_L2CR();	/* (returns -1 if not available) */
	if (save_l3cr != 0xffffffff && (save_l3cr & L3CR_L3E) != 0)
		_set_L3CR(save_l3cr & 0x7fffffff);
	if (save_l2cr != 0xffffffff && (save_l2cr & L2CR_L2E) != 0)
		_set_L2CR(save_l2cr & 0x7fffffff);

	/* Send the new speed command. My assumption is that this command
	 * will cause PLL_CFG[0..3] to be changed next time CPU goes to sleep
	 */
	pmu_request(&req, NULL, 6, PMU_CPU_SPEED, 'W', 'O', 'O', 'F', low_speed);
	while (!req.complete)
		pmu_poll();
	
	pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,1,1);

	low_sleep_handler();
	
	pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,1,0);

	/* Restore L2 cache */
	if (save_l2cr != 0xffffffff && (save_l2cr & L2CR_L2E) != 0)
 		_set_L2CR(save_l2cr);
	/* Restore L3 cache */
	if (save_l3cr != 0xffffffff && (save_l3cr & L3CR_L3E) != 0)
 		_set_L3CR(save_l3cr);

	/* Restore userland MMU context */
	set_context(current->active_mm->context, current->active_mm->pgd);

#ifdef DEBUG_FREQ
	printk(KERN_DEBUG "HID1, after: %x\n", mfspr(SPRN_HID1));	
#endif

	/* Restore decrementer */
	wakeup_decrementer();

	/* Restore interrupts */
	openpic_sleep_restore_intrs();

	pmu_resume();

	/* Let interrupts flow again ... */
	local_irq_enable();

#ifdef DEBUG_FREQ
	debug_calc_bogomips();
#endif

	return 0;
}

static int __pmac
do_set_cpu_speed(int speed_mode)
{
	struct cpufreq_freqs    freqs;
	int rc;
	
	freqs.old = cur_freq;
	freqs.new = (speed_mode == PMAC_CPU_HIGH_SPEED) ? hi_freq : low_freq;
	freqs.cpu = CPUFREQ_ALL_CPUS;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	if (cpufreq_uses_pmu)
		rc = pmu_set_cpu_speed(speed_mode);
	else
		rc = cpu_750fx_cpu_speed(speed_mode);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	cur_freq = (speed_mode == PMAC_CPU_HIGH_SPEED) ? hi_freq : low_freq;

	return rc;
}

static int __pmac
pmac_cpufreq_verify(struct cpufreq_policy *policy)
{
	if (!policy)
		return -EINVAL;
		
	policy->cpu = 0; /* UP only */

	cpufreq_verify_within_limits(policy, low_freq, hi_freq);

	if ((policy->min > low_freq) && 
	    (policy->max < hi_freq))
		policy->max = hi_freq;

	return 0;
}

static int __pmac
pmac_cpufreq_setpolicy(struct cpufreq_policy *policy)
{
	int rc;
	
	if (!policy)
		return -EINVAL;
	if (policy->min > low_freq)
		rc = do_set_cpu_speed(PMAC_CPU_HIGH_SPEED);
	else if (policy->max < hi_freq)
		rc = do_set_cpu_speed(PMAC_CPU_LOW_SPEED);
	else if (policy->policy == CPUFREQ_POLICY_POWERSAVE)
		rc = do_set_cpu_speed(PMAC_CPU_LOW_SPEED);
	else
		rc = do_set_cpu_speed(PMAC_CPU_HIGH_SPEED);

	return rc;
}

unsigned int __pmac
pmac_get_one_cpufreq(int i)
{
	/* Supports only one CPU for now */
	return (i == 0) ? cur_freq : 0;
}


/* Currently, we support the following machines:
 * 
 *  - Titanium PowerBook 800 (PMU based, 667Mhz & 800Mhz)
 *  - Titanium PowerBook 500 (PMU based, 300Mhz & 500Mhz)
 *  - iBook2 500 (PMU based, 400Mhz & 500Mhz)
 *  - iBook2 700 (CPU based, 400Mhz & 700Mhz, support low voltage)
 */
static int __init
pmac_cpufreq_setup(void)
{	
	struct device_node	*cpunode;
	struct cpufreq_driver   *driver;
	u32			*value;
	int			has_freq_ctl = 0;
	int			rc;
	
	memset(&driver, 0, sizeof(driver));

	/* Assume only one CPU */
	cpunode = find_type_devices("cpu");
	if (!cpunode)
		goto out;

	/* Get current cpu clock freq */
	value = (u32 *)get_property(cpunode, "clock-frequency", NULL);
	if (!value)
		goto out;
	cur_freq = (*value) / 1000;

	/* Check for newer machines */
	if (machine_is_compatible("PowerBook3,4") ||
	    machine_is_compatible("PowerBook3,5") ||
	    machine_is_compatible("MacRISC3")) {
		value = (u32 *)get_property(cpunode, "min-clock-frequency", NULL);
		if (!value)
			goto out;
		low_freq = (*value) / 1000;
		/* The PowerBook G4 12" (PowerBook6,1) has an error in the device-tree
		 * here */
		if (low_freq < 100000)
			low_freq *= 10;
		
		value = (u32 *)get_property(cpunode, "max-clock-frequency", NULL);
		if (!value)
			goto out;
		hi_freq = (*value) / 1000;			
		has_freq_ctl = 1;
		cpufreq_uses_pmu = 1;
	}
	/* Else check for iBook2 500 */
	else if (machine_is_compatible("PowerBook4,1")) {
		/* We only know about 500Mhz model */
		if (cur_freq < 450000 || cur_freq > 550000)
			goto out;
		hi_freq = cur_freq;
		low_freq = 400000;
		has_freq_ctl = 1;
		cpufreq_uses_pmu = 1;
	}
	/* Else check for TiPb 500 */
	else if (machine_is_compatible("PowerBook3,2")) {
		/* We only know about 500Mhz model */
		if (cur_freq < 450000 || cur_freq > 550000)
			goto out;
		hi_freq = cur_freq;
		low_freq = 300000;
		has_freq_ctl = 1;
		cpufreq_uses_pmu = 1;
	}
	/* Else check for 750FX */
	else if (PVR_VER(mfspr(PVR)) == 0x7000) {
		if (get_property(cpunode, "dynamic-power-step", NULL) == NULL)
			goto out;	
		hi_freq = cur_freq;
		value = (u32 *)get_property(cpunode, "reduced-clock-frequency", NULL);
		if (!value)
			goto out;
		low_freq = (*value) / 1000;
		cpufreq_uses_pmu = 0;
		has_freq_ctl = 1;
	}
out:
	if (!has_freq_ctl)
		return -ENODEV;
	
	/* initialization of main "cpufreq" code*/
	driver = kmalloc(sizeof(struct cpufreq_driver) + 
			 NR_CPUS * sizeof(struct cpufreq_policy), GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->policy = (struct cpufreq_policy *) (driver + 1);

	driver->verify		= &pmac_cpufreq_verify;
	driver->setpolicy	= &pmac_cpufreq_setpolicy;
	driver->init		= NULL;
	driver->exit		= NULL;
	strncpy(driver->name, "powermac", CPUFREQ_NAME_LEN);

	driver->policy[0].cpu				= 0;
	driver->policy[0].cpuinfo.transition_latency	= CPUFREQ_ETERNAL;
	driver->policy[0].cpuinfo.min_freq		= low_freq;
	driver->policy[0].min				= low_freq;
	driver->policy[0].max				= cur_freq;
	driver->policy[0].cpuinfo.max_freq		= cur_freq;
	driver->policy[0].policy			= (cur_freq == low_freq) ? 
	    	CPUFREQ_POLICY_POWERSAVE : CPUFREQ_POLICY_PERFORMANCE;

	rc = cpufreq_register_driver(driver);
	if (rc)
		kfree(driver);
	return rc;
}

__initcall(pmac_cpufreq_setup);

