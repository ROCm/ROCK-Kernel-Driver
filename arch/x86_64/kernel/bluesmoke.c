/*
 * Machine check handler.
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Rest from unknown author(s). 
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/processor.h> 
#include <asm/msr.h>
#include <asm/kdebug.h>
#include <linux/pci.h>
#include <linux/timer.h>

static int mce_disabled __initdata;
static unsigned long mce_cpus; 

/*
 *	Machine Check Handler For PII/PIII/K7
 */

static int banks;
static unsigned long ignored_banks, disabled_banks;

/* Machine Check on everything dubious. This is a good setting
   for device driver testing. */
#define K8_DRIVER_DEBUG ((1<<13)-1)
/* Report RAM errors and Hyper Transport Problems, but ignore Device
   aborts and GART errors. */
#define K8_NORMAL_OP    0xff

#ifdef CONFIG_MCE_DEBUG
static u32 k8_nb_flags __initdata = K8_DRIVER_DEBUG;
#else
static u32 k8_nb_flags __initdata = K8_NORMAL_OP;
#endif

static void generic_machine_check(struct pt_regs * regs, long error_code)
{
	int recover=1;
	u32 alow, ahigh, high, low;
	u32 mcgstl, mcgsth;
	int i;

	preempt_disable();

	rdmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
	if(mcgstl&(1<<0))	/* Recoverable ? */
		recover=0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x\n", smp_processor_id(), mcgsth, mcgstl);
	
	if (regs && (mcgstl & 2))
		printk(KERN_EMERG "RIP <%02lx>:%016lx RSP %016lx\n", 
		       regs->cs, regs->rip, regs->rsp); 

	for(i=0;i<banks;i++)
	{
		if ((1UL<<i) & ignored_banks) 
			continue; 

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
				printk("[%08x%08x]", alow, ahigh);
			}
			if(high&(1<<26))
			{
				rdmsr(MSR_IA32_MC0_ADDR+i*4, alow, ahigh);
				printk(" at %08x%08x", 
					ahigh, alow);
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

	preempt_enable();
}

static void unexpected_machine_check(struct pt_regs *regs, long error_code)
{	
	printk("unexpected machine check %lx\n", error_code); 
}

/*
 *	Call the installed machine check handler for this CPU setup.
 */

static void (*machine_check_vector)(struct pt_regs *, long error_code) = unexpected_machine_check;

void do_machine_check(struct pt_regs * regs, long error_code)
{
	notify_die(DIE_NMI, "machine check", regs, error_code, 255, SIGKILL);
	machine_check_vector(regs, error_code);
}

/* 
 *	K8 machine check.
 */

static struct pci_dev *find_k8_nb(void)
{ 
	struct pci_dev *dev;
	int cpu = smp_processor_id(); 
	pci_for_each_dev(dev) {
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
		    PCI_SLOT(dev->devfn) == (24+cpu))
			return dev;
	}
	return NULL;
}

static void check_k8_nb(void)
{
	struct pci_dev *nb;
	nb = find_k8_nb(); 
	if (nb == NULL)
		return;

	u32 statuslow, statushigh;
	pci_read_config_dword(nb, 0x48, &statuslow);
	pci_read_config_dword(nb, 0x4c, &statushigh);
	if (!(statushigh & (1<<31)))
		return;
	printk(KERN_ERR "Northbridge status %08x%08x\n",
	       statushigh,statuslow); 
	if (statuslow & 0x10) 
		printk(KERN_ERR "GART error %d\n", statuslow & 0xf); 
	if (statushigh & (1<<31))
		printk(KERN_ERR "Lost an northbridge error\n"); 
	if (statushigh & (1<<25))
		printk(KERN_EMERG "NB status: unrecoverable\n"); 
	if (statushigh & (1<<26)) { 
		u32 addrhigh, addrlow; 
		pci_read_config_dword(nb, 0x54, &addrhigh); 
		pci_read_config_dword(nb, 0x50, &addrlow); 
		printk(KERN_ERR "NB error address %08x%08x\n", addrhigh,addrlow); 
	}
	if (statushigh & (1<<29))
		printk(KERN_EMERG "Error uncorrected\n"); 
	statushigh &= ~(1<<31); 
	pci_write_config_dword(nb, 0x4c, statushigh); 		
}

