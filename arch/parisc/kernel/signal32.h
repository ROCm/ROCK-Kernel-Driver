#ifndef _PARISC64_KERNEL_SIGNAL32_H
#define _PARISC64_KERNEL_SIGNAL32_H

#include <linux/compat.h>
#include <asm/compat_signal.h>
#include <asm/compat_rt_sigframe.h>

/* ELF32 signal handling */

struct k_sigaction32 {
	struct compat_sigaction sa;
};

void sigset_32to64(sigset_t *s64, compat_sigset_t *s32);
void sigset_64to32(compat_sigset_t *s32, sigset_t *s64);
int do_sigaltstack32 (const compat_stack_t *uss32, 
		compat_stack_t *uoss32, unsigned long sp);
long restore_sigcontext32(struct compat_sigcontext *sc, 
		struct compat_regfile *rf,
		struct pt_regs *regs);
long setup_sigcontext32(struct compat_sigcontext *sc, 
		struct compat_regfile *rf,
		struct pt_regs *regs, int in_syscall);

#endif
