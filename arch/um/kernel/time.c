/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "process.h"
#include "signal_user.h"

extern struct timeval xtime;

void timer_handler(int sig, struct uml_pt_regs *regs)
{
	timer_irq(regs);
}

void timer(void)
{
	gettimeofday(&xtime, NULL);
}

static struct itimerval profile_interval;

void get_profile_timer(void)
{
	getitimer(ITIMER_PROF, &profile_interval);
	profile_interval.it_value = profile_interval.it_interval;
}

void disable_profile_timer(void)
{
	struct itimerval interval = ((struct itimerval) { { 0, 0 }, { 0, 0 }});
	setitimer(ITIMER_PROF, &interval, NULL);
}

static void set_interval(int timer_type)
{
	struct itimerval interval;

	interval.it_interval.tv_sec = 0;
	interval.it_interval.tv_usec = 1000000/hz();
	interval.it_value.tv_sec = 0;
	interval.it_value.tv_usec = 1000000/hz();
	if(setitimer(timer_type, &interval, NULL) == -1)
		panic("setitimer failed - errno = %d\n", errno);
}

void idle_timer(void)
{
	if(signal(SIGVTALRM, SIG_IGN) == SIG_ERR)
		panic("Couldn't unset SIGVTALRM handler");
	set_handler(SIGALRM, (__sighandler_t) alarm_handler, 
		    SA_NODEFER | SA_RESTART, SIGUSR1, SIGIO, SIGWINCH, -1);
	set_interval(ITIMER_REAL);
}

void user_time_init(void)
{
	if(signal(SIGVTALRM, (__sighandler_t) alarm_handler) == SIG_ERR)
		panic("Couldn't set SIGVTALRM handler");
	set_interval(ITIMER_VIRTUAL);
}

void time_init(void)
{
	if(signal(SIGVTALRM, boot_timer_handler) == SIG_ERR)
		panic("Couldn't set SIGVTALRM handler");
	set_interval(ITIMER_VIRTUAL);
}

void set_timers(int set_signal)
{
	if(set_signal)
		set_interval(ITIMER_VIRTUAL);
	if(setitimer(ITIMER_PROF, &profile_interval, NULL) == -1)
		panic("setitimer ITIMER_PROF failed - errno = %d\n", errno);
}

struct timeval local_offset = { 0, 0 };

void do_gettimeofday(struct timeval *tv)
{
	gettimeofday(tv, NULL);
	timeradd(tv, &local_offset, tv);
}

void do_settimeofday(struct timeval *tv)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	timersub(tv, &now, &local_offset);
}

void idle_sleep(int secs)
{
	struct timespec ts;

	ts.tv_sec = secs;
	ts.tv_nsec = 0;
	nanosleep(&ts, &ts);
}

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