static void k8_machine_check(struct pt_regs * regs, long error_code)
{ 
	u64 status, nbstatus;

	preempt_disable();

	rdmsrl(MSR_IA32_MCG_STATUS, status); 
	if ((status & (1<<2)) == 0) { 
		if (!regs) 
			check_k8_nb();
		return; 
		}
	if (status & 1)
		printk(KERN_EMERG "MCG_STATUS: unrecoverable\n"); 

	rdmsrl(MSR_IA32_MC0_STATUS+4*4, nbstatus); 
	if ((nbstatus & (1UL<<63)) == 0)
		goto others; 
	
	printk(KERN_EMERG "Northbridge Machine Check %s %016lx %lx\n", 
	       regs ? "exception" : "timer",
	       (unsigned long)nbstatus, error_code); 
	if (nbstatus & (1UL<<62))
		printk(KERN_EMERG "Lost at least one NB error condition\n"); 	
	if (nbstatus & (1UL<<61))
		printk(KERN_EMERG "Uncorrectable condition\n"); 
	if (nbstatus & (1UL<57))
		printk(KERN_EMERG "Unrecoverable condition\n"); 
		
	check_k8_nb();

	if (nbstatus & (1UL<<58)) { 
		u64 adr;
		rdmsrl(MSR_IA32_MC0_ADDR+4*4, adr);
		printk(KERN_EMERG "Address: %016lx\n", (unsigned long)adr);
	}
	
	wrmsrl(MSR_IA32_MC0_STATUS+4*4, 0); 
	wrmsrl(MSR_IA32_MCG_STATUS, 0);
       
	if (regs && (status & (1<<1)))
		printk(KERN_EMERG "MCE at EIP %lx ESP %lx\n", regs->rip, regs->rsp); 

 others:
	generic_machine_check(regs, error_code); 

	preempt_enable();
} 

static struct timer_list mcheck_timer;
int mcheck_interval = 30*HZ; 

#ifndef CONFIG_SMP 
static void mcheck_timer_handler(unsigned long data)
{
	k8_machine_check(NULL,0);
	mcheck_timer.expires = jiffies + mcheck_interval;
	add_timer(&mcheck_timer);
}
#else

/* SMP needs a process context trampoline because smp_call_function cannot be 
   called from interrupt context. */

static void mcheck_timer_other(void *data)
{ 
	k8_machine_check(NULL, 0); 
} 

static void mcheck_timer_dist(void *data)
{
	smp_call_function(mcheck_timer_other,0,0,0);
	k8_machine_check(NULL, 0); 
	mcheck_timer.expires = jiffies + mcheck_interval;
	add_timer(&mcheck_timer);
} 

static void mcheck_timer_handler(unsigned long data)
{ 
	static DECLARE_WORK(mcheck_work, mcheck_timer_dist, NULL);
	schedule_work(&mcheck_work); 
}
#endif

static int nok8 __initdata; 

