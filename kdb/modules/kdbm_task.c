/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/blkdev.h>
#include <linux/types.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/signal.h>

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug struct task and sigset information");
MODULE_LICENSE("GPL");

static char *
kdb_cpus_allowed_string(struct task_struct *tp)
{
	static char maskbuf[NR_CPUS * 8];
	if (cpus_equal(tp->cpus_allowed, cpu_online_map))
		strcpy(maskbuf, "ALL");
	else if (cpus_full(tp->cpus_allowed))
		strcpy(maskbuf, "ALL(NR_CPUS)");
	else if (cpus_empty(tp->cpus_allowed))
		strcpy(maskbuf, "NONE");
	else if (cpus_weight(tp->cpus_allowed) == 1)
		snprintf(maskbuf, sizeof(maskbuf), "ONLY(%d)", first_cpu(tp->cpus_allowed));
	else
		cpulist_scnprintf(maskbuf, sizeof(maskbuf), tp->cpus_allowed);
	return maskbuf;
}

static int
kdbm_task(int argc, const char **argv)
{
	unsigned long addr;
	long offset=0;
	int nextarg;
	int e = 0;
	struct task_struct *tp = NULL, *tp1;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((e = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) != 0)
		return(e);

	if (!(tp = kmalloc(sizeof(*tp), GFP_ATOMIC))) {
	    kdb_printf("%s: cannot kmalloc tp\n", __FUNCTION__);
	    goto out;
	}
	if ((e = kdb_getarea(*tp, addr))) {
	    kdb_printf("%s: invalid task address\n", __FUNCTION__);
	    goto out;
	}

	tp1 = (struct task_struct *)addr;
	kdb_printf(
	    "struct task at 0x%lx, pid=%d flags=0x%x state=%ld comm=\"%s\"\n",
	    addr, tp->pid, tp->flags, tp->state, tp->comm);

	kdb_printf("  cpu=%d policy=%u ", kdb_process_cpu(tp), tp->policy);
	kdb_printf(
	    "prio=%d static_prio=%d cpus_allowed=",
	    tp->prio, tp->static_prio);
	{
		/* The cpus allowed string may be longer than kdb_printf() can
		 * handle.  Print it in chunks.
		 */
		char c, *p;
		p = kdb_cpus_allowed_string(tp);
		while (1) {
			if (strlen(p) < 100) {
				kdb_printf("%s", p);
				break;
			}
			c = p[100];
			p[100] = '\0';
			kdb_printf("%s", p);
			p[100] = c;
			p += 100;
		}
	}
	kdb_printf(" &thread=0x%p\n", &tp1->thread);

	kdb_printf("  need_resched=%d ",
		test_tsk_thread_flag(tp, TIF_NEED_RESCHED));
	kdb_printf(
	    "time_slice=%u",
	    tp->time_slice);
	kdb_printf(" lock_depth=%d\n", tp->lock_depth);

	kdb_printf(
	    "  fs=0x%p files=0x%p mm=0x%p\n",
	    tp->fs, tp->files, tp->mm);

	kdb_printf(
	    "  uid=%d euid=%d suid=%d fsuid=%d gid=%d egid=%d sgid=%d fsgid=%d\n",
	    tp->uid, tp->euid, tp->suid, tp->fsuid, tp->gid, tp->egid, tp->sgid, tp->fsgid);

	kdb_printf(
	    "  user=0x%p\n",
	    tp->user);

	if (tp->sysvsem.undo_list)
		kdb_printf(
		    "  sysvsem.sem_undo refcnt %d proc_list=0x%p\n",
		    atomic_read(&tp->sysvsem.undo_list->refcnt),
		    tp->sysvsem.undo_list->proc_list);

	kdb_printf(
	    "  signal=0x%p &blocked=0x%p &pending=0x%p\n",
	    tp->signal, &tp1->blocked, &tp1->pending);

	kdb_printf(
	    "  utime=%ld stime=%ld cutime=%ld cstime=%ld\n",
	    tp->utime, tp->stime,
	    tp->signal ? tp->signal->cutime : 0L,
	    tp->signal ? tp->signal->cstime : 0L);

	kdb_printf("  thread_info=0x%p\n", task_thread_info(tp));
	kdb_printf("  ti flags=0x%lx\n", (unsigned long)task_thread_info(tp)->flags);

out:
	if (tp)
	    kfree(tp);
	return e;
}

static int
kdbm_sigset(int argc, const char **argv)
{
	sigset_t *sp = NULL;
	unsigned long addr;
	long offset=0;
	int nextarg;
	int e = 0;
	int i;
	char fmt[32];

	if (argc != 1)
		return KDB_ARGCOUNT;

#ifndef _NSIG_WORDS
	kdb_printf("unavailable on this platform, _NSIG_WORDS not defined.\n");
#else
	nextarg = 1;
	if ((e = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) != 0)
		return(e);

	if (!(sp = kmalloc(sizeof(*sp), GFP_ATOMIC))) {
	    kdb_printf("%s: cannot kmalloc sp\n", __FUNCTION__);
	    goto out;
	}
	if ((e = kdb_getarea(*sp, addr))) {
	    kdb_printf("%s: invalid sigset address\n", __FUNCTION__);
	    goto out;
	}

	sprintf(fmt, "[%%d]=0x%%0%dlx ", (int)sizeof(sp->sig[0])*2);
	kdb_printf("sigset at 0x%p : ", sp);
	for (i=_NSIG_WORDS-1; i >= 0; i--) {
	    if (i == 0 || sp->sig[i]) {
		kdb_printf(fmt, i, sp->sig[i]);
	    }
	}
	kdb_printf("\n");
#endif /* _NSIG_WORDS */

out:
	if (sp)
	    kfree(sp);
	return e;
}

static int __init kdbm_task_init(void)
{
	kdb_register("task", kdbm_task, "<vaddr>", "Display task_struct", 0);
	kdb_register("sigset", kdbm_sigset, "<vaddr>", "Display sigset_t", 0);

	return 0;
}

static void __exit kdbm_task_exit(void)
{
	kdb_unregister("task");
	kdb_unregister("sigset");
}

module_init(kdbm_task_init)
module_exit(kdbm_task_exit)
