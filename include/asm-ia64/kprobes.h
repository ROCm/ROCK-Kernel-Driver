#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H
/*
 *  Dynamic Probes (kprobes) support
 *  	Vamsi Krishna S <vamsi_krishna@in.ibm.com>, July, 2002
 *	Prasanna S Panchamukhi <prasanna@in.ibm.com>, April 2004
 *	Mailing list: dprobes@www-124.ibm.com
 */
#include <linux/types.h>
#include <linux/ptrace.h>

struct pt_regs;

typedef unsigned char kprobe_opcode_t;

#define KPROBE_BREAK_NUM		0xeb0be
#define KPROBE_INSTR_BREAKPOINT		(KPROBE_BREAK_NUM << 6)
#define KPROBE_SLOT0_INSTR_MASK		0x3fffffffffe0
#define MAX_INSN_SIZE			16

struct kprobe_trap_data {
	struct pt_regs *regs;
	int return_val;
	int trapnr;
};

/* trap3/1 are intr gates for kprobes.  So, restore the status of IF,
 * if necessary, before executing the original int3/1 (trap) handler.
 */
static inline void restore_interrupts(struct pt_regs *regs)
{
	if (ia64_psr(regs)->i == 0)
		local_irq_enable();

}

#ifdef CONFIG_KPROBES
extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
extern int post_kprobe_handler(struct pt_regs *regs, unsigned long vector);
extern int kprobe_handler(struct pt_regs *regs, unsigned long vector);
extern unsigned long kp_get_gr(unsigned short , struct pt_regs *, struct switch_stack *);
extern void kp_set_gr(unsigned short , unsigned long , struct pt_regs *,  struct switch_stack *);
#else /* !CONFIG_KPROBES */
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr) { return 0; }
static inline int post_kprobe_handler(struct pt_regs *regs, unsigned long vector) { return 0; }
static inline int kprobe_handler(struct pt_regs *regs, unsigned long vector) { return 0; }
static unsigned long kp_get_gr(unsigned short regnum, struct pt_regs *regs, struct switch_stack *sw) { return 0; }
static void kp_set_gr(unsigned short regnum, unsigned long val, struct pt_regs *regs,  struct switch_stack *sw) { }
#endif
#endif /* _ASM_KPROBES_H */
