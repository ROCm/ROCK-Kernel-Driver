/* $Id: siginfo.h,v 1.9 2002/02/08 03:57:18 davem Exp $
 * siginfo.c:
 */

#ifndef _SPARC_SIGINFO_H
#define _SPARC_SIGINFO_H

#define HAVE_ARCH_SIGINFO_T
#define HAVE_ARCH_COPY_SIGINFO

#include <asm-generic/siginfo.h>

typedef struct siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			pid_t _pid;		/* sender's pid */
			unsigned int _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t _pid;		/* sender's pid */
			unsigned int _uid;	/* sender's uid */
			sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			unsigned int _uid;	/* sender's uid */
			int _status;		/* exit code */
			clock_t _utime;
			clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGEMT */
		struct {
			void *_addr;	/* faulting insn/memory ref. */
			int  _trapno;	/* TRAP # which caused the signal */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} siginfo_t;

#define si_trapno	_sifields._sigfault._trapno

#define SI_NOINFO	32767		/* no information in siginfo_t */

/*
 * SIGEMT si_codes
 */
#define EMT_TAGOVF	(__SI_FAULT|1)	/* tag overflow */
#define NSIGEMT		1

#ifdef __KERNEL__

#include <linux/string.h>

extern inline void copy_siginfo(siginfo_t *to, siginfo_t *from)
{
	if (from->si_code < 0)
		*to = *from;
	else
		/* _sigchld is currently the largest know union member */
		memcpy(to, from, 3*sizeof(int) + sizeof(from->_sifields._sigchld));
}

#endif /* __KERNEL__ */

#endif /* !(_SPARC_SIGINFO_H) */
