/*
 *  linux/fs/proc/array.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 * Fixes:
 * Michael. K. Johnson: stat,statm extensions.
 *                      <johnsonm@stolaf.edu>
 *
 * Pauline Middelink :  Made cmdline,envline only break at '\0's, to
 *                      make sure SET_PROCTITLE works. Also removed
 *                      bad '!' which forced address recalculation for
 *                      EVERY character on the current page.
 *                      <middelin@polyware.iaf.nl>
 *
 * Danny ter Haar    :	added cpuinfo
 *			<dth@cistron.nl>
 *
 * Alessandro Rubini :  profile extension.
 *                      <rubini@ipvvis.unipv.it>
 *
 * Jeff Tranter      :  added BogoMips field to cpuinfo
 *                      <Jeff_Tranter@Mitel.COM>
 *
 * Bruno Haible      :  remove 4K limit for the maps file
 *			<haible@ma2s2.mathematik.uni-karlsruhe.de>
 *
 * Yves Arrouye      :  remove removal of trailing spaces in get_array.
 *			<Yves.Arrouye@marin.fdn.fr>
 *
 * Jerome Forissier  :  added per-CPU time information to /proc/stat
 *                      and /proc/<pid>/cpu extension
 *                      <forissier@isia.cma.fr>
 *			- Incorporation and non-SMP safe operation
 *			of forissier patch in 2.1.78 by
 *			Hans Marcus <crowbar@concepts.nl>
 *
 * aeb@cwi.nl        :  /proc/partitions
 *
 *
 * Alan Cox	     :  security fixes.
 *			<Alan.Cox@linux.org>
 *
 * Al Viro           :  safe handling of mm_struct
 *
 * Gerhard Wichert   :  added BIGMEM support
 * Siemens AG           <Gerhard.Wichert@pdb.siemens.de>
 *
 * Al Viro & Jeff Garzik :  moved most of the thing into base.c and
 *			 :  proc_misc.c. The rest may eventually go into
 *			 :  base.c too.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/times.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/processor.h>

/* Gcc optimizes away "strlen(x)" for constant x */
#define ADDBUF(buffer, string) \
do { memcpy(buffer, string, strlen(string)); \
     buffer += strlen(string); } while (0)

static inline char * task_name(struct task_struct *p, char * buf)
{
	int i;
	char * name;

	ADDBUF(buf, "Name:\t");
	name = p->comm;
	i = sizeof(p->comm);
	do {
		unsigned char c = *name;
		name++;
		i--;
		*buf = c;
		if (!c)
			break;
		if (c == '\\') {
			buf[1] = c;
			buf += 2;
			continue;
		}
		if (c == '\n') {
			buf[0] = '\\';
			buf[1] = 'n';
			buf += 2;
			continue;
		}
		buf++;
	} while (i);
	*buf = '\n';
	return buf+1;
}

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const char *task_state_array[] = {
	"R (running)",		/*  0 */
	"S (sleeping)",		/*  1 */
	"D (disk sleep)",	/*  2 */
	"T (stopped)",		/*  4 */
	"Z (zombie)",		/*  8 */
	"X (dead)"		/* 16 */
};

static inline const char * get_task_state(struct task_struct *tsk)
{
	unsigned int state = tsk->state & (TASK_RUNNING |
					   TASK_INTERRUPTIBLE |
					   TASK_UNINTERRUPTIBLE |
					   TASK_ZOMBIE |
					   TASK_STOPPED);
	const char **p = &task_state_array[0];

	while (state) {
		p++;
		state >>= 1;
	}
	return *p;
}

