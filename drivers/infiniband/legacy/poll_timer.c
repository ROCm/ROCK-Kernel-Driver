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

  $Id: poll_timer.c 32 2004-04-09 03:57:42Z roland $
*/

#ifdef W2K_OS // Vipul
#include <ntddk.h>
#include "all/common/include/w2k.h"
#endif
#include "poll.h"

#include "ts_kernel_poll.h"
#include "ts_kernel_timer.h"

#include "ts_kernel_trace.h"
#ifndef W2K_OS // Vipul
#include <linux/sched.h>
#include <linux/spinlock.h>
#endif
enum {
  TS_NUMBER_OF_TIMER_LISTS = 4,

  TS_TIMER_LIST_BITS = 8,
  TS_TIMER_LIST_SIZE = (1 << TS_TIMER_LIST_BITS),
  TS_TIMER_LIST_MASK = TS_TIMER_LIST_SIZE - 1
};

struct tTS_KERNEL_TIMER_LIST_STRUCT {
  struct list_head timer_list[TS_TIMER_LIST_SIZE];
  int              cur_list;
};

static struct tTS_KERNEL_TIMER_LIST_STRUCT tls[TS_NUMBER_OF_TIMER_LISTS];
#ifdef W2K_OS // Vipul
static spinlock_t tls_lock;
#else
static spinlock_t tls_lock = SPIN_LOCK_UNLOCKED;
#endif
static tTS_KERNEL_POLL_HANDLE timer_poll;
static unsigned long          timer_jiffies;

static inline void _tsKernelTimerInsert(
                                        tTS_KERNEL_TIMER timer
                                        ) {
  unsigned long interval = timer->run_time - timer_jiffies;
  struct list_head *timer_list;

  if (interval < TS_TIMER_LIST_SIZE) {
    int i      = timer->run_time & TS_TIMER_LIST_MASK;
    timer_list = tls[0].timer_list + i;
  } else if (interval < 1 << (TS_TIMER_LIST_BITS * 2)) {
    int i      = (timer->run_time >> TS_TIMER_LIST_BITS) & TS_TIMER_LIST_MASK;
    timer_list = tls[1].timer_list + i;
  } else if (interval < 1 << (TS_TIMER_LIST_BITS * 3)) {
    int i      = (timer->run_time >> (TS_TIMER_LIST_BITS * 2)) & TS_TIMER_LIST_MASK;
    timer_list = tls[2].timer_list + i;
  } else if ((long) interval < 0) {
    timer_list = tls[0].timer_list + tls[0].cur_list;
  } else {
    int i      = (timer->run_time >> (TS_TIMER_LIST_BITS * 3)) & TS_TIMER_LIST_MASK;
    timer_list = tls[3].timer_list + i;
  }

  list_add_tail(&timer->list, timer_list);
}

static inline void _tsKernelTimerDetach(
                                        tTS_KERNEL_TIMER timer
                                        ) {
  if (tsKernelTimerPending(timer)) {
    list_del(&timer->list);
  }
}

static inline void _tsKernelTimerCascade(
                                         void
                                         ) {
  int n = 1;

  do {
    struct list_head *timer_list;
    struct list_head *cur;
    struct list_head *next;

    timer_list = tls[n].timer_list + tls[n].cur_list;

    for (cur = timer_list->next; cur != timer_list; cur = next) {
      tTS_KERNEL_TIMER timer;

      timer = list_entry(cur, tTS_KERNEL_TIMER_STRUCT, list);
      next  = cur->next;

      _tsKernelTimerInsert(timer);
    }

    INIT_LIST_HEAD(timer_list);

    tls[n].cur_list = (tls[n].cur_list + 1) & TS_TIMER_LIST_MASK;
  } while (tls[n].cur_list == 1
           && ++n < TS_NUMBER_OF_TIMER_LISTS);
}

