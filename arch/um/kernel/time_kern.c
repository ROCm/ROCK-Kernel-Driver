/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/unistd.h"
#include "linux/stddef.h"
#include "linux/spinlock.h"
#include "linux/time.h"
#include "linux/sched.h"
#include "linux/interrupt.h"
#include "linux/init.h"
#include "linux/delay.h"
#include "asm/irq.h"
#include "asm/param.h"
#include "asm/current.h"
#include "kern_util.h"
#include "user_util.h"
#include "time_user.h"
#include "mode.h"

u64 jiffies_64;

EXPORT_SYMBOL(jiffies_64);

int hz(void)
{
	return(HZ);
}

/* Changed at early boot */
int timer_irq_inited = 0;

/* missed_ticks will be modified after kernel memory has been 
 * write-protected, so this puts it in a section which will be left 
 * write-enabled.
 */
int __attribute__ ((__section__ (".unprotected"))) missed_ticks[NR_CPUS];

void timer_irq(union uml_pt_regs *regs)
{
	int cpu = current->thread_info->cpu, ticks = missed_ticks[cpu];

        if(!timer_irq_inited) return;
	missed_ticks[cpu] = 0;
	while(ticks--) do_IRQ(TIMER_IRQ, regs);
}

void boot_timer_handler(int sig)
{
	struct pt_regs regs;

	CHOOSE_MODE((void) 
		    (UPT_SC(&regs.regs) = (struct sigcontext *) (&sig + 1)),
		    (void) (regs.regs.skas.is_user = 0));
	do_timer(&regs);
}

void um_timer(int irq, void *dev, struct pt_regs *regs)
{
	do_timer(regs);
	write_seqlock(&xtime_lock);
	timer();
	write_sequnlock(&xtime_lock);
}

long um_time(int * tloc)
{
	struct timeval now;

	do_gettimeofday(&now);
	if (tloc) {
 		if (put_user(now.tv_sec,tloc))
			now.tv_sec = -EFAULT;
	}
	return now.tv_sec;
}

long um_stime(int * tptr)
{
	int value;
	struct timeval new;

	if (get_user(value, tptr))
                return -EFAULT;
	new.tv_sec = value;
	new.tv_usec = 0;
	do_settimeofday(&new);
	return 0;
}

/* XXX Needs to be moved under sys-i386 */
void __delay(um_udelay_t time)
{
	/* Stolen from the i386 __loop_delay */
	int d0;
	__asm__ __volatile__(
		"\tjmp 1f\n"
		".align 16\n"
		"1:\tjmp 2f\n"
		".align 16\n"
		"2:\tdecl %0\n\tjns 2b"
		:"=&a" (d0)
		:"0" (time));
}

void __udelay(um_udelay_t usecs)
{
	int i, n;

	n = (loops_per_jiffy * HZ * usecs) / 1000000;
	for(i=0;i<n;i++) ;
}

void __const_udelay(um_udelay_t usecs)
{
	int i, n;

	n = (loops_per_jiffy * HZ * usecs) / 1000000;
	for(i=0;i<n;i++) ;
}

void timer_handler(int sig, union uml_pt_regs *regs)
{
#ifdef CONFIG_SMP
	update_process_times(user_context(UPT_SP(regs)));
#endif
	if(current->thread_info->cpu == 0)
		timer_irq(regs);
}

static spinlock_t timer_spinlock = SPIN_LOCK_UNLOCKED;

unsigned long time_lock(void)
{
	unsigned long flags;
	spin_lock_irqsave(&timer_spinlock, flags);
	return(flags);
}

void time_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&timer_spinlock, flags);
}

int __init timer_init(void)
{
	int err;

	CHOOSE_MODE(user_time_init_tt(), user_time_init_skas());
	if((err = request_irq(TIMER_IRQ, um_timer, SA_INTERRUPT, "timer", 
			      NULL)) != 0)
		printk(KERN_ERR "timer_init : request_irq failed - "
		       "errno = %d\n", -err);
	timer_irq_inited = 1;
	return(0);
}

__initcall(timer_init);


/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
