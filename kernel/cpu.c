/* CPU control.
 * (C) 2001 Rusty Russell
 * This code is licenced under the GPL.
 */
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <asm/semaphore.h>

/* This protects CPUs going up and down... */
DECLARE_MUTEX(cpucontrol);

static struct notifier_block *cpu_chain = NULL;

/* Need to know about CPUs going up/down? */
int register_cpu_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&cpu_chain, nb);
}

void unregister_cpu_notifier(struct notifier_block *nb)
{
	notifier_chain_unregister(&cpu_chain,nb);
}

int __devinit cpu_up(unsigned int cpu)
{
	int ret;

	if ((ret = down_interruptible(&cpucontrol)) != 0) 
		return ret;

	if (cpu_online(cpu)) {
		ret = -EINVAL;
		goto out;
	}

	/* Arch-specific enabling code. */
	ret = __cpu_up(cpu);
	if (ret != 0) goto out;
	if (!cpu_online(cpu))
		BUG();

	/* Now call notifier in preparation. */
	printk("CPU %u IS NOW UP!\n", cpu);
	notifier_call_chain(&cpu_chain, CPU_ONLINE, (void *)cpu);

 out:
	up(&cpucontrol);
	return ret;
}
