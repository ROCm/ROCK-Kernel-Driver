#ifndef _M68K_SIGINFO_H
#define _M68K_SIGINFO_H

#define HAVE_ARCH_SIGINFO_T

#include <asm-generic/siginfo.h>

typedef struct siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			__kernel_pid_t _pid;	/* sender's pid */
			__kernel_uid_t _uid;	/* backwards compatibility */
			__kernel_uid32_t _uid32; /* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			__kernel_pid_t _pid;	/* sender's pid */
			__kernel_uid_t _uid;	/* backwards compatibility */
			sigval_t _sigval;
			__kernel_uid32_t _uid32; /* sender's uid */
		} _rt;

		/* SIGCHLD */
		struct {
			__kernel_pid_t _pid;	/* which child */
			__kernel_uid_t _uid;	/* backwards compatibility */
			int _status;		/* exit code */
			clock_t _utime;
			clock_t _stime;
			__kernel_uid32_t _uid32; /* sender's uid */
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			void *_addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} siginfo_t;

#define UID16_SIGINFO_COMPAT_NEEDED

/*
 * How these fields are to be accessed.
 */
#undef si_uid
#ifdef __KERNEL__
#define si_uid		_sifields._kill._uid32
#define si_uid16	_sifields._kill._uid
#else
#define si_uid		_sifields._kill._uid
#endif /* __KERNEL__ */

#endif
