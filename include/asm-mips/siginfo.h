/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2001 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIGINFO_H
#define _ASM_SIGINFO_H

#include <linux/config.h>

#define SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE/sizeof(int)) - 4)
#define SI_PAD_SIZE	((SI_MAX_SIZE/sizeof(int)) - 4)

#define HAVE_ARCH_SIGINFO_T
#define HAVE_ARCH_SIGEVENT_T

/*
 * We duplicate the generic versions - <asm-generic/siginfo.h> is just borked
 * by design ...
 */
#define HAVE_ARCH_COPY_SIGINFO
struct siginfo;

#include <asm-generic/siginfo.h>

/* This structure matches the 32/n32 ABIs for source compatibility but
   has Linux extensions.  */

typedef struct siginfo {
	int si_signo;
	int si_code;
	int si_errno;

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
		} _kill;

		/* SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			uid_t _uid;		/* sender's uid */
			clock_t _utime;
			int _status;		/* exit code */
			clock_t _stime;
		} _sigchld;

		/* IRIX SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			clock_t _utime;
			int _status;		/* exit code */
			clock_t _stime;
		} _irix_sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			void *_addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL, SIGXFSZ (To do ...)  */
		struct {
#ifdef CONFIG_MIPS32
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
#endif
#ifdef CONFIG_MIPS64
			long _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
#endif
			int _fd;
		} _sigpoll;

		/* POSIX.1b timers */
		struct {
			timer_t _tid;		/* timer id */
			int _overrun;		/* overrun count */
			char _pad[sizeof( __ARCH_SI_UID_T) - sizeof(int)];
			sigval_t _sigval;	/* same as below */
			int _sys_private;	/* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
			sigval_t _sigval;
		} _rt;

	} _sifields;
} siginfo_t;

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)

#include <linux/compat.h>

#define SI_PAD_SIZE32   ((SI_MAX_SIZE/sizeof(int)) - 3)

typedef union sigval32 {
	int sival_int;
	s32 sival_ptr;
} sigval_t32;

typedef struct siginfo32 {
	int si_signo;
	int si_code;
	int si_errno;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			compat_uid_t _uid;	/* sender's uid */
		} _kill;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;	/* which child */
			compat_uid_t _uid;	/* sender's uid */
			compat_clock_t _utime;
			int _status;		/* exit code */
			compat_clock_t _stime;
		} _sigchld;

		/* IRIX SIGCHLD */
		struct {
			compat_pid_t _pid;	/* which child */
			compat_clock_t _utime;
			int _status;		/* exit code */
			compat_clock_t _stime;
		} _irix_sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			s32 _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL, SIGXFSZ (To do ...)  */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			compat_uid_t _uid;	/* sender's uid */
			sigval_t32 _sigval;
		} _rt;

	} _sifields;
} siginfo_t32;

#endif /* defined(__KERNEL__) && defined(CONFIG_COMPAT) */

/*
 * si_code values
 * Again these have been choosen to be IRIX compatible.
 */
#undef SI_ASYNCIO
#undef SI_TIMER
#undef SI_MESGQ
#define SI_ASYNCIO	-2	/* sent by AIO completion */
#define SI_TIMER __SI_CODE(__SI_TIMER,-3) /* sent by timer expiration */
#define SI_MESGQ	-4	/* sent by real time mesq state change */

/*
 * sigevent definitions
 *
 * It seems likely that SIGEV_THREAD will have to be handled from
 * userspace, libpthread transmuting it to SIGEV_SIGNAL, which the
 * thread manager then catches and does the appropriate nonsense.
 * However, everything is written out here so as to not get lost.
 */
#undef SIGEV_NONE
#undef SIGEV_SIGNAL
#undef SIGEV_THREAD
#define SIGEV_NONE	128	/* other notification: meaningless */
#define SIGEV_SIGNAL	129	/* notify via signal */
#define SIGEV_CALLBACK	130	/* ??? */
#define SIGEV_THREAD	131	/* deliver via thread creation */

/* XXX This one isn't yet IRIX / ABI compatible.  */
typedef struct sigevent {
	int	sigev_notify;
	sigval_t	sigev_value;
	int	sigev_signo;
	union {
		int	_pad[SIGEV_PAD_SIZE];
		int	_tid;

		struct {
			void	(*_function)(sigval_t);
			void	*_attribute;	/* really pthread_attr_t */
		} _sigev_thread;
	} _sigev_un;
} sigevent_t;

#ifdef __KERNEL__

/*
 * Duplicated here because of <asm-generic/siginfo.h> braindamage ...
 */
#include <linux/string.h>

static inline void copy_siginfo(struct siginfo *to, struct siginfo *from)
{
	if (from->si_code < 0)
		memcpy(to, from, sizeof(*to));
	else
		/* _sigchld is currently the largest know union member */
		memcpy(to, from, 3*sizeof(int) + sizeof(from->_sifields._sigchld));
}

#endif

#endif /* _ASM_SIGINFO_H */
