/*
 *  linux/arch/x86_64/nmi.c
 *
 *  NMI watchdog support on APIC systems
 *
 *  Started by Ingo Molnar <mingo@redhat.com>
 *
 *  Fixes:
 *  Mikael Pettersson	: AMD K7 support for local APIC NMI watchdog.
 *  Mikael Pettersson	: Power Management for local APIC NMI watchdog.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/kernel_stat.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/proto.h>
#include <asm/kdebug.h>

extern void default_do_nmi(struct pt_regs *);

unsigned int nmi_watchdog = NMI_LOCAL_APIC;
static unsigned int nmi_hz = HZ;
unsigned int nmi_perfctr_msr;	/* the MSR to reset in NMI handler */
int nmi_watchdog_disabled;

#define K7_EVNTSEL_ENABLE	(1 << 22)
#define K7_EVNTSEL_INT		(1 << 20)
#define K7_EVNTSEL_OS		(1 << 17)
#define K7_EVNTSEL_USR		(1 << 16)
#define K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING	0x76
#define K7_NMI_EVENT		K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING

#define P6_EVNTSEL0_ENABLE	(1 << 22)
#define P6_EVNTSEL_INT		(1 << 20)
#define P6_EVNTSEL_OS		(1 << 17)
#define P6_EVNTSEL_USR		(1 << 16)
#define P6_EVENT_CPU_CLOCKS_NOT_HALTED	0x79
#define P6_NMI_EVENT		P6_EVENT_CPU_CLOCKS_NOT_HALTED

/* Why is there no CPUID flag for this? */
static __init int cpu_has_lapic(void)
{
	switch (boot_cpu_data.x86_vendor) { 
	case X86_VENDOR_INTEL:
	case X86_VENDOR_AMD: 
		return boot_cpu_data.x86 >= 6; 
	/* .... add more cpus here or find a different way to figure this out. */	
	default:
		return 0;
	} 	
}

int __init check_nmi_watchdog (void)
{
	int counts[NR_CPUS];
	int cpu;

	if (nmi_watchdog == NMI_LOCAL_APIC && !cpu_has_lapic())  {
		nmi_watchdog = NMI_NONE;
		return -1; 
	}	

	printk(KERN_INFO "testing NMI watchdog ... ");

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		counts[cpu] = cpu_pda[cpu].__nmi_count; 
	local_irq_enable();
	mdelay((10*1000)/nmi_hz); // wait 10 ticks

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;
		if (cpu_pda[cpu].__nmi_count - counts[cpu] <= 5) {
			printk("CPU#%d: NMI appears to be stuck (%d)!\n", 
			       cpu,
			       cpu_pda[cpu].__nmi_count);
			return -1;
		}
	}
	printk("OK.\n");

	/* now that we know it works we can reduce NMI frequency to
	   something more reasonable; makes a difference in some configs */
	if (nmi_watchdog == NMI_LOCAL_APIC)
		nmi_hz = 1;

	return 0;
}

static int __init setup_nmi_watchdog(char *str)
{
	int nmi;

	get_option(&str, &nmi);

	if (nmi >= NMI_INVALID)
		return 0;
		nmi_watchdog = nmi;
	return 1;
}

__setup("nmi_watchdog=", setup_nmi_watchdog);

#ifdef CONFIG_PM

#include <linux/pm.h>

struct pm_dev *nmi_pmdev;

static void disable_apic_nmi_watchdog(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		wrmsr(MSR_K7_EVNTSEL0, 0, 0);
		break;
	case X86_VENDOR_INTEL:
		wrmsr(MSR_IA32_EVNTSEL0, 0, 0);
		break;
	}
}

static int nmi_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	switch (rqst) {
	case PM_SUSPEND:
		disable_apic_nmi_watchdog();
		break;
	case PM_RESUME:
		setup_apic_nmi_watchdog();
		break;
	}
	return 0;
}

struct pm_dev * set_nmi_pm_callback(pm_callback callback)
{
	apic_pm_unregister(nmi_pmdev);
	return apic_pm_register(PM_SYS_DEV, 0, callback);
}

void unset_nmi_pm_callback(struct pm_dev * dev)
{
	apic_pm_unregister(dev);
	nmi_pmdev = apic_pm_register(PM_SYS_DEV, 0, nmi_pm_callback);
}

static void nmi_pm_init(void)
{
	if (!nmi_pmdev)
		nmi_pmdev = apic_pm_register(PM_SYS_DEV, 0, nmi_pm_callback);
}