static void __init k8_mcheck_init(struct cpuinfo_x86 *c)
{
	u64 cap;
	int i;
	struct pci_dev *nb; 

	if (!test_bit(X86_FEATURE_MCE, &c->x86_capability) || 
	    !test_bit(X86_FEATURE_MCA, &c->x86_capability))
		return; 

	rdmsrl(MSR_IA32_MCG_CAP, cap); 
	banks = cap&0xff; 
	machine_check_vector = k8_machine_check; 
	for (i = 0; i < banks; i++) { 
		u64 val = ((1UL<<i) & disabled_banks) ? 0 : ~0UL; 
		wrmsrl(MSR_IA32_MC0_CTL+4*i, val);
		wrmsrl(MSR_IA32_MC0_STATUS+4*i,0); 
	}

	nb = find_k8_nb(); 
	if (nb != NULL) {
		u32 reg, reg2;
		pci_read_config_dword(nb, 0x40, &reg); 
		pci_write_config_dword(nb, 0x40, k8_nb_flags);
		pci_read_config_dword(nb, 0x44, &reg2);
		pci_write_config_dword(nb, 0x44, reg2); 
		printk(KERN_INFO "Machine Check for K8 Northbridge %d enabled (%x,%x)\n",
		       nb->devfn, reg, reg2);
		ignored_banks |= (1UL<<4); 
	} 

	set_in_cr4(X86_CR4_MCE);	   	

	if (mcheck_interval && (smp_processor_id() == 0)) { 
		init_timer(&mcheck_timer); 
		mcheck_timer.function = (void (*)(unsigned long))mcheck_timer_handler; 
		mcheck_timer.expires = jiffies + mcheck_interval; 
		add_timer(&mcheck_timer); 
	} 
	
	printk(KERN_INFO "Machine Check Reporting enabled for CPU#%d\n", smp_processor_id()); 
} 

/*
 *	Set up machine check reporting for Intel processors
 */

static void __init generic_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int i;
	static int done;
	
	/*
	 *	Check for MCE support
	 */

	if( !test_bit(X86_FEATURE_MCE, &c->x86_capability) )
		return;	

	/*
	 *	Check for PPro style MCA
	 */
	 		
	if( !test_bit(X86_FEATURE_MCA, &c->x86_capability) )
		return;

	/* Ok machine check is available */
	
	machine_check_vector = generic_machine_check;
	wmb();

	if(done==0)
		printk(KERN_INFO "Intel machine check architecture supported.\n");
	rdmsr(MSR_IA32_MCG_CAP, l, h);
	if(l&(1<<8))
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);
	banks = l&0xff;

	for(i=0;i<banks;i++)
	{
		u32 val = ((1UL<<i) & disabled_banks) ? 0 : ~0;
		wrmsr(MSR_IA32_MC0_CTL+4*i, val, val);
		wrmsr(MSR_IA32_MC0_STATUS+4*i, 0x0, 0x0);
	}
	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Intel machine check reporting enabled on CPU#%d.\n", smp_processor_id());
	done=1;
}

/*
 *	This has to be run for each processor
 */

void __init mcheck_init(struct cpuinfo_x86 *c)
{
	if (test_and_set_bit(smp_processor_id(), &mce_cpus))
		return; 

	if(mce_disabled==1)
		return;

	switch(c->x86_vendor) {
		case X86_VENDOR_AMD:
		if (c->x86 == 15 && !nok8) {
			k8_mcheck_init(c); 
			break;
		}
		/* FALL THROUGH */
		default:
	case X86_VENDOR_INTEL:
		generic_mcheck_init(c);
			break;
	}
}

static int __init mcheck_disable(char *str)
{
	mce_disabled = 1;
	return 0;
}


/* mce=off disable machine check
   mce=nok8 disable k8 specific features
   mce=disable<NUMBER> disable bank NUMBER
   mce=enable<NUMBER> enable bank number
   mce=device	Enable device driver test reporting in NB
   mce=NUMBER mcheck timer interval number seconds. 
   Can be also comma separated in a single mce= */
static int __init mcheck_enable(char *str)
{
	char *p;
	while ((p = strsep(&str,",")) != NULL) { 
		if (isdigit(*p))
			mcheck_interval = simple_strtol(p,NULL,0) * HZ; 
		else if (!strcmp(p,"off"))
			mce_disabled = 1; 
		else if (!strncmp(p,"enable",6))
			disabled_banks &= ~(1<<simple_strtol(p+6,NULL,0));
		else if (!strncmp(p,"disable",7))
			disabled_banks |= ~(1<<simple_strtol(p+7,NULL,0));
		else if (!strcmp(p,"nok8"))
			nok8 = 1;
		else if (!strcmp(p,"device"))
			k8_nb_flags = K8_DRIVER_DEBUG;
	}
	return 0;
}

__setup("nomce", mcheck_disable);
__setup("mce", mcheck_enable);
