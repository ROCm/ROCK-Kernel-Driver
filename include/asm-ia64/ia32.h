#ifndef _ASM_IA64_IA32_H
#define _ASM_IA64_IA32_H

#include <linux/config.h>

#include <asm/ptrace.h>
#include <asm/signal.h>

#ifdef CONFIG_IA32_SUPPORT

extern void ia32_cpu_init (void);
extern void ia32_boot_gdt_init (void);
extern void ia32_gdt_init (void);
extern int ia32_exception (struct pt_regs *regs, unsigned long isr);
extern int ia32_intercept (struct pt_regs *regs, unsigned long isr);
extern int ia32_clone_tls (struct task_struct *child, struct pt_regs *childregs);

#endif /* !CONFIG_IA32_SUPPORT */

/* Declare this unconditionally, so we don't get warnings for unreachable code.  */
extern int ia32_setup_frame1 (int sig, struct k_sigaction *ka, siginfo_t *info,
			      sigset_t *set, struct pt_regs *regs);

#endif /* _ASM_IA64_IA32_H */
