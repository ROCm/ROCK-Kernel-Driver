#ifndef _PPC64_PPC32_H
#define _PPC64_PPC32_H

#include <linux/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>

/*
 * Data types and macros for providing 32b PowerPC support.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* Use this to get at 32-bit user passed pointers. */
/* Things to consider: the low-level assembly stub does
   srl x, 0, x for first four arguments, so if you have
   pointer to something in the first four arguments, just
   declare it as a pointer, not u32. On the other side, 
   arguments from 5th onwards should be declared as u32
   for pointers, and need AA() around each usage.
   A() macro should be used for places where you e.g.
   have some internal variable u32 and just want to get
   rid of a compiler warning. AA() has to be used in
   places where you want to convert a function argument
   to 32bit pointer or when you e.g. access pt_regs
   structure and want to consider 32bit registers only.
   -
 */
#define A(__x) ((unsigned long)(__x))
#define AA(__x)				\
({	unsigned long __ret;		\
	__asm__ ("clrldi	%0, %0, 32"	\
		 : "=r" (__ret)		\
		 : "0" (__x));		\
	__ret;				\
})

/* These are here to support 32-bit syscalls on a 64-bit kernel. */

typedef struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			compat_uid_t _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			compat_uid_t _uid;		/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;		/* which child */
			compat_uid_t _uid;		/* sender's uid */
			int _status;			/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGEMT */
		struct {
			unsigned int _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} compat_siginfo_t;

#define __old_sigaction32	old_sigaction32

struct __old_sigaction32 {
	unsigned		sa_handler;
	compat_old_sigset_t  	sa_mask;
	unsigned int    	sa_flags;
	unsigned		sa_restorer;     /* not used by Linux/SPARC yet */
};



struct sigaction32 {
       unsigned int  sa_handler;	/* Really a pointer, but need to deal with 32 bits */
       unsigned int sa_flags;
       unsigned int sa_restorer;	/* Another 32 bit pointer */
       compat_sigset_t sa_mask;		/* A 32 bit mask */
};

typedef struct sigaltstack_32 {
	unsigned int ss_sp;
	int ss_flags;
	compat_size_t ss_size;
} stack_32_t;

struct sigcontext32 {
	unsigned int	_unused[4];
	int		signal;
	unsigned int	handler;
	unsigned int	oldmask;
	u32 regs;  /* 4 byte pointer to the pt_regs32 structure. */
};

struct mcontext32 {
	elf_gregset_t32		mc_gregs;
	elf_fpregset_t		mc_fregs;
	unsigned int		mc_pad[2];
	elf_vrregset_t32	mc_vregs __attribute__((__aligned__(16)));
};

struct ucontext32 { 
	unsigned int	  	uc_flags;
	unsigned int 	  	uc_link;
	stack_32_t	 	uc_stack;
	int		 	uc_pad[7];
	u32			uc_regs;	/* points to uc_mcontext field */
	compat_sigset_t	 	uc_sigmask;	/* mask last for extensibility */
	/* glibc has 1024-bit signal masks, ours are 64-bit */
	int		 	uc_maskext[30];
	int		 	uc_pad2[3];
	struct mcontext32	uc_mcontext;
};

struct ipc_kludge_32 {
	unsigned int msgp;
	int msgtyp;
};

#endif  /* _PPC64_PPC32_H */
