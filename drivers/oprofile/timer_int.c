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
#include <linux/oprofile.h>
#include <linux/profile.h>
#include <linux/init.h>
#include <asm/ptrace.h>
 
static int timer_notify(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long eip = profile_pc(regs);
 
	oprofile_add_sample(eip, !user_mode(regs), 0, cpu);
	return 0;
}

static int timer_start(void)
{
	return register_timer_hook(timer_notify);
}


static void timer_stop(void)
{
	unregister_timer_hook(timer_notify);
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
