#ifndef _SPARC64_SIGINFO_H
#define _SPARC64_SIGINFO_H

#define SI_PAD_SIZE	((SI_MAX_SIZE/sizeof(int)) - 4)
#define SI_PAD_SIZE32	((SI_MAX_SIZE/sizeof(int)) - 3)

#define SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE/sizeof(int)) - 4)
#define SIGEV_PAD_SIZE32 ((SIGEV_MAX_SIZE/sizeof(int)) - 3)

#define HAVE_ARCH_SIGINFO_T
#define HAVE_ARCH_COPY_SIGINFO
#define HAVE_ARCH_COPY_SIGINFO_TO_USER

#include <asm-generic/siginfo.h>

#ifdef __KERNEL__

typedef union sigval32 {
	int sival_int;
	u32 sival_ptr;
} sigval_t32;

#endif /* __KERNEL__ */

typedef struct siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
			sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			uid_t _uid;		/* sender's uid */
			int _status;		/* exit code */
			clock_t _utime;
			clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGEMT */
		struct {
			void *_addr; /* faulting insn/memory ref. */
			int  _trapno; /* TRAP # which caused the signal */
		} _sigfault;

		/* SIGPOLL */
		struct {
			long _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} siginfo_t;

#ifdef __KERNEL__

typedef struct siginfo32 {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			__kernel_pid_t32 _pid;		/* sender's pid */
			unsigned int _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			__kernel_pid_t32 _pid;		/* sender's pid */
			unsigned int _uid;		/* sender's uid */
			sigval_t32 _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			__kernel_pid_t32 _pid;		/* which child */
			unsigned int _uid;		/* sender's uid */
			int _status;			/* exit code */
			__kernel_clock_t32 _utime;
			__kernel_clock_t32 _stime;
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

#define si_trapno	_sifields._sigfault._trapno

#define SI_NOINFO	32767		/* no information in siginfo_t */

/*
 * SIGEMT si_codes
 */
#define EMT_TAGOVF	(__SI_FAULT|1)	/* tag overflow */
#define NSIGEMT		1

#ifdef __KERNEL__

typedef struct sigevent32 {
	sigval_t sigev_value;
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

#include <linux/string.h>

static inline void copy_siginfo(siginfo_t *to, siginfo_t *from)
{
	if (from->si_code < 0)
		*to = *from;
	else
		/* _sigchld is currently the largest know union member */
		memcpy(to, from, 4*sizeof(int) + sizeof(from->_sifields._sigchld));
}

extern int copy_siginfo_to_user32(siginfo_t32 *to, siginfo_t *from);

#endif /* __KERNEL__ */

#endif
