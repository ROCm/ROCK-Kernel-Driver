/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYS_SIGCONTEXT_I386_H
#define __SYS_SIGCONTEXT_I386_H

#define IP_RESTART_SYSCALL(ip) ((ip) -= 2)

#define SC_RESTART_SYSCALL(sc) IP_RESTART_SYSCALL(SC_IP(sc))
#define SC_SET_SYSCALL_RETURN(sc, result) do SC_EAX(sc) = (result) ; while(0)

#define SC_FAULT_ADDR(sc) SC_CR2(sc)
#define SC_FAULT_WRITE(sc) (SC_ERR(sc) & 2)

/* ptrace expects that, at the start of a system call, %eax contains
 * -ENOSYS, so this makes it so.
 */
#define SC_START_SYSCALL(sc) do SC_EAX(sc) = -ENOSYS; while(0)

/* These are General Protection and Page Fault */
#define SEGV_IS_FIXABLE(sc) ((SC_TRAPNO(sc) == 13) || (SC_TRAPNO(sc) == 14))

/* XXX struct sigcontext needs declaring by now */

static inline void sc_to_regs(struct uml_pt_regs *regs, struct sigcontext *sc,
			      unsigned long syscall)
{
	regs->syscall = syscall;
	regs->args[0] = SC_EBX(sc);
	regs->args[1] = SC_ECX(sc);
	regs->args[2] = SC_EDX(sc);
	regs->args[3] = SC_ESI(sc);
	regs->args[4] = SC_EDI(sc);
	regs->args[5] = SC_EBP(sc);
}

extern unsigned long *sc_sigmask(void *sc_ptr);
extern int sc_get_fpregs(unsigned long buf, void *sc_ptr);

#endif
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
