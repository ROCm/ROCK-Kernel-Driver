/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: background.c,v 1.38 2003/05/26 09:50:38 dwmw2 Exp $
 *
 */

#define __KERNEL_SYSCALLS__

#include <linux/kernel.h>
#include <linux/jffs2.h>
#include <linux/mtd/mtd.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include "nodelist.h"


static int jffs2_garbage_collect_thread(void *);
static int thread_should_wake(struct jffs2_sb_info *c);

void jffs2_garbage_collect_trigger(struct jffs2_sb_info *c)
{
	spin_lock(&c->erase_completion_lock);
        if (c->gc_task && thread_should_wake(c))
                send_sig(SIGHUP, c->gc_task, 1);
	spin_unlock(&c->erase_completion_lock);
}

/* This must only ever be called when no GC thread is currently running */
int jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c)
{
	pid_t pid;
	int ret = 0;

	if (c->gc_task)
		BUG();

	init_MUTEX_LOCKED(&c->gc_thread_start);
	init_completion(&c->gc_thread_exit);

	pid = kernel_thread(jffs2_garbage_collect_thread, c, CLONE_FS|CLONE_FILES);
	if (pid < 0) {
		printk(KERN_WARNING "fork failed for JFFS2 garbage collect thread: %d\n", -pid);
		complete(&c->gc_thread_exit);
		ret = pid;
	} else {
		/* Wait for it... */
		D1(printk(KERN_DEBUG "JFFS2: Garbage collect thread is pid %d\n", pid));
		down(&c->gc_thread_start);
	}
 
	return ret;
}

void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c)
{
	if (c->mtd->type == MTD_NANDFLASH) {
		/* stop a eventually scheduled wbuf flush timer */
		del_timer_sync(&c->wbuf_timer);
		/* make sure, that a scheduled wbuf flush task is completed */
		flush_scheduled_work();
	}

	spin_lock(&c->erase_completion_lock);
	if (c->gc_task) {
		D1(printk(KERN_DEBUG "jffs2: Killing GC task %d\n", c->gc_task->pid));
		send_sig(SIGKILL, c->gc_task, 1);
	}
	spin_unlock(&c->erase_completion_lock);
	wait_for_completion(&c->gc_thread_exit);
}

static int jffs2_garbage_collect_thread(void *_c)
{
	struct jffs2_sb_info *c = _c;

	daemonize("jffs2_gcd_mtd%d", c->mtd->index);

	c->gc_task = current;
	up(&c->gc_thread_start);

	set_user_nice(current, 10);

	for (;;) {
		spin_lock_irq(&current_sig_lock);
		siginitsetinv (&current->blocked, sigmask(SIGHUP) | sigmask(SIGKILL) | sigmask(SIGSTOP) | sigmask(SIGCONT));
		recalc_sigpending();
		spin_unlock_irq(&current_sig_lock);

		if (!thread_should_wake(c)) {
			set_current_state (TASK_INTERRUPTIBLE);
			D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread sleeping...\n"));
			/* Yes, there's a race here; we checked thread_should_wake() before
			   setting current->state to TASK_INTERRUPTIBLE. But it doesn't
			   matter - We don't care if we miss a wakeup, because the GC thread
			   is only an optimisation anyway. */
			schedule();
		}

		cond_resched();

		/* Put_super will send a SIGKILL and then wait on the sem. 
		 */
		while (signal_pending(current)) {
			siginfo_t info;
			unsigned long signr;

			spin_lock_irq(&current_sig_lock);
			signr = dequeue_signal(current, &current->blocked, &info);
			spin_unlock_irq(&current_sig_lock);

			switch(signr) {
			case SIGSTOP:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): SIGSTOP received.\n"));
				set_current_state(TASK_STOPPED);
				schedule();
				break;

			case SIGKILL:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): SIGKILL received.\n"));
				spin_lock(&c->erase_completion_lock);
				c->gc_task = NULL;
				spin_unlock(&c->erase_completion_lock);
				complete_and_exit(&c->gc_thread_exit, 0);

			case SIGHUP:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): SIGHUP received.\n"));
				break;
			default:
				D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): signal %ld received\n", signr));
			}
		}
		/* We don't want SIGHUP to interrupt us. STOP and KILL are OK though. */
		spin_lock_irq(&current_sig_lock);
		siginitsetinv (&current->blocked, sigmask(SIGKILL) | sigmask(SIGSTOP) | sigmask(SIGCONT));
		recalc_sigpending();
		spin_unlock_irq(&current_sig_lock);

		D1(printk(KERN_DEBUG "jffs2_garbage_collect_thread(): pass\n"));
		jffs2_garbage_collect_pass(c);
	}
}

static int thread_should_wake(struct jffs2_sb_info *c)
{
	int ret = 0;
	uint32_t dirty;

	if (c->unchecked_size) {
		D1(printk(KERN_DEBUG "thread_should_wake(): unchecked_size %d, checked_ino #%d\n",
			  c->unchecked_size, c->checked_ino));
		return 1;
	}

	/* dirty_size contains blocks on erase_pending_list
	 * those blocks are counted in c->nr_erasing_blocks.
	 * If one block is actually erased, it is not longer counted as dirty_space
	 * but it is counted in c->nr_erasing_blocks, so we add it and subtract it
	 * with c->nr_erasing_blocks * c->sector_size again.
	 * Blocks on erasable_list are counted as dirty_size, but not in c->nr_erasing_blocks
	 * This helps us to force gc and pick eventually a clean block to spread the load.
	 */
	dirty = c->dirty_size + c->erasing_size - c->nr_erasing_blocks * c->sector_size;

	if (c->nr_free_blocks + c->nr_erasing_blocks < JFFS2_RESERVED_BLOCKS_GCTRIGGER && 
			(dirty > c->sector_size)) 
		ret = 1;

	D1(printk(KERN_DEBUG "thread_should_wake(): nr_free_blocks %d, nr_erasing_blocks %d, dirty_size 0x%x: %s\n", 
		  c->nr_free_blocks, c->nr_erasing_blocks, c->dirty_size, ret?"yes":"no"));

	return ret;
}
