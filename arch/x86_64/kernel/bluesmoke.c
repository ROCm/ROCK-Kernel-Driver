/*
 * arch/x86_64/kernel/bluesmoke.c - x86-64 Machine Check Exception Reporting
 * 

RED-PEN: need to add power management to restore after S3 wakeup. 

 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/config.h>
#include <linux/irq.h>
#include <asm/processor.h> 
#include <asm/system.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_X86_MCE

static int mce_disabled __initdata = 0;

static int banks;


/*
 *	Machine Check Handler For Hammer
 */

static void hammer_machine_check(struct pt_regs * regs, long error_code)
{
	int recover=1;
	u32 alow, ahigh, high, low;
	u32 mcgstl, mcgsth;
	int i;

	rdmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
	if(mcgstl&(1<<0))	/* Recoverable ? */
		recover=0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x\n", smp_processor_id(), mcgsth, mcgstl);
	preempt_disable();
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
	preempt_enable();

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
			printk (KERN_EMERG "MCE: The hardware reports a non fatal, correctable incident occurred on CPU %d.\n", smp_processor_id());
			printk (KERN_EMERG "Bank %d: %08x%08x\n", i, high, low);

			/* Scrub the error so we don't pick it up in MCE_RATE seconds time. */
			wrmsr(MSR_IA32_MC0_STATUS+i*4, 0UL, 0UL);

			/* Serialize */
			wmb();
		}
	}
}


static void mce_timerfunc (unsigned long data)
{
	on_each_cpu (mce_checkregs, NULL, 1, 1);

	/* Refresh the timer. */
	mce_timer.expires = jiffies + MCE_RATE;
	add_timer (&mce_timer);
}
#endif


/*
 *	Set up machine check reporting for processors with Intel style MCE
 */

static void __init hammer_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int i;
	static int done;
	
	/*
	 *	Check for MCE support
	 */

	if( !test_bit(X86_FEATURE_MCE, c->x86_capability) )
		return;	

	/* Check for PPro style MCA */
	if( !test_bit(X86_FEATURE_MCA, c->x86_capability) )
		return;

	/* Ok machine check is available */
	machine_check_vector = hammer_machine_check;
	wmb();

	if(done==0)
		printk(KERN_INFO "Machine check architecture supported.\n");
	rdmsr(MSR_IA32_MCG_CAP, l, h);
	if(l&(1<<8))	/* Control register present ? */
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);
	banks = l&0xff;

	for(i=0; i<banks; i++)
		wrmsr(MSR_IA32_MC0_CTL+4*i, 0xffffffff, 0xffffffff);

	for(i=0; i<banks; i++)
		wrmsr(MSR_IA32_MC0_STATUS+4*i, 0x0, 0x0);

	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Machine check reporting enabled on CPU#%d.\n", smp_processor_id());

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
			hammer_mcheck_init(c);
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
void __init mcheck_init(struct cpuinfo_x86 *c) {}
#endif
