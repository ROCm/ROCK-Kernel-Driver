/*
 * arch/i386/kernel/bluesmoke.c - x86 Machine Check Exception Reporting
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/config.h>
#include <linux/irq.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>

#include <asm/processor.h> 
#include <asm/system.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/hardirq.h>

#ifdef CONFIG_X86_MCE

/* as supported by the P4/Xeon family */
struct intel_mce_extended_msrs {
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
	u32 esi;
	u32 edi;
	u32 ebp;
	u32 esp;
	u32 eflags;
	u32 eip;
	/* u32 *reserved[]; */
};

static int mce_disabled __initdata = 0;

static int mce_num_extended_msrs = 0;
static int banks;


#ifdef CONFIG_X86_MCE_P4THERMAL
/*
 *	P4/Xeon Thermal transition interrupt handler
 */

static void intel_thermal_interrupt(struct pt_regs *regs)
{
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
	irq_enter();
	vendor_thermal_interrupt(&regs);
	irq_exit();
}

/* P4/Xeon Thermal regulation detect and init */

static void __init intel_init_thermal(struct cpuinfo_x86 *c)
{
	u32 l, h;
	unsigned int cpu = smp_processor_id();

	/* Thermal monitoring */
	if (!cpu_has(c, X86_FEATURE_ACPI))
		return;	/* -ENODEV */

	/* Clock modulation */
	if (!cpu_has(c, X86_FEATURE_ACC))
		return;	/* -ENODEV */

	/* first check if its enabled already, in which case there might
	 * be some SMM goo which handles it, so we can't even put a handler
	 * since it might be delivered via SMI already -zwanem.
	 */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	h = apic_read(APIC_LVTTHMR);
	if ((l & (1<<3)) && (h & APIC_DM_SMI)) {
		printk(KERN_DEBUG "CPU#%d: Thermal monitoring handled by SMI\n", cpu);
		return; /* -EBUSY */
	}

	/* check whether a vector already exists, temporarily masked? */	
	if (h & APIC_VECTOR_MASK) {
		printk(KERN_DEBUG "CPU#%d: Thermal LVT vector (%#x) already installed\n",
			cpu, (h & APIC_VECTOR_MASK));
		return; /* -EBUSY */
	}

	/* The temperature transition interrupt handler setup */
	h = THERMAL_APIC_VECTOR;		/* our delivery vector */
	h |= (APIC_DM_FIXED | APIC_LVT_MASKED);	/* we'll mask till we're ready */
	apic_write_around(APIC_LVTTHMR, h);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT, l | 0x03 , h);

	/* ok we're good to go... */
	vendor_thermal_interrupt = intel_thermal_interrupt;
	
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | (1<<3), h);
	
	l = apic_read(APIC_LVTTHMR);
	apic_write_around(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);
	printk(KERN_INFO "CPU#%d: Thermal monitoring enabled\n", cpu);
	return;
}
#endif /* CONFIG_X86_MCE_P4THERMAL */


/* P4/Xeon Extended MCE MSR retrieval, return 0 if unsupported */

