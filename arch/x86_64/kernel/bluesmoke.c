
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/config.h>
#include <linux/irq.h>
#include <asm/processor.h> 
#include <asm/system.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

static int mce_disabled __initdata = 0;

static int banks;

/*
 * If we get an MCE, we don't know what state the caches/TLB's are
 * going to be in, so we throw them all away.
 */
static void inline flush_all (void)
{   
	__asm__ __volatile__ ("invd": : );
	__flush_tlb();
}

/*
 *	P4/Xeon Thermal transition interrupt handler
 */

static void intel_thermal_interrupt(struct pt_regs *regs)
{
#ifdef CONFIG_X86_LOCAL_APIC
	u32 l, h;
	unsigned int cpu = smp_processor_id();

	ack_APIC_irq();

	rdmsr(MSR_IA32_THERM_STATUS, l, h);
	if (l & 1) {
		printk(KERN_EMERG "CPU#%d: Temperature above threshold\n", cpu);
		printk(KERN_EMERG "CPU#%d: Running in modulated clock mode\n", cpu);
	} else {
		printk(KERN_INFO "CPU#%d: Temperature/speed normal\n", cpu);
	}
#endif
}

static void unexpected_thermal_interrupt(struct pt_regs *regs)
{	
	printk(KERN_ERR "CPU#%d: Unexpected LVT TMR interrupt!\n", smp_processor_id());
}

/*
 *	Thermal interrupt handler for this CPU setup
 */

static void (*vendor_thermal_interrupt)(struct pt_regs *regs) = unexpected_thermal_interrupt;

asmlinkage void smp_thermal_interrupt(struct pt_regs regs)
{
	vendor_thermal_interrupt(&regs);
}

/* P4/Xeon Thermal regulation detect and init */

static void __init intel_init_thermal(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_LOCAL_APIC
	u32 l, h;
	unsigned int cpu = smp_processor_id();

	/* Thermal monitoring */
	if (!test_bit(X86_FEATURE_ACPI, &c->x86_capability))
		return;	/* -ENODEV */
	
	/* Clock modulation */
	if (!test_bit(X86_FEATURE_ACC, &c->x86_capability))
		return;	/* -ENODEV */

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	/* first check if its enabled already, in which case there might
	 * be some SMM goo which handles it, so we can't even put a handler
	 * since it might be delivered via SMI already -zwanem.
	 */

	if (l & (1<<3)) {
		printk(KERN_DEBUG "CPU#%d: Thermal monitoring already enabled\n", cpu);
	} else {
		wrmsr(MSR_IA32_MISC_ENABLE, l | (1<<3), h);
		printk(KERN_INFO "CPU#%d: Thermal monitoring enabled\n", cpu);
	}

	/* check wether a vector already exists */	
	l = apic_read(APIC_LVTTHMR);
	if (l & 0xff) {
		printk(KERN_DEBUG "CPU#%d: Thermal LVT already handled\n", cpu);
		return; /* -EBUSY */
	}
	
	wrmsr(MSR_IA32_MISC_ENABLE, l | (1<<3), h);
	printk(KERN_INFO "CPU#%d: Thermal monitoring enabled\n", cpu);
	
	/* The temperature transition interrupt handler setup */
	l = THERMAL_APIC_VECTOR;		/* our delivery vector */
	l |= (APIC_DM_FIXED | APIC_LVT_MASKED);	/* we'll mask till we're ready */
	apic_write_around(APIC_LVTTHMR, l);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT, l | 0x3 , h);

	/* ok we're good to go... */
	vendor_thermal_interrupt = intel_thermal_interrupt;
	l = apic_read(APIC_LVTTHMR);
	apic_write_around(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);

	return;
#endif
}

/*
 *	Machine Check Handler For PII/PIII
 */

static void intel_machine_check(struct pt_regs * regs, long error_code)
{
	int recover=1;
	u32 alow, ahigh, high, low;
	u32 mcgstl, mcgsth;
	int i;
	
	flush_all();

	rdmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
	if(mcgstl&(1<<0))	/* Recoverable ? */
		recover=0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x\n", smp_processor_id(), mcgsth, mcgstl);
	
	for(i=0;i<banks;i++)
	{
		rdmsr(MSR_IA32_MC0_STATUS+i*4,low, high);
		if(high&(1<<31))
		{
			if(high&(1<<29))
				recover|=1;
			if(high&(1<<25))
				recover|=2;
			printk(KERN_EMERG "Bank %d: %08x%08x", i, high, low);
			high&=~(1<<31);
			if(high&(1<<27))
			{
				rdmsr(MSR_IA32_MC0_MISC+i*4, alow, ahigh);
				printk("[%08x%08x]", ahigh, alow);
			}
			if(high&(1<<26))
			{
				rdmsr(MSR_IA32_MC0_ADDR+i*4, alow, ahigh);
				printk(" at %08x%08x", ahigh, alow);
			}
			printk("\n");
			/* Clear it */
			wrmsr(MSR_IA32_MC0_STATUS+i*4, 0UL, 0UL);
			/* Serialize */
			wmb();
		}
	}
	
	if(recover&2)
		panic("CPU context corrupt");
	if(recover&1)
		panic("Unable to continue");
	printk(KERN_EMERG "Attempting to continue.\n");
	mcgstl&=~(1<<2);
	wrmsr(MSR_IA32_MCG_STATUS,mcgstl, mcgsth);
}