static inline char * task_state(struct task_struct *p, char *buffer)
{
	int g;

	read_lock(&tasklist_lock);
	buffer += sprintf(buffer,
		"State:\t%s\n"
		"SleepAVG:\t%lu%%\n"
		"Tgid:\t%d\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"TracerPid:\t%d\n"
		"Uid:\t%d\t%d\t%d\t%d\n"
		"Gid:\t%d\t%d\t%d\t%d\n",
		get_task_state(p),
		(p->sleep_avg/1024)*100/(1020000000/1024),
	       	p->tgid,
		p->pid, p->pid ? p->real_parent->pid : 0,
		p->pid && p->ptrace ? p->parent->pid : 0,
		p->uid, p->euid, p->suid, p->fsuid,
		p->gid, p->egid, p->sgid, p->fsgid);
	read_unlock(&tasklist_lock);
	task_lock(p);
	buffer += sprintf(buffer,
		"FDSize:\t%d\n"
		"Groups:\t",
		p->files ? p->files->max_fds : 0);
	task_unlock(p);

	get_group_info(p->group_info);
	for (g = 0; g < min(p->group_info->ngroups,NGROUPS_SMALL); g++)
		buffer += sprintf(buffer, "%d ", GROUP_AT(p->group_info,g));
	put_group_info(p->group_info);

	buffer += sprintf(buffer, "\n");
	return buffer;
}

static char * render_sigset_t(const char *header, sigset_t *set, char *buffer)
{
	int i, len;

	len = strlen(header);
	memcpy(buffer, header, len);
	buffer += len;

	i = _NSIG;
	do {
		int x = 0;

		i -= 4;
		if (sigismember(set, i+1)) x |= 1;
		if (sigismember(set, i+2)) x |= 2;
		if (sigismember(set, i+3)) x |= 4;
		if (sigismember(set, i+4)) x |= 8;
		*buffer++ = (x < 10 ? '0' : 'a' - 10) + x;
	} while (i >= 4);

	*buffer++ = '\n';
	*buffer = 0;
	return buffer;
}

static void collect_sigign_sigcatch(struct task_struct *p, sigset_t *ign,
				    sigset_t *catch)
{
	struct k_sigaction *k;
	int i;

	k = p->sighand->action;
	for (i = 1; i <= _NSIG; ++i, ++k) {
		if (k->sa.sa_handler == SIG_IGN)
			sigaddset(ign, i);
		else if (k->sa.sa_handler != SIG_DFL)
			sigaddset(catch, i);
	}
}

static inline char * task_sig(struct task_struct *p, char *buffer)
{
	sigset_t pending, shpending, blocked, ignored, caught;
	int num_threads = 0;

	sigemptyset(&pending);
	sigemptyset(&shpending);
	sigemptyset(&blocked);
	sigemptyset(&ignored);
	sigemptyset(&caught);

	/* Gather all the data with the appropriate locks held */
	read_lock(&tasklist_lock);
	if (p->sighand) {
		spin_lock_irq(&p->sighand->siglock);
		pending = p->pending.signal;
		shpending = p->signal->shared_pending.signal;
		blocked = p->blocked;
		collect_sigign_sigcatch(p, &ignored, &caught);
		num_threads = atomic_read(&p->signal->count);
		spin_unlock_irq(&p->sighand->siglock);
	}
	read_unlock(&tasklist_lock);

	buffer += sprintf(buffer, "Threads:\t%d\n", num_threads);

	/* render them all */
	buffer = render_sigset_t("SigPnd:\t", &pending, buffer);
	buffer = render_sigset_t("ShdPnd:\t", &shpending, buffer);
	buffer = render_sigset_t("SigBlk:\t", &blocked, buffer);
	buffer = render_sigset_t("SigIgn:\t", &ignored, buffer);
	buffer = render_sigset_t("SigCgt:\t", &caught, buffer);

	return buffer;
}

static inline char *task_cap(struct task_struct *p, char *buffer)
{
    return buffer + sprintf(buffer, "CapInh:\t%016x\n"
			    "CapPrm:\t%016x\n"
			    "CapEff:\t%016x\n",
			    cap_t(p->cap_inheritable),
			    cap_t(p->cap_permitted),
			    cap_t(p->cap_effective));
}

extern char *task_mem(struct mm_struct *, char *);
int proc_pid_status(struct task_struct *task, char * buffer)
{
	char * orig = buffer;
	struct mm_struct *mm = get_task_mm(task);

	buffer = task_name(task, buffer);
	buffer = task_state(task, buffer);
 
	if (mm) {
		buffer = task_mem(mm, buffer);
		mmput(mm);
	}
	buffer = task_sig(task, buffer);
	buffer = task_cap(task, buffer);
#if defined(CONFIG_ARCH_S390)
	buffer = task_show_regs(task, buffer);
#endif
	return buffer - orig;
}

