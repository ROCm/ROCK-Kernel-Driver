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

  $Id: ts_kernel_thread.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_THREAD_H
#define _TS_KERNEL_THREAD_H

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,services_export.ver)
#endif
#endif

#include <linux/types.h>
#include <linux/list.h>

typedef struct tTS_KERNEL_THREAD_STRUCT tTS_KERNEL_THREAD_STRUCT,
  *tTS_KERNEL_THREAD;
typedef struct tTS_KERNEL_QUEUE_THREAD_STRUCT tTS_KERNEL_QUEUE_THREAD_STRUCT,
  *tTS_KERNEL_QUEUE_THREAD;

typedef void (*tTS_KERNEL_THREAD_FUNCTION)(void *arg);
typedef void (*tTS_KERNEL_QUEUE_THREAD_FUNCTION)(struct list_head *entry,
                                                 void *arg);

int tsKernelThreadStart(
                        const char *name,
                        tTS_KERNEL_THREAD_FUNCTION function,
                        void *arg,
                        tTS_KERNEL_THREAD *thread
                        );

int tsKernelThreadStop(
                       tTS_KERNEL_THREAD thread
                       );

int tsKernelQueueThreadStart(
                             const char *name,
                             tTS_KERNEL_QUEUE_THREAD_FUNCTION function,
                             void *arg,
                             tTS_KERNEL_QUEUE_THREAD *thread
                             );

int tsKernelQueueThreadStop(
                            tTS_KERNEL_QUEUE_THREAD thread
                            );

void tsKernelQueueThreadAdd(
                            tTS_KERNEL_QUEUE_THREAD thread,
                            struct list_head       *entry
                            );

#endif /* _TS_KERNEL_THREAD_H */