/*
 *	Handle unconfigured int18 (should never happen)
 */

static void unexpected_machine_check(struct pt_regs * regs, long error_code)
{ 
	printk(KERN_ERR "CPU#%d: Unexpected int18 (Machine Check).\n", smp_processor_id());
} 

/*
 *	Call the installed machine check handler for this CPU setup.
 */ 
 
static void (*machine_check_vector)(struct pt_regs *, long error_code) = unexpected_machine_check;

asmlinkage void do_machine_check(struct pt_regs * regs, long error_code)
{
	machine_check_vector(regs, error_code);
}


#ifdef CONFIG_X86_MCE_NONFATAL
struct timer_list mce_timer;

static void mce_checkregs (unsigned int cpu)
{
	u32 low, high;
	int i;

	if (cpu!=smp_processor_id())
		BUG();

	for (i=0; i<banks; i++) {
		rdmsr(MSR_IA32_MC0_STATUS+i*4, low, high);

		if ((low | high) != 0) {
			flush_all();
			printk (KERN_EMERG "MCE: The hardware reports a non fatal, correctable incident occured on CPU %d.\n", smp_processor_id());
			printk (KERN_EMERG "Bank %d: %08x%08x\n", i, high, low);

			/* Scrub the error so we don't pick it up in 5 seconds time. */
			wrmsr(MSR_IA32_MC0_STATUS+i*4, 0UL, 0UL);

			/* Serialize */
			wmb();
		}
	}

	/* Refresh the timer. */
	mce_timer.expires = jiffies + 5 * HZ;
	add_timer (&mce_timer);
}

static void mce_timerfunc (unsigned long data)
{
	int i;

	for (i=0; i<smp_num_cpus; i++) {
		if (i == smp_processor_id())
			mce_checkregs(i);
		else
			smp_call_function (mce_checkregs, i, 1, 1);
	}
}
#endif


/*
 *	Set up machine check reporting for processors with Intel style MCE
 */

static void __init intel_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int i;
	static int done;
	
	/*
	 *	Check for MCE support
	 */

	if( !test_bit(X86_FEATURE_MCE, c->x86_capability) )
		return;	
	
	/*
	 *	Check for PPro style MCA
	 */
	 		
	if( !test_bit(X86_FEATURE_MCA, c->x86_capability) )
		return;
		
	/* Ok machine check is available */
	
	machine_check_vector = intel_machine_check;
	wmb();
	
	if(done==0)
		printk(KERN_INFO "Intel machine check architecture supported.\n");
	rdmsr(MSR_IA32_MCG_CAP, l, h);
	if(l&(1<<8))	/* Control register present ? */
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);
	banks = l&0xff;

	/* Don't enable bank 0 on intel P6 cores, it goes bang quickly. */
	if (c->x86_vendor == X86_VENDOR_INTEL && c->x86 == 6) {
		for(i=1; i<banks; i++)
			wrmsr(MSR_IA32_MC0_CTL+4*i, 0xffffffff, 0xffffffff);
	} else {
		for(i=0; i<banks; i++)
		wrmsr(MSR_IA32_MC0_CTL+4*i, 0xffffffff, 0xffffffff);
	}

	for(i=0; i<banks; i++)
		wrmsr(MSR_IA32_MC0_STATUS+4*i, 0x0, 0x0);

	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Intel machine check reporting enabled on CPU#%d.\n", smp_processor_id());
	
	intel_init_thermal(c);

	done=1;
}

/*
 *	This has to be run for each processor
 */

void __init mcheck_init(struct cpuinfo_x86 *c)
{
	if(mce_disabled==1)
		return;
		
	switch(c->x86_vendor)
	{
		case X86_VENDOR_AMD:
			if(c->x86 == 6 || c->x86 == 15) {
				intel_mcheck_init(c);
#ifdef CONFIG_X86_MCE_NONFATAL
				/* Set the timer to check for non-fatal errors every 5 seconds */
				init_timer (&mce_timer);
				mce_timer.expires = jiffies + 5 * HZ;
				mce_timer.data = 0;
				mce_timer.function = &mce_timerfunc;
				add_timer (&mce_timer);
#endif
			}
			break;

		case X86_VENDOR_INTEL:
			intel_mcheck_init(c);
			break;

		default:
			break;
	}
}

static int __init mcheck_disable(char *str)
{
	mce_disabled = 1;
	return 0;
}

static int __init mcheck_enable(char *str)
{
	mce_disabled = -1;
	return 0;
}

__setup("nomce", mcheck_disable);
__setup("mce", mcheck_enable);
