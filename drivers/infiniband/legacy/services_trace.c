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

  $Id: services_trace.c 32 2004-04-09 03:57:42Z roland $
*/

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"

#include <linux/module.h>

#include <linux/spinlock.h>
#include <linux/proc_fs.h>

#include <asm/uaccess.h>

#include "module_names.h"

static const char proc_entry_name[] = "tracelevel";
static struct proc_dir_entry *proc_entry;


typedef struct tTRACE_INFO_STRUCT {
  tTS_TRACE_LEVEL tracelevel;
  uint32_t flow_mask;
} tTRACE_INFO_STRUCT;

static tTRACE_INFO_STRUCT trace_table[MOD_MAX + 1] = {
  [0 ... MOD_MAX] = {
    .tracelevel = TS_TRACE_LEVEL_KERNEL_DEFVAL,
    .flow_mask  = TS_TRACE_MASK_KERNEL_DEFVAL
  }
};

static char big_trace_buf[4096];
static spinlock_t big_trace_lock = SPIN_LOCK_UNLOCKED;

static void *_tsKernelTraceSeqStart(
                                    struct seq_file *file,
                                    loff_t *pos
                                    ) {
  return (void *) (long) (*pos + 1);
}

static void *_tsKernelTraceSeqNext(
                                   struct seq_file *file,
                                   void *arg,
                                   loff_t *pos
                                   ) {
  int num = (long) arg;

  (*pos)++;
  return (num <= MOD_MAX) ? (void *) (long) (num + 1) : NULL;
}

static void _tsKernelTraceSeqStop(
                                  struct seq_file *file,
                                  void *arg
                                  ) {
  /* nothing for now */
}

static int _tsKernelTraceSeqShow(
                                 struct seq_file *file,
                                 void *arg
                                 ) {
  int num = (long) arg;

  if (num == 1) {
    seq_printf(file,
               "module      id  tracelevel   flowmask\n");
  } else {
    num -= 2;

    if (num < MOD_MAX) {
      seq_printf(file,
                 "%-12s%2d  %6d      0x%08x\n",
                 ModuleName[num],
                 (int) num,
                 trace_table[num].tracelevel,
                 trace_table[num].flow_mask);
    }
  }

  return 0;
}

static ssize_t _tsKernelTraceProcWrite(
                                       struct file *file,
                                       const char *buffer,
                                       size_t count,
                                       loff_t *pos
                                       ) {
  char kernel_buf[256];
  int ret;
  int mod;
  int level;
  uint32_t flow_mask;

  if (count > sizeof kernel_buf) {
    count = sizeof kernel_buf;
  }

  if (copy_from_user(kernel_buf, buffer, count)) {
    return -EFAULT;
  }

  ret = count;

  kernel_buf[count - 1] = '\0';

  if (sscanf(kernel_buf, "%d %d %i",
             &mod, &level, &flow_mask)
      != 3) {
    return ret;
  }

  if (mod >= 0 && mod < MOD_MAX) {
    level = min(level, T_MAX - 1);
    level = max(level, 0);
    trace_table[mod].tracelevel = level;
    trace_table[mod].flow_mask = flow_mask;
  }

  return ret;
}

static struct seq_operations trace_seq_operations = {
  .start = _tsKernelTraceSeqStart,
  .next  = _tsKernelTraceSeqNext,
  .stop  = _tsKernelTraceSeqStop,
  .show  = _tsKernelTraceSeqShow
};

static int _tsKernelTraceProcOpen(
                                  struct inode *inode,
                                  struct file *file
                                  ) {
  return seq_open(file, &trace_seq_operations);
}

void tsKernelTrace(
                   const char *file,
                   int line,
                   const char *function,
                   tMODULE_ID mod,
                   tTS_TRACE_LEVEL level,
                   uint32_t flow_mask,
                   const char *format,
                   ...
                   ) {
  unsigned long flags;

  if (mod < 0 || mod >= MOD_MAX) {
    return;
  }

  if (trace_table[mod].tracelevel >= level
      && trace_table[mod].flow_mask & flow_mask) {

    va_list args;
    va_start(args, format);

    spin_lock_irqsave(&big_trace_lock, flags);

    vsnprintf(big_trace_buf, sizeof big_trace_buf, format, args);
    /* XXX add time printout? */
    printk("%s[%s][%s][%s:%d]%s\n",
           KERN_CRIT,
           ModuleName[mod],
           function,
           file,
           line,
           big_trace_buf);

    spin_unlock_irqrestore(&big_trace_lock, flags);

    va_end(args);
  }
}

void tsKernelTraceLevelSet(
                           tMODULE_ID mod,
                           tTS_TRACE_LEVEL level
                           ) {
  if (mod >= 0 && mod < MOD_MAX
      && level >= T_NO_DISPLAY && level < T_MAX) {
    trace_table[mod].tracelevel = level;
  }
}

void tsKernelTraceFlowMaskSet(
                              tMODULE_ID mod,
                              uint32_t flow_mask
                              ) {
  if (mod >= 0 && mod < MOD_MAX) {
    trace_table[mod].flow_mask = flow_mask;
  }
}

static struct file_operations trace_proc_operations = {
  .owner   = THIS_MODULE,
  .open    = _tsKernelTraceProcOpen,
  .read    = seq_read,
  .write   = _tsKernelTraceProcWrite,
  .llseek  = seq_lseek,
  .release = seq_release
};

int tsKernelTraceInit(
                      void
                      ) {
  proc_entry = create_proc_entry(proc_entry_name,
                                 S_IRUGO | S_IWUGO,
                                 tsKernelProcDirGet());
  if (!proc_entry) {
    printk(KERN_ERR "Can't create %s in proc directory\n",
           proc_entry_name);
    return -ENOMEM;
  }

  proc_entry->proc_fops = &trace_proc_operations;

  return 0;
}


void tsKernelTraceCleanup(
                          void
                          ) {
  if (proc_entry) {
    remove_proc_entry(proc_entry_name, tsKernelProcDirGet());
  }
}
