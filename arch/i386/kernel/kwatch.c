/* 
 * Support for kernel watchpoints.
 * (C) 2002 Vamsi Krishna S <vamsi_krishna@in.ibm.com>.
 */
#include <linux/config.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/kwatch.h>
#include <asm/debugreg.h>
#include <asm/bitops.h>

static struct kwatch kwatch_list[DR_MAX];
static spinlock_t kwatch_lock = SPIN_LOCK_UNLOCKED;
static unsigned long kwatch_in_progress; /* currently being handled */

struct dr_info {
	int debugreg;
	unsigned long addr;
	int type;
};

void kwatch_asm_start(void) {}
static inline void write_smp_dr(void *info)
{
	struct dr_info *dr = (struct dr_info *)info;

	if (cpu_has_de && dr->type == DR_TYPE_IO)
		set_in_cr4(X86_CR4_DE);
	write_dr(dr->debugreg, dr->addr);
}

/* Update the debug register on all CPUs */
static void sync_dr(int debugreg, unsigned long addr, int type)
{
	struct dr_info dr;
	dr.debugreg = debugreg;
	dr.addr = addr;
	dr.type = type;
	smp_call_function(write_smp_dr, &dr, 0, 0);
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate and they
 * remain disabled thorough out this function.
 */
int kwatch_handler(unsigned long condition, struct pt_regs *regs)
{
	int debugreg = dr_trap(condition);
	unsigned long addr = dr_trap_addr(condition);
	int retval = 0;

	if (!(condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3))) {
		return 0;
	}
	
	/* We're in an interrupt, but this is clear and BUG()-safe. */
	preempt_disable();

	/* If we are recursing, we already hold the lock. */
	if (kwatch_in_progress) {
		goto recursed;
	}
	set_bit(debugreg, &kwatch_in_progress);
	
	spin_lock(&kwatch_lock);
	if (kwatch_list[debugreg].addr != addr)
		goto out;

	if (kwatch_list[debugreg].handler) {
		kwatch_list[debugreg].handler(&kwatch_list[debugreg], regs, debugreg);
	}
	
	if (kwatch_list[debugreg].type == DR_TYPE_EXECUTE)
		regs->eflags |= RF_MASK;
out:
	clear_bit(debugreg, &kwatch_in_progress);
	spin_unlock(&kwatch_lock);
	preempt_enable_no_resched();
	return retval;

recursed:
	if (kwatch_list[debugreg].type == DR_TYPE_EXECUTE)
		regs->eflags |= RF_MASK;
	preempt_enable_no_resched();
	return 1;
}

int register_kwatch(unsigned long addr, u8 length, u8 type, 
		kwatch_handler_t handler)
{
	int debugreg;
	unsigned long dr7, flags;

	debugreg = dr_alloc(DR_ANY, DR_ALLOC_GLOBAL);
	if (debugreg < 0) {
		return -1;
	}

	spin_lock_irqsave(&kwatch_lock, flags);
	kwatch_list[debugreg].addr = addr;
	kwatch_list[debugreg].length = length;
	kwatch_list[debugreg].type = type;
	kwatch_list[debugreg].handler = handler;
	spin_unlock_irqrestore(&kwatch_lock, flags);

	write_dr(debugreg, (unsigned long)addr);
	sync_dr(debugreg, (unsigned long)addr, type);
	if (cpu_has_de && type == DR_TYPE_IO)
		set_in_cr4(X86_CR4_DE);
	
	dr7 = read_dr(7);
	SET_DR7(dr7, debugreg, type, length);
	write_dr(7, dr7);
	sync_dr(7, dr7, 0);
	return debugreg;
}

void unregister_kwatch(int debugreg)
{
	unsigned long flags;
	unsigned long dr7 = read_dr(7);

	RESET_DR7(dr7, debugreg);
	write_dr(7, dr7);
	sync_dr(7, dr7, 0);
	dr_free(debugreg);

	spin_lock_irqsave(&kwatch_lock, flags);
	kwatch_list[debugreg].addr = 0;
	kwatch_list[debugreg].handler = NULL;
	spin_unlock_irqrestore(&kwatch_lock, flags);
}

void kwatch_asm_end(void) {}

EXPORT_SYMBOL_GPL(register_kwatch);
EXPORT_SYMBOL_GPL(unregister_kwatch);
EXPORT_SYMBOL_GPL(kwatch_asm_start);
EXPORT_SYMBOL_GPL(kwatch_asm_end);

