/*
 * Non Fatal Machine Check Exception Reporting
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/config.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/module.h>

#include <asm/processor.h> 
#include <asm/system.h>
#include <asm/msr.h>

#include "mce.h"

static struct timer_list mce_timer;
static int timerset;

#define MCE_RATE	15*HZ	/* timer rate is 15s */

static void mce_checkregs (void *info)
{
	u32 low, high;
	int i;

	preempt_disable(); 
	for (i=0; i<nr_mce_banks; i++) {
		rdmsr (MSR_IA32_MC0_STATUS+i*4, low, high);

		if (high & (1<<31)) {
			printk (KERN_EMERG "MCE: The hardware reports a non fatal, correctable incident occurred on CPU %d.\n",
				smp_processor_id());
			printk (KERN_EMERG "Bank %d: %08x%08x\n", i, high, low);

			/* Scrub the error so we don't pick it up in MCE_RATE seconds time. */
			wrmsr (MSR_IA32_MC0_STATUS+i*4, 0UL, 0UL);

			/* Serialize */
			wmb();
		}
	}
	preempt_enable();
}

static void do_mce_timer(void *data)
{ 
	smp_call_function (mce_checkregs, NULL, 1, 1);
} 

static DECLARE_WORK(mce_work, do_mce_timer, NULL);

static void mce_timerfunc (unsigned long data)
{
	mce_checkregs (NULL);
#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) 
		schedule_work (&mce_work); 
#endif
	mce_timer.expires = jiffies + MCE_RATE;
	add_timer (&mce_timer);
}	

static int __init init_nonfatal_mce_checker(void)
{
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
	return 0;
}
module_init(init_nonfatal_mce_checker);
