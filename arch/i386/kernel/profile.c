/*
 *	linux/arch/i386/kernel/profile.c
 *
 *	(C) 2002 John Levon <levon@movementarian.org>
 *
 */

#include <linux/profile.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/irq.h>
#include <asm/hw_irq.h> 
 
static struct notifier_block * profile_listeners;
static rwlock_t profile_lock = RW_LOCK_UNLOCKED;
 
int register_profile_notifier(struct notifier_block * nb)
{
	int err;
	write_lock_irq(&profile_lock);
	err = notifier_chain_register(&profile_listeners, nb);
	write_unlock_irq(&profile_lock);
	return err;
}


int unregister_profile_notifier(struct notifier_block * nb)
{
	int err;
	write_lock_irq(&profile_lock);
	err = notifier_chain_unregister(&profile_listeners, nb);
	write_unlock_irq(&profile_lock);
	return err;
}


void x86_profile_hook(struct pt_regs * regs)
{
	/* we would not even need this lock if
	 * we had a global cli() on register/unregister
	 */ 
	read_lock(&profile_lock);
	notifier_call_chain(&profile_listeners, 0, regs);
	read_unlock(&profile_lock);
}
