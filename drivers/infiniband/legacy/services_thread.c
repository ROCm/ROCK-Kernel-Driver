/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: services_thread.c 32 2004-04-09 03:57:42Z roland $
*/

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include "ts_kernel_thread.h"
#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"

#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/smp_lock.h>

#include <asm/semaphore.h>

typedef struct tTS_KERNEL_QUEUE_ENTRY_STRUCT tTS_KERNEL_QUEUE_ENTRY_STRUCT,
  *tTS_KERNEL_QUEUE_ENTRY;

struct tTS_KERNEL_THREAD_STRUCT {
  int                        pid;
  struct semaphore           init_sem;
  struct completion          done;
  char                       name[sizeof ((struct task_struct *) 0)->comm];
  tTS_KERNEL_THREAD_FUNCTION function;
  void *                     arg;
};

struct tTS_KERNEL_QUEUE_THREAD_STRUCT {
  tTS_KERNEL_THREAD                thread;
  spinlock_t                       lock;
  struct list_head                 list;
  wait_queue_head_t                wait;
  tTS_KERNEL_QUEUE_THREAD_FUNCTION function;
  void                            *arg;
};

static void _tsKernelThreadTimeout(
                                   unsigned long arg
                                   ) {
  tTS_KERNEL_THREAD thread = (tTS_KERNEL_THREAD) arg;

  TS_REPORT_WARN(MOD_SYS,
                 "wait_for_completion (thread %s) timed out",
                 thread->name);
}

static int _tsKernelThreadStart(
                                void *thread_ptr
                                ) {
  tTS_KERNEL_THREAD thread = thread_ptr;

  lock_kernel();
#ifdef TS_KERNEL_2_6
  daemonize(thread->name);
#else
  daemonize();
  strncpy(current->comm, thread->name, sizeof current->comm);
  current->comm[sizeof current->comm - 1] = '\0';
#endif
  unlock_kernel();

#ifndef TS_KERNEL_2_6
  reparent_to_init();
#endif

#if defined(INIT_SIGHAND) || defined(TS_KERNEL_2_6)
  /* 2.4.20-8/9 kernels */
  spin_lock_irq(&current->sighand->siglock);
  siginitsetinv(&current->blocked, sigmask(SIGUSR1));
  recalc_sigpending();
  spin_unlock_irq(&current->sighand->siglock);

#else
  spin_lock_irq(&current->sigmask_lock);
  siginitsetinv(&current->blocked, sigmask(SIGUSR1));
  recalc_sigpending(current);
  spin_unlock_irq(&current->sigmask_lock);
#endif

  /* done initializing, let tsKernelThreadStart return */
  up(&thread->init_sem);

  thread->function(thread->arg);

  complete_and_exit(&thread->done, 0);
}

int tsKernelThreadStart(
                        const char *name,
                        tTS_KERNEL_THREAD_FUNCTION function,
                        void *arg,
                        tTS_KERNEL_THREAD *thread
                        ) {
  *thread = kmalloc(sizeof **thread, GFP_KERNEL);
  if (!*thread) {
    return -ENOMEM;
  }

  strncpy((*thread)->name, name, sizeof (*thread)->name);
  (*thread)->name[sizeof (*thread)->name - 1] = '\0';

  sema_init(&(*thread)->init_sem, 0);
  init_completion(&(*thread)->done);

  (*thread)->function = function;
  (*thread)->arg      = arg;

#ifdef CLONE_KERNEL
  (*thread)->pid = kernel_thread(_tsKernelThreadStart, *thread, CLONE_KERNEL);
#else
  (*thread)->pid = kernel_thread(_tsKernelThreadStart, *thread,
                                 CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
#endif
  if ((*thread)->pid < 0) {
    int ret = (*thread)->pid;
    kfree(*thread);
    *thread = NULL;
    return ret;
  }

  /* wait for thread to initialize before we return */
  down_interruptible(&(*thread)->init_sem);

  return 0;
}

int tsKernelThreadStop(
                       tTS_KERNEL_THREAD thread
                       ) {
  int ret = 0;
  struct timer_list debug_timer;

  init_timer(&debug_timer);
  /* Use SIGUSR1 so that we can block SIGSTOP/SIGKILL (which are used
     by shutdown scripts etc -- cf. bug 3077) */
  ret = kill_proc(thread->pid, SIGUSR1, 1);
  if (ret) {
    goto out;
  }

  debug_timer.function = _tsKernelThreadTimeout;
  debug_timer.expires  = jiffies + 10 * HZ;
  debug_timer.data     = (unsigned long) thread;
  add_timer(&debug_timer);
  wait_for_completion(&thread->done);
  del_timer_sync(&debug_timer);

 out:
  kfree(thread);
  return ret;
}

static void _tsKernelQueueThread(
                                 void *thread_ptr
                                 ) {
  tTS_KERNEL_QUEUE_THREAD thread = thread_ptr;
  struct list_head       *entry;
  int ret;

  while (!signal_pending(current)) {
    ret = wait_event_interruptible(thread->wait,
                                   !list_empty(&thread->list));
    if (ret) {
      TS_REPORT_CLEANUP(MOD_SYS,
                        "queue thread exiting");
      return;
    }

    spin_lock_irq(&thread->lock);
    entry = thread->list.next;
    list_del(entry);
    spin_unlock_irq(&thread->lock);

    thread->function(entry, thread->arg);
  }
}

int tsKernelQueueThreadStart(
                             const char                      *name,
                             tTS_KERNEL_QUEUE_THREAD_FUNCTION function,
                             void                            *arg,
                             tTS_KERNEL_QUEUE_THREAD         *thread
                             ) {
  int ret;

  *thread = kmalloc(sizeof **thread, GFP_KERNEL);
  if (!*thread) {
    return -ENOMEM;
  }

  spin_lock_init(&(*thread)->lock);
  INIT_LIST_HEAD(&(*thread)->list);
  init_waitqueue_head(&(*thread)->wait);
  (*thread)->function = function;
  (*thread)->arg      = arg;

  ret = tsKernelThreadStart(name, _tsKernelQueueThread, *thread, &(*thread)->thread);
  if (ret) {
    kfree(thread);
  }

  return ret;
}

int tsKernelQueueThreadStop(
                            tTS_KERNEL_QUEUE_THREAD thread
                            ) {
  int ret = tsKernelThreadStop(thread->thread);
  kfree(thread);
  return ret;
}

void tsKernelQueueThreadAdd(
                            tTS_KERNEL_QUEUE_THREAD thread,
                            struct list_head       *entry
                            ) {
  unsigned long flags;

  spin_lock_irqsave(&thread->lock, flags);
  list_add_tail(entry, &thread->list);
  spin_unlock_irqrestore(&thread->lock, flags);

  wake_up_interruptible(&thread->wait);
}