static int inline intel_get_extended_msrs(struct intel_mce_extended_msrs *r)
{
	u32 h;

	if (mce_num_extended_msrs == 0)
		goto done;

	rdmsr(MSR_IA32_MCG_EAX, r->eax, h);
	rdmsr(MSR_IA32_MCG_EBX, r->ebx, h);
	rdmsr(MSR_IA32_MCG_ECX, r->ecx, h);
	rdmsr(MSR_IA32_MCG_EDX, r->edx, h);
	rdmsr(MSR_IA32_MCG_ESI, r->esi, h);
	rdmsr(MSR_IA32_MCG_EDI, r->edi, h);
	rdmsr(MSR_IA32_MCG_EBP, r->ebp, h);
	rdmsr(MSR_IA32_MCG_ESP, r->esp, h);
	rdmsr(MSR_IA32_MCG_EFLAGS, r->eflags, h);
	rdmsr(MSR_IA32_MCG_EIP, r->eip, h);

	/* can we rely on kmalloc to do a dynamic
	 * allocation for the reserved registers?
	 */
done:
	return mce_num_extended_msrs;
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
	struct intel_mce_extended_msrs dbg;

	rdmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
	if(mcgstl&(1<<0))	/* Recoverable ? */
		recover=0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x\n", smp_processor_id(), mcgsth, mcgstl);

	if (intel_get_extended_msrs(&dbg)) {
		printk(KERN_DEBUG "CPU %d: EIP: %08x EFLAGS: %08x\n",
			smp_processor_id(), dbg.eip, dbg.eflags);
		printk(KERN_DEBUG "\teax: %08x ebx: %08x ecx: %08x edx: %08x\n",
			dbg.eax, dbg.ebx, dbg.ecx, dbg.edx);
		printk(KERN_DEBUG "\tesi: %08x edi: %08x ebp: %08x esp: %08x\n",
			dbg.esi, dbg.edi, dbg.ebp, dbg.esp);
	}

	for (i=0;i<banks;i++) {
		rdmsr(MSR_IA32_MC0_STATUS+i*4,low, high);
		if(high&(1<<31)) {
			if(high&(1<<29))
				recover|=1;
			if(high&(1<<25))
				recover|=2;
			printk(KERN_EMERG "Bank %d: %08x%08x", i, high, low);
			high&=~(1<<31);
			if(high&(1<<27)) {
				rdmsr(MSR_IA32_MC0_MISC+i*4, alow, ahigh);
				printk("[%08x%08x]", ahigh, alow);
			}
			if(high&(1<<26)) {
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
 *	Machine check handler for Pentium class Intel
 */

static void pentium_machine_check(struct pt_regs * regs, long error_code)
{
	u32 loaddr, hi, lotype;
	rdmsr(MSR_IA32_P5_MC_ADDR, loaddr, hi);
	rdmsr(MSR_IA32_P5_MC_TYPE, lotype, hi);
	printk(KERN_EMERG "CPU#%d: Machine Check Exception:  0x%8X (type 0x%8X).\n", smp_processor_id(), loaddr, lotype);
	if(lotype&(1<<5))
		printk(KERN_EMERG "CPU#%d: Possible thermal failure (CPU on fire ?).\n", smp_processor_id());
}

/*
 *	Machine check handler for WinChip C6
 */

static void winchip_machine_check(struct pt_regs * regs, long error_code)
{
	printk(KERN_EMERG "CPU#%d: Machine Check Exception.\n", smp_processor_id());
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
static struct timer_list mce_timer;
static int timerset = 0;

#define MCE_RATE	15*HZ	/* timer rate is 15s */

static void mce_checkregs (void *info)
{
	u32 low, high;
	int i;

	for (i=0; i<banks; i++) {
		rdmsr(MSR_IA32_MC0_STATUS+i*4, low, high);

		if ((low | high) != 0) {
			printk (KERN_EMERG "MCE: The hardware reports a non fatal, correctable incident occured on CPU %d.\n", smp_processor_id());
			printk (KERN_EMERG "Bank %d: %08x%08x\n", i, high, low);

			/* Scrub the error so we don't pick it up in MCE_RATE seconds time. */
			wrmsr(MSR_IA32_MC0_STATUS+i*4, 0UL, 0UL);

			/* Serialize */
			wmb();
		}
	}
}

static void do_mce_timer(void *data)
{ 
	preempt_disable(); 
	mce_checkregs(NULL);
	smp_call_function (mce_checkregs, NULL, 1, 1);
	preempt_enable();
	mce_timer.expires = jiffies + MCE_RATE;
	add_timer (&mce_timer);
} 

static struct tq_struct mce_task = { 
	routine: do_mce_timer	
};

static void mce_timerfunc (unsigned long data)
{
#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) { 
		schedule_task(&mce_task); 
		return;
	}
#endif
	mce_checkregs(NULL);
	mce_timer.expires = jiffies + MCE_RATE;
	add_timer (&mce_timer);
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

	if( !cpu_has(c, X86_FEATURE_MCE) )
		return;	

	/*
	 *	Pentium machine check
	 */

	if(c->x86 == 5)
	{
		/* Default P5 to off as its often misconnected */
		if(mce_disabled != -1)
			return;
		machine_check_vector = pentium_machine_check;
		wmb();
		/* Read registers before enabling */
		rdmsr(MSR_IA32_P5_MC_ADDR, l, h);
		rdmsr(MSR_IA32_P5_MC_TYPE, l, h);
		if(done==0)
			printk(KERN_INFO "Intel old style machine check architecture supported.\n");
 		/* Enable MCE */
		set_in_cr4(X86_CR4_MCE);
		printk(KERN_INFO "Intel old style machine check reporting enabled on CPU#%d.\n", smp_processor_id());
		return;
	}


	/*
	 *	Check for PPro style MCA
	 */
 		
	if( !cpu_has(c, X86_FEATURE_MCA) )
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

	/*
	 *	Check for P4/Xeon specific MCE extensions
	 */

	if (c->x86_vendor == X86_VENDOR_INTEL && c->x86 == 15) {
		/* Check for P4/Xeon extended MCE MSRs */
		rdmsr(MSR_IA32_MCG_CAP, l, h);
		if (l & (1<<9))	{/* MCG_EXT_P */
			mce_num_extended_msrs = (l >> 16) & 0xff;
			printk(KERN_INFO "CPU#%d: Intel P4/Xeon Extended MCE MSRs (%d) available\n",
				smp_processor_id(), mce_num_extended_msrs);
		}
		
#ifdef CONFIG_X86_MCE_P4THERMAL
		/* Check for P4/Xeon Thermal monitor */
		intel_init_thermal(c);
#endif
	}

	done=1;
}

/*
 *	Set up machine check reporting on the Winchip C6 series
 */

static void __init winchip_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 lo, hi;
	/* Not supported on C3 */
	if(c->x86 != 5)
		return;
	/* Winchip C6 */
	machine_check_vector = winchip_machine_check;
	wmb();
	rdmsr(MSR_IDT_FCR1, lo, hi);
	lo|= (1<<2);	/* Enable EIERRINT (int 18 MCE) */
	lo&= ~(1<<4);	/* Enable MCE */
	wrmsr(MSR_IDT_FCR1, lo, hi);
	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Winchip machine check reporting enabled on CPU#%d.\n", smp_processor_id());
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
			/* AMD K7 machine check is Intel like */
			if(c->x86 == 6 || c->x86 == 15) {
				intel_mcheck_init(c);
#ifdef CONFIG_X86_MCE_NONFATAL
				if (timerset == 0) {
					/* Set the timer to check for non-fatal
					   errors every MCE_RATE seconds */
					init_timer (&mce_timer);
					mce_timer.expires = jiffies + MCE_RATE;
					mce_timer.data = 0;
					mce_timer.function = &mce_timerfunc;
					add_timer (&mce_timer);
					timerset = 1;
					printk(KERN_INFO "Machine check exception polling timer started.\n");
				}
#endif
			}
			break;

		case X86_VENDOR_INTEL:
			intel_mcheck_init(c);
			break;

		case X86_VENDOR_CENTAUR:
			winchip_mcheck_init(c);
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

#else
asmlinkage void do_machine_check(struct pt_regs * regs, long error_code) {}
asmlinkage void smp_thermal_interrupt(struct pt_regs regs) {}
void __init mcheck_init(struct cpuinfo_x86 *c) {}
#endif
