/*
 * Copyright (c) 2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
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
#ifndef CPU_ARRAY_SIZE
	static char maskbuf[BITS_PER_LONG/4+8];
	sprintf(maskbuf, "0x%0lx", tp->cpus_allowed);
#else
	int i, j;
	static char maskbuf[CPU_ARRAY_SIZE * BITS_PER_LONG / 4 + 8];

	strcpy(maskbuf, "0x");
	for (j=2, i=CPU_ARRAY_SIZE-1; i >= 0; i--) {
		j += sprintf(maskbuf + j, "%0*lx", (int)(2*sizeof(tp->cpus_allowed.mask[0])), tp->cpus_allowed.mask[i]);
	}
#endif /* CPU_ARRAY_SIZE */

	return maskbuf;
}

static int
kdbm_task(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	unsigned long	addr;
	long		offset=0;
	int		nextarg;
	int		e = 0;
	struct task_struct *tp = NULL;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((e = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) != 0)
		return(e);

	if (!(tp = kmalloc(sizeof(*tp), GFP_ATOMIC))) {
	    kdb_printf("%s: cannot kmalloc tp\n", __FUNCTION__);
	    goto out;
	}
	if ((e = kdb_getarea(*tp, addr))) {
	    kdb_printf("%s: invalid task address\n", __FUNCTION__);
	    goto out;
	}

	kdb_printf(
	    "struct task at 0x%p, pid=%d flags=0x%lx state=%ld comm=\"%s\"\n",
	    tp, tp->pid, tp->flags, tp->state, tp->comm);

	kdb_printf("  cpu=%d policy=%lu ", kdb_process_cpu(tp), tp->policy);
	kdb_printf(
	    "prio=%d static_prio=%d cpus_allowed=%s",
	    tp->prio, tp->static_prio, kdb_cpus_allowed_string(tp));
	kdb_printf(" &thread=0x%p\n", &tp->thread);

        kdb_printf("  need_resched=%d ", test_tsk_thread_flag(tp, TIF_NEED_RESCHED));
        kdb_printf(
	    "timestamp=%llu time_slice=%u",
	    tp->timestamp, tp->time_slice);
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
	    tp->signal, &tp->blocked, &tp->pending);

	kdb_printf(
	    "  utime=%ld stime=%ld cutime=%ld cstime=%ld\n",
	    tp->utime, tp->stime, tp->cutime, tp->cstime);

out:
	if (tp)
	    kfree(tp);
	return e;
}

static int
kdbm_sigset(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	sigset_t	*sp = NULL;
	unsigned long	addr;
	long		offset=0;
	int		nextarg;
	int		e = 0;
	int		i;
	char		fmt[32];
	
	if (argc != 1)
		return KDB_ARGCOUNT;

#ifndef _NSIG_WORDS
        kdb_printf("unavailable on this platform, _NSIG_WORDS not defined.\n");
#else
	nextarg = 1;
	if ((e = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) != 0)
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

kdb_module_init(kdbm_task_init)
kdb_module_exit(kdbm_task_exit)
