#ifndef _SPARC64_SIGINFO_H
#define _SPARC64_SIGINFO_H

#define SI_PAD_SIZE32	((SI_MAX_SIZE/sizeof(int)) - 3)

#define SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE/sizeof(int)) - 4)
#define SIGEV_PAD_SIZE32 ((SIGEV_MAX_SIZE/sizeof(int)) - 3)

#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#define __ARCH_SI_TRAPNO
#define __ARCH_SI_BAND_T int

#include <asm-generic/siginfo.h>

#ifdef __KERNEL__

#include <linux/compat.h>

typedef union sigval32 {
	int sival_int;
	u32 sival_ptr;
} sigval_t32;

typedef struct siginfo32 {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			unsigned int _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			timer_t _tid;			/* timer id */
			int _overrun;			/* overrun count */
			sigval_t32 _sigval;		/* same as below */
			int _sys_private;		/* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			unsigned int _uid;		/* sender's uid */
			sigval_t32 _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;		/* which child */
			unsigned int _uid;		/* sender's uid */
			int _status;			/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGEMT */
		struct {
			u32 _addr; /* faulting insn/memory ref. */
			int _trapno;
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} siginfo_t32;

#endif /* __KERNEL__ */

#define SI_NOINFO	32767		/* no information in siginfo_t */

/*
 * SIGEMT si_codes
 */
#define EMT_TAGOVF	(__SI_FAULT|1)	/* tag overflow */
#define NSIGEMT		1

#ifdef __KERNEL__

typedef struct sigevent32 {
	sigval_t32 sigev_value;
	int sigev_signo;
	int sigev_notify;
	union {
		int _pad[SIGEV_PAD_SIZE32];

		struct {
			u32 _function;
			u32 _attribute;	/* really pthread_attr_t */
		} _sigev_thread;
	} _sigev_un;
} sigevent_t32;

extern int copy_siginfo_to_user32(siginfo_t32 *to, siginfo_t *from);

#endif /* __KERNEL__ */

#endif
