/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from kswapd()
 *  in linux/mm/vmscan.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/timex.h>

/* #define DEBUG */

/**
 * int_sqrt - oom_kill.c internal function, rough approximation to sqrt
 * @x: integer of which to calculate the sqrt
 * 
 * A very rough approximation to the sqrt() function.
 */
static unsigned int int_sqrt(unsigned int x)
{
	unsigned int out = x;
	while (x & ~(unsigned int)1) x >>=2, out >>=1;
	if (x) out -= out >> 2;
	return (out ? out : 1);
}	

/**
 * oom_badness - calculate a numeric value for how bad this task has been
 * @p: task struct of which task we should calculate
 *
 * The formula used is relatively simple and documented inline in the
 * function. The main rationale is that we want to select a good task
 * to kill when we run out of memory.
 *
 * Good in this context means that:
 * 1) we lose the minimum amount of work done
 * 2) we recover a large amount of memory
 * 3) we don't kill anything innocent of eating tons of memory
 * 4) we want to kill the minimum amount of processes (one)
 * 5) we try to kill the process the user expects us to kill, this
 *    algorithm has been meticulously tuned to meet the priniciple
 *    of least surprise ... (be careful when you change it)
 */

static int badness(struct task_struct *p)
{
	int points, cpu_time, run_time;

	if (!p->mm)
		return 0;
	/*
	 * The memory size of the process is the basis for the badness.
	 */
	points = p->mm->total_vm;

	/*
	 * CPU time is in seconds and run time is in minutes. There is no
	 * particular reason for this other than that it turned out to work
	 * very well in practice. This is not safe against jiffie wraps
	 * but we don't care _that_ much...
	 */
	cpu_time = (p->times.tms_utime + p->times.tms_stime) >> (SHIFT_HZ + 3);
	run_time = (jiffies - p->start_time) >> (SHIFT_HZ + 10);

	points /= int_sqrt(cpu_time);
	points /= int_sqrt(int_sqrt(run_time));

	/*
	 * Niced processes are most likely less important, so double
	 * their badness points.
	 */
	if (p->nice > 0)
		points *= 2;

	/*
	 * Superuser processes are usually more important, so we make it
	 * less likely that we kill those.
	 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_ADMIN) ||
				p->uid == 0 || p->euid == 0)
		points /= 4;

	/*
	 * We don't want to kill a process with direct hardware access.
	 * Not only could that mess up the hardware, but usually users
	 * tend to only have this flag set on applications they think
	 * of as important.
	 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_RAWIO))
		points /= 4;
#ifdef DEBUG
	printk(KERN_DEBUG "OOMkill: task %d (%s) got %d points\n",
	p->pid, p->comm, points);
#endif
	return points;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'. We need the locks to make sure that the
 * list of task structs doesn't change while we look the other way.
 *
 * (not docbooked, we don't want this one cluttering up the manual)
 */
static struct task_struct * select_bad_process(void)
{
	int maxpoints = 0;
	struct task_struct *p = NULL;
	struct task_struct *chosen = NULL;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (p->pid) {
			int points = badness(p);
			if (points > maxpoints) {
				chosen = p;
				maxpoints = points;
			}
		}
	}
	read_unlock(&tasklist_lock);
	return chosen;
}

/**
 * oom_kill - kill the "best" process when we run out of memory
 *
 * If we run out of memory, we have the choice between either
 * killing a random task (bad), letting the system crash (worse)
 * OR try to be smart about which process to kill. Note that we
 * don't have to be perfect here, we just have to be good.
 *
 * We must be careful though to never send SIGKILL a process with
 * CAP_SYS_RAW_IO set, send SIGTERM instead (but it's unlikely that
 * we select a process with CAP_SYS_RAW_IO set).
 */
void oom_kill(void)
{

	struct task_struct *p = select_bad_process();

	/* Found nothing?!?! Either we hang forever, or we panic. */
	if (p == NULL)
		panic("Out of memory and no killable processes...\n");

	printk(KERN_ERR "Out of Memory: Killed process %d (%s).\n", p->pid, p->comm);

	/*
	 * We give our sacrificial lamb high priority and access to
	 * all the memory it needs. That way it should be able to
	 * exit() and clear out its resources quickly...
	 */
	p->counter = 5 * HZ;
	p->flags |= PF_MEMALLOC;

	/* This process has hardware access, be more careful. */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_RAWIO)) {
		force_sig(SIGTERM, p);
	} else {
		force_sig(SIGKILL, p);
	}

	/*
	 * Make kswapd go out of the way, so "p" has a good chance of
	 * killing itself before someone else gets the chance to ask
	 * for more memory.
	 */
	current->policy |= SCHED_YIELD;
	schedule();
	return;
}

/**
 * out_of_memory - is the system out of memory?
 *
 * Returns 0 if there is still enough memory left,
 * 1 when we are out of memory (otherwise).
 */
int out_of_memory(void)
{
	struct sysinfo swp_info;

	/* Enough free memory?  Not OOM. */
	if (nr_free_pages() > freepages.min)
		return 0;

	if (nr_free_pages() + nr_inactive_clean_pages() > freepages.low)
		return 0;

	/* Enough swap space left?  Not OOM. */
	si_swapinfo(&swp_info);
	if (swp_info.freeswap > 0)
		return 0;

	/* Else... */
	return 1;
}
