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

  $Id: mad_proc.c,v 1.9 2004/02/25 00:35:21 roland Exp $
*/

#include "mad_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>

#include <asm/uaccess.h>

enum {
  TS_USER_MAD_SIZE = sizeof (tTS_IB_MAD_STRUCT) - sizeof (struct list_head)
};

static struct proc_dir_entry *mad_dir;

static void *_tsFilterSeqStart(
                               struct seq_file *file,
                               loff_t *pos
                               ) {
  return (void *) (uintptr_t) (*pos + 1);
}

static void *_tsFilterSeqNext(
                              struct seq_file *file,
                              void *iter_ptr,
                              loff_t *pos
                              ) {
  long iter = (long) iter_ptr;

  ++*pos;
  if (tsIbMadFilterGetByIndex(iter - 1, NULL)) {
    return NULL;
  } else {
    return (void *) (iter + 1);
  }
}

static void _tsFilterSeqStop(
                             struct seq_file *file,
                             void *iter_ptr
                             ) {
  /* nothing for now */
}

static int _tsFilterSeqShow(
                            struct seq_file *file,
                            void *iter_ptr
                            ) {
  long iter = (long) iter_ptr;
  tTS_IB_MAD_FILTER_LIST_STRUCT filter;

  if (iter == 1) {
    seq_printf(file, "%-32s|%12s|%4s|%4s|%4s|%4s|%4s|%4s|%8s|\n",
               "filter name", "device", "port", "qpn", "mcls", "meth", "aid", "dir", "matches");
    return 0;
  }

  if (tsIbMadFilterGetByIndex(iter - 2, &filter)) {
    return 0;
  }

  seq_printf(file, "%-32s|", filter.filter.name);
  if (filter.filter.mask & TS_IB_MAD_FILTER_DEVICE) {
    seq_printf(file, "%12s|", ((tTS_IB_DEVICE) filter.filter.device)->name);
  } else {
    seq_puts(file, "            |");
  }
  if (filter.filter.mask & TS_IB_MAD_FILTER_PORT) {
    seq_printf(file, "%4d|", filter.filter.port);
  } else {
    seq_puts(file, "    |");
  }
  if (filter.filter.mask & TS_IB_MAD_FILTER_QPN) {
    seq_printf(file, "%4d|", filter.filter.qpn);
  } else {
    seq_puts(file, "    |");
  }
  if (filter.filter.mask & TS_IB_MAD_FILTER_MGMT_CLASS) {
    seq_printf(file, "  %02x|", filter.filter.mgmt_class);
  } else {
    seq_puts(file, "    |");
  }
  if (filter.filter.mask & TS_IB_MAD_FILTER_R_METHOD) {
    seq_printf(file, "  %02x|", filter.filter.r_method);
  } else {
    seq_puts(file, "    |");
  }
  if (filter.filter.mask & TS_IB_MAD_FILTER_ATTRIBUTE_ID) {
    seq_printf(file, "%04x|", filter.filter.attribute_id);
  } else {
    seq_puts(file, "    |");
  }
  if (filter.filter.mask & TS_IB_MAD_FILTER_DIRECTION) {
    seq_printf(file, "%4s|",
               filter.filter.direction == TS_IB_MAD_DIRECTION_IN ? "in" : "out");
  } else {
    seq_puts(file, "    |");
  }
  seq_printf(file, "%8d|\n", filter.matches);

  return 0;
}

static struct seq_operations filter_seq_ops = {
  .start = _tsFilterSeqStart,
  .next  = _tsFilterSeqNext,
  .stop  = _tsFilterSeqStop,
  .show  = _tsFilterSeqShow
};

static int _tsIbFilterOpen(
                           struct inode *inode,
                           struct file *file
                           ) {
  return seq_open(file, &filter_seq_ops);
}

static int _tsIbFilterRelease(
                              struct inode *inode,
                              struct file *file
                              ) {
  return seq_release(inode, file);
}

static struct file_operations filter_ops = {
  .owner   = THIS_MODULE,
  .open    = _tsIbFilterOpen,
  .read    = seq_read,
  .llseek  = seq_lseek,
  .release = _tsIbFilterRelease
};

static int _tsIbProcessOpen(
                           struct inode *inode,
                           struct file *file
                           ) {
  struct proc_dir_entry *pde = PDE(inode);
  file->private_data = pde->data;

  return 0;
}