extern unsigned long task_vsize(struct mm_struct *);
int proc_pid_stat(struct task_struct *task, char * buffer)
{
	unsigned long vsize, eip, esp, wchan;
	long priority, nice;
	int tty_pgrp = -1, tty_nr = 0;
	sigset_t sigign, sigcatch;
	char state;
	int res;
 	pid_t ppid, pgid = -1, sid = -1;
	int num_threads = 0;
	struct mm_struct *mm;
	unsigned long long start_time;

	state = *get_task_state(task);
	vsize = eip = esp = 0;
	task_lock(task);
	mm = task->mm;
	if(mm)
		mm = mmgrab(mm);
	task_unlock(task);
	if (mm) {
		down_read(&mm->mmap_sem);
		vsize = task_vsize(mm);
		eip = KSTK_EIP(task);
		esp = KSTK_ESP(task);
		up_read(&mm->mmap_sem);
	}

	wchan = get_wchan(task);

	sigemptyset(&sigign);
	sigemptyset(&sigcatch);
	read_lock(&tasklist_lock);
	if (task->sighand) {
		spin_lock_irq(&task->sighand->siglock);
		num_threads = atomic_read(&task->signal->count);
		collect_sigign_sigcatch(task, &sigign, &sigcatch);
		spin_unlock_irq(&task->sighand->siglock);
	}
	if (task->signal) {
		if (task->signal->tty) {
			tty_pgrp = task->signal->tty->pgrp;
			tty_nr = new_encode_dev(tty_devnum(task->signal->tty));
		}
		pgid = process_group(task);
		sid = task->signal->session;
	}
	read_unlock(&tasklist_lock);

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = task_prio(task);
	nice = task_nice(task);

	read_lock(&tasklist_lock);
	ppid = task->pid ? task->real_parent->pid : 0;
	read_unlock(&tasklist_lock);

	/* Temporary variable needed for gcc-2.96 */
	start_time = jiffies_64_to_clock_t(task->start_time - INITIAL_JIFFIES);

	res = sprintf(buffer,"%d (%s) %c %d %d %d %d %d %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %d %ld %llu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %lu\n",
		task->pid,
		task->comm,
		state,
		ppid,
		pgid,
		sid,
		tty_nr,
		tty_pgrp,
		task->flags,
		task->min_flt,
		task->cmin_flt,
		task->maj_flt,
		task->cmaj_flt,
		jiffies_to_clock_t(task->utime),
		jiffies_to_clock_t(task->stime),
		jiffies_to_clock_t(task->cutime),
		jiffies_to_clock_t(task->cstime),
		priority,
		nice,
		num_threads,
		jiffies_to_clock_t(task->it_real_value),
		start_time,
		vsize,
		mm ? mm->rss : 0, /* you might want to shift this left 3 */
		task->rlim[RLIMIT_RSS].rlim_cur,
		mm ? mm->start_code : 0,
		mm ? mm->end_code : 0,
		mm ? mm->start_stack : 0,
		esp,
		eip,
		/* The signal information here is obsolete.
		 * It must be decimal for Linux 2.0 compatibility.
		 * Use /proc/#/status for real-time signals.
		 */
		task->pending.signal.sig[0] & 0x7fffffffUL,
		task->blocked.sig[0] & 0x7fffffffUL,
		sigign      .sig[0] & 0x7fffffffUL,
		sigcatch    .sig[0] & 0x7fffffffUL,
		wchan,
		0UL,
		0UL,
		task->exit_signal,
		task_cpu(task),
		task->rt_priority,
		task->policy);
	if(mm)
		mmput(mm);
	return res;
}

extern int task_statm(struct mm_struct *, int *, int *, int *, int *);
int proc_pid_statm(struct task_struct *task, char *buffer)
{
	int size = 0, resident = 0, shared = 0, text = 0, lib = 0, data = 0;
	struct mm_struct *mm = get_task_mm(task);
	
	if (mm) {
		down_read(&mm->mmap_sem);
		size = task_statm(mm, &shared, &text, &data, &resident);
		up_read(&mm->mmap_sem);

		mmput(mm);
	}

	return sprintf(buffer,"%d %d %d %d %d %d %d\n",
		       size, resident, shared, text, lib, data, 0);
}