#define __pminit	/*empty*/

#else	/* CONFIG_PM */

static inline void nmi_pm_init(void) { }

#define __pminit	__init

#endif	/* CONFIG_PM */

/*
 * Activate the NMI watchdog via the local APIC.
 * Original code written by Keith Owens.
 */

static void setup_k7_watchdog(void)
{
	int i;
	unsigned int evntsel;

	/* XXX should check these in EFER */

	nmi_perfctr_msr = MSR_K7_PERFCTR0;

	for(i = 0; i < 4; ++i) {
		/* Simulator may not support it */
		if (checking_wrmsrl(MSR_K7_EVNTSEL0+i, 0UL))
			return;
		wrmsrl(MSR_K7_PERFCTR0+i, 0UL);
	}

	evntsel = K7_EVNTSEL_INT
		| K7_EVNTSEL_OS
		| K7_EVNTSEL_USR
		| K7_NMI_EVENT;

	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
	printk(KERN_INFO "watchdog: setting K7_PERFCTR0 to %08x\n", -(cpu_khz/nmi_hz*1000));
	wrmsr(MSR_K7_PERFCTR0, -(cpu_khz/nmi_hz*1000), -1);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= K7_EVNTSEL_ENABLE;
	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
}

void setup_apic_nmi_watchdog (void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 < 6)
			return;
		setup_k7_watchdog();
		break;
	default:
		return;
	}
	nmi_pm_init();
}

static spinlock_t nmi_print_lock = SPIN_LOCK_UNLOCKED;

/*
 * the best way to detect whether a CPU has a 'hard lockup' problem
 * is to check it's local APIC timer IRQ counts. If they are not
 * changing then that CPU has some problem.
 *
 * as these watchdog NMI IRQs are generated on every CPU, we only
 * have to check the current processor.
 *
 * since NMIs don't listen to _any_ locks, we have to be extremely
 * careful not to rely on unsafe variables. The printk might lock
 * up though, so we have to break up any console locks first ...
 * [when there will be more tty-related locks, break them up
 *  here too!]
 */

static unsigned int
	last_irq_sums [NR_CPUS],
	alert_counter [NR_CPUS];

void touch_nmi_watchdog (void)
{
	int i;

	/*
	 * Just reset the alert counters, (other CPUs might be
	 * spinning on locks we hold):
	 */
	for (i = 0; i < NR_CPUS; i++)
		alert_counter[i] = 0;
}

void nmi_watchdog_tick (struct pt_regs * regs, unsigned reason)
{
	if (nmi_watchdog_disabled)
		return;

	int sum, cpu = safe_smp_processor_id();

	sum = read_pda(apic_timer_irqs);

	if (last_irq_sums[cpu] == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		alert_counter[cpu]++;
		if (alert_counter[cpu] == 5*nmi_hz) {
			if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT) == NOTIFY_BAD) { 
				alert_counter[cpu] = 0; 
				return;
			} 
			spin_lock(&nmi_print_lock);
			/*
			 * We are in trouble anyway, lets at least try
			 * to get a message out.
			 */
			bust_spinlocks(1);
			printk("NMI Watchdog detected LOCKUP on CPU%d, registers:\n", cpu);
			show_registers(regs);
			printk("console shuts up ...\n");
			console_silent();
			spin_unlock(&nmi_print_lock);
			bust_spinlocks(0);
			do_exit(SIGSEGV);
		}
	} else {
		last_irq_sums[cpu] = sum;
		alert_counter[cpu] = 0;
	}
	if (nmi_perfctr_msr)
		wrmsr(nmi_perfctr_msr, -(cpu_khz/nmi_hz*1000), -1);
}

static int dummy_nmi_callback(struct pt_regs * regs, int cpu)
{
	return 0;
}
 
static nmi_callback_t nmi_callback = dummy_nmi_callback;
 
asmlinkage void do_nmi(struct pt_regs * regs, long error_code)
{
	int cpu = safe_smp_processor_id();

	init_tss[cpu].ist[NMI_STACK] -= 2048;	/* this shouldn't be needed. */	

	nmi_enter();

	add_pda(__nmi_count,1);

	if (!nmi_callback(regs, cpu))
		default_do_nmi(regs);
	
	nmi_exit();

	init_tss[cpu].ist[NMI_STACK] += 2048;
}

void set_nmi_callback(nmi_callback_t callback)
{
	nmi_callback = callback;
}

void unset_nmi_callback(void)
{
	nmi_callback = dummy_nmi_callback;
}