static ssize_t _tsIbProcessWrite(
                                 struct file *file,
                                 const char *buf,
                                 size_t count,
                                 loff_t *ppos
                                 ) {
  tTS_IB_MAD mad_in;
  tTS_IB_MAD mad_out;
  tTS_IB_DEVICE device = file->private_data;
  int i;
  int ret;
  tTS_IB_MAD_RESULT mad_result;

  if (count != TS_USER_MAD_SIZE) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "write of size %d not equal to mad size %d",
                   count, TS_USER_MAD_SIZE);
    return -EINVAL;
  }

  mad_in = kmalloc(sizeof *mad_in, GFP_KERNEL);
  if (!mad_in) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Failed to allocate MAD buffer");
    return -ENOMEM;
  }

  mad_out = kmalloc(sizeof *mad_out, GFP_KERNEL);
  if (!mad_out) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Failed to allocate MAD buffer");
    kfree(mad_in);
    return -ENOMEM;
  }

  if (copy_from_user(mad_in, buf, TS_USER_MAD_SIZE)) {
    ret = -EFAULT;
    goto out;
  }

  mad_in->device          = device;
  mad_in->completion_func = NULL;

  TS_TRACE(MOD_KERNEL_IB, T_TERSE, TRACE_KERNEL_IB_GEN,
           "Processing MAD on device %s:",
           device->name);

  for (i = 0; i < 256; ++i) {
    if (!(i % 16)) {
      printk(KERN_ERR "   ");
    }
    printk(" %02x", ((uint8_t *) mad_in)[i]);
    if (!((i + 1) % 16)) {
      printk("\n");
    } else {
      if (!((i + 1) % 4)) {
        printk(" ");
      }
    }
  }

  mad_result = device->mad_process(device, 1, mad_in, mad_out);
  if (!(mad_result & TS_IB_MAD_RESULT_SUCCESS)) {
    TS_TRACE(MOD_KERNEL_IB, T_TERSE, TRACE_KERNEL_IB_GEN,
             "mad_process returned %x",
             mad_result);
  } else if (mad_result & TS_IB_MAD_RESULT_REPLY) {
    TS_TRACE(MOD_KERNEL_IB, T_TERSE, TRACE_KERNEL_IB_GEN,
             "mad_process returned response:");

    for (i = 0; i < 256; ++i) {
      if (!(i % 16)) {
        printk(KERN_ERR "   ");
      }
      printk(" %02x", ((uint8_t *) mad_out)[i]);
      if (!((i + 1) % 16)) {
        printk("\n");
      } else {
        if (!((i + 1) % 4)) {
          printk(" ");
        }
      }
    }
  }

  ret = TS_USER_MAD_SIZE;

 out:
  kfree(mad_in);
  kfree(mad_out);
  return ret;
}

static struct file_operations mad_process_ops = {
  .owner   = THIS_MODULE,
  .open    = _tsIbProcessOpen,
  .write   = _tsIbProcessWrite,
};

int tsIbMadProcSetup(
                     void
                     ) {
  int i = 0;
  tTS_IB_DEVICE device;
  struct proc_dir_entry *entry;

  mad_dir = proc_mkdir("mad", tsKernelProcDirGet());
  if (!mad_dir) {
    return -ENOMEM;
  }

  entry = create_proc_entry("filter", S_IRUGO, mad_dir);
  if (!entry) {
    remove_proc_entry("mad", tsKernelProcDirGet());
    return -ENOMEM;
  }
  entry->proc_fops = &filter_ops;

  while ((device = tsIbDeviceGetByIndex(i)) != TS_IB_HANDLE_INVALID) {
    if (!device->mad_process) {
      continue;
    }

    entry = create_proc_entry(device->name, S_IWUSR, mad_dir);
    if (!entry) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Couldn't create /proc/mad entry for %s",
                     device->name);
      continue;
    }
    entry->proc_fops = &mad_process_ops;
    entry->data      = device;
    ++i;
  }

  return 0;
}

void tsIbMadProcCleanup(
                        void
                        ) {
  int i = 0;
  tTS_IB_DEVICE device;

  while ((device = tsIbDeviceGetByIndex(i)) != TS_IB_HANDLE_INVALID) {
    remove_proc_entry(device->name, mad_dir);
    ++i;
  }

  remove_proc_entry("filter", mad_dir);
  remove_proc_entry("mad", tsKernelProcDirGet());
}
