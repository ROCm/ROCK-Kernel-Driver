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

  $Id: ts_kernel_timer.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_TIMER_H
#define _TS_KERNEL_TIMER_H

#ifdef W2K_OS // Vipul
#include "all/common/include/w2k.h"
#include "all/common/include/os_dep/win/linux/list.h"
#else

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,poll_export.ver)
#endif

#include <linux/types.h>
#include <linux/list.h>
#endif

typedef struct tTS_KERNEL_TIMER_STRUCT tTS_KERNEL_TIMER_STRUCT,
  *tTS_KERNEL_TIMER;

typedef void (*tTS_KERNEL_TIMER_FUNCTION)(void *arg);

/*
  We declare tTS_KERNEL_TIMER_STRUCT here so that users of this
  facility can allocate timers without having to go through a function
  to dynamically allocate memory.  For example, this means a user of
  the facility could declare a struct that contains a timer.
*/

struct tTS_KERNEL_TIMER_STRUCT {
  unsigned long             run_time;    /* time in jiffies */
  tTS_KERNEL_TIMER_FUNCTION function;
  void *                    arg;
  struct list_head          list;
};

void tsKernelTimerAdd(
                      tTS_KERNEL_TIMER timer
                      );

void tsKernelTimerRemove(
                         tTS_KERNEL_TIMER timer
                         );

void tsKernelTimerModify(
                         tTS_KERNEL_TIMER timer,
                         unsigned long new_time
                         );

static inline void tsKernelTimerInit(
                                     tTS_KERNEL_TIMER timer
                                     ) {
  timer->list.prev = NULL;
  timer->list.next = NULL;
}

static inline int tsKernelTimerPending(
                                       tTS_KERNEL_TIMER timer
                                       ) {
  return timer->list.next != NULL;
}

#endif /* _TS_KERNEL_TIMER_H */
