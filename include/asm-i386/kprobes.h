#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H
/*
 *  Dynamic Probes (kprobes) support
 *  	Vamsi Krishna S <vamsi_krishna@in.ibm.com>, July, 2002
 *	Mailing list: dprobes@www-124.ibm.com
 */
#include <linux/types.h>
#include <linux/ptrace.h>

struct pt_regs;

typedef u8 kprobe_opcode_t;
#define BREAKPOINT_INSTRUCTION	0xcc
#define MAX_INSN_SIZE 16

/* trap3/1 are intr gates for kprobes.  So, restore the status of IF,
 * if necessary, before executing the original int3/1 (trap) handler.
 */
static inline void restore_interrupts(struct pt_regs *regs)
{
	if (regs->eflags & IF_MASK)
		__asm__ __volatile__ ("sti");
}

void kprobes_asm_code_start(void);
void kprobes_asm_code_end(void);

#ifdef CONFIG_KPROBES
extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
extern int post_kprobe_handler(struct pt_regs *regs);
extern int kprobe_handler(struct pt_regs *regs);
#else /* !CONFIG_KPROBES */
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr) { return 0; }
static inline int post_kprobe_handler(struct pt_regs *regs) { return 0; }
static inline int kprobe_handler(struct pt_regs *regs) { return 0; }
#endif
#endif /* _ASM_KPROBES_H */
