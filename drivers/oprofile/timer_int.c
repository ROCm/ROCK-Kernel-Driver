/**
 * @file timer_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/oprofile.h>
#include <linux/profile.h>
#include <linux/init.h>
#include <asm/ptrace.h>
 
static int timer_notify(struct notifier_block * self, unsigned long val, void * data)
{
	struct pt_regs * regs = (struct pt_regs *)data;
	int cpu = smp_processor_id();
	unsigned long eip = instruction_pointer(regs);
 
	oprofile_add_sample(eip, !user_mode(regs), 0, cpu);
	return 0;
}
 
 
static struct notifier_block timer_notifier = {
	.notifier_call	= timer_notify,
};
 

static int timer_start(void)
{
	return register_profile_notifier(&timer_notifier);
}


static void timer_stop(void)
{
	unregister_profile_notifier(&timer_notifier);
}


static struct oprofile_operations timer_ops = {
	.start	= timer_start,
	.stop	= timer_stop,
	.cpu_type = "timer"
};

 
void __init timer_init(struct oprofile_operations ** ops)
{
	*ops = &timer_ops;
	printk(KERN_INFO "oprofile: using timer interrupt.\n");
}