static int _tsKernelTimerRun(
                             void *arg
                             ) {
  int work = 0;
#ifdef W2K_OS // Vipul
  int flags;
  spin_lock_irqsave(&tls_lock, flags);
#else
  spin_lock_irq(&tls_lock);
#endif

  while (time_after_eq(jiffies, timer_jiffies)) {
    struct list_head *timer_list;
    struct list_head *cur;

    if (tls[0].cur_list == 0) {
      /* we just wrapped around */
      _tsKernelTimerCascade();
    }

    timer_list = tls[0].timer_list + tls[0].cur_list;
    while (1) {
      tTS_KERNEL_TIMER timer;
      tTS_KERNEL_TIMER_FUNCTION function;
      void *arg;

      cur = timer_list->next;
      if (cur == timer_list) {
        break;
      }

      timer    = list_entry(cur, tTS_KERNEL_TIMER_STRUCT, list);
      function = timer->function;
      arg      = timer->arg;

      _tsKernelTimerDetach(timer);
      timer->list.prev = NULL;
      timer->list.next = NULL;

#ifdef W2K_OS // Vipul
      spin_unlock_irqrestore(&tls_lock, flags);
      function(arg);
      spin_lock_irqsave(&tls_lock, flags);
#else
      spin_unlock_irq(&tls_lock);
      function(arg);
      spin_lock_irq(&tls_lock);
#endif
    }

    ++timer_jiffies;
    ++work;

    tls[0].cur_list = (tls[0].cur_list + 1) & TS_TIMER_LIST_MASK;
  }

#ifdef W2K_OS // Vipul
  spin_unlock_irqrestore(&tls_lock, flags);
#else
  spin_unlock_irq(&tls_lock);
#endif
  return work;
}

void tsKernelTimerAdd(
                      tTS_KERNEL_TIMER timer
                      ) {
  unsigned long flags;

  spin_lock_irqsave(&tls_lock, flags);

  if (tsKernelTimerPending(timer)) {
    goto twice;
  }
#ifndef W2K_OS
  if (!timer->function) {
    TS_REPORT_WARN(MOD_POLL,
                   "adding timer with NULL callback from [<%p>]",
                   __builtin_return_address(0));
  }
#endif
  _tsKernelTimerInsert(timer);

  spin_unlock_irqrestore(&tls_lock, flags);
  return;

 twice:
  spin_unlock_irqrestore(&tls_lock, flags);
#ifdef W2K_OS
  TS_REPORT_WARN(MOD_POLL,
                 "trying to add timer already in list");
#else
  TS_REPORT_WARN(MOD_POLL,
                 "trying to add timer already in list from [<%p>]",
                 __builtin_return_address(0));
#endif
}

void tsKernelTimerRemove(
                         tTS_KERNEL_TIMER timer
                         ) {
  unsigned long flags;
  spin_lock_irqsave(&tls_lock, flags);

  _tsKernelTimerDetach(timer);
  timer->list.prev = NULL;
  timer->list.next = NULL;

  spin_unlock_irqrestore(&tls_lock, flags);
}

void tsKernelTimerModify(
                         tTS_KERNEL_TIMER timer,
                         unsigned long new_time
                         ) {
  unsigned long flags;
  spin_lock_irqsave(&tls_lock, flags);
#ifndef W2K_OS
  if (!timer->function) {
    TS_REPORT_WARN(MOD_POLL,
                   "modifying timer with NULL callback from [<%p>]",
                   __builtin_return_address(0));
  }
#endif

  _tsKernelTimerDetach(timer);
  timer->run_time = new_time;
  _tsKernelTimerInsert(timer);

  spin_unlock_irqrestore(&tls_lock, flags);
}

void tsKernelTimerTableInit(
                            void
                            ) {
  int i, j;
  int inc = 0;
#ifdef W2K_OS // Vipul
    KeInitializeSpinLock(&tls_lock);
#endif
  timer_jiffies = jiffies;

  for (i = 0; i < TS_NUMBER_OF_TIMER_LISTS; ++i) {
    tls[i].cur_list = (timer_jiffies >> (TS_TIMER_LIST_BITS * i))
      & TS_TIMER_LIST_MASK;

    /* cur_list for i > 0 should point to the first list we want to
       cascade, which is one more than the bits in jiffies are now.
       So we increment cur_list, unless we will increment the first
       time through the timer run function. */
    if (inc) {
      tls[i].cur_list = (tls[i].cur_list + 1) & TS_TIMER_LIST_MASK;
    }

    if (tls[i].cur_list != 0) {
      inc = 1;
    }

    for (j = 0; j < TS_TIMER_LIST_SIZE; ++j) {
      INIT_LIST_HEAD(&tls[i].timer_list[j]);
    }
  }

  tsKernelPollRegister("timer",
                       _tsKernelTimerRun,
                       NULL,
                       &timer_poll);
}

void tsKernelTimerTableCleanup(
                               void
                               ) {
  if (timer_poll) {
    tsKernelPollFree(timer_poll);
  }
}
