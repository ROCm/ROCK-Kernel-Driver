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

  $Id: poll_main.c 32 2004-04-09 03:57:42Z roland $
*/

#ifdef W2K_OS // Vipul
#include <ntddk.h>
#include "all/common/include/os_dep/win/linux/list.h"
#endif
#include "poll.h"

#include "ts_kernel_timer.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"
#include "ts_kernel_thread.h"

#ifdef W2K_OS // Vipul
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING UniRegistryPath);
VOID DriverUnload(IN PDRIVER_OBJECT DriverObject);
#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif
#else // W2K_OS
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <linux/list.h>
#include <linux/proc_fs.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("kernel poll loop");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_PARM(sleep, "i");
MODULE_PARM_DESC(sleep, "If non-zero, sleep one jiffy after each iteration");

/* sleep == 0 isn't very nice on a host system :) */
#ifdef TS_HOST_DRIVER
static int sleep = 1;
#else
static int sleep = 0;
#endif
#endif // W2K_OS

struct tTS_KERNEL_POLL_HANDLE_STRUCT {
  tTS_KERNEL_POLL_FUNCTION function;
  void *                   arg;
  char                     name[TS_KERNEL_POLL_NAME_LENGTH];
  int                      total;
  struct list_head         list;
};

static tTS_KERNEL_THREAD poll_thread;
static LIST_HEAD(poll_list);

#ifdef W2K_OS // Vipul
#define NT_DEV_NAME        L"\\Device\\ts_poll_counts"
#define DOS_DEV_NAME       L"\\DosDevices\\ts_poll_counts"
#else
static const char proc_entry_name[] = "poll_counts";
static struct proc_dir_entry *proc_entry;
#endif

int tsKernelPollRegister(
                         const char *name,
                         tTS_KERNEL_POLL_FUNCTION function,
                         void *arg,
                         tTS_KERNEL_POLL_HANDLE *handle
                         ) {
  *handle = kmalloc(sizeof **handle, GFP_KERNEL);
  if (!*handle) {
    return -ENOMEM;
  }

  strncpy((*handle)->name, name, TS_KERNEL_POLL_NAME_LENGTH);
  (*handle)->name[TS_KERNEL_POLL_NAME_LENGTH - 1] = '\0';

  (*handle)->function = function;
  (*handle)->arg      = arg;
  (*handle)->total    = 0;

  list_add_tail(&(*handle)->list, &poll_list);

  return 0;
}

void tsKernelPollFree(
                      tTS_KERNEL_POLL_HANDLE handle
                      ) {
  list_del(&handle->list);
  kfree(handle);
}

static void _tsKernelPollIteration(
                                   void
                                   ) {
  struct list_head *cur_ptr;

  list_for_each(cur_ptr, &poll_list) {
    tTS_KERNEL_POLL_HANDLE cur;
    tTS_KERNEL_POLL_FUNCTION function;
    void *arg;

    cur      = list_entry(cur_ptr, tTS_KERNEL_POLL_HANDLE_STRUCT, list);
    function = cur->function;
    arg      = cur->arg;

    cur->total += function(arg);
  }
}
#ifndef W2K_OS // Vipul
static void _tsKernelPollThread(
                                void *arg
                                ) {
  TS_REPORT_STAGE(MOD_POLL, "Kernel poll thread started");

  while (!signal_pending(current)) {
    _tsKernelPollIteration();

#if defined(TS_KERNEL_2_6)
    cond_resched();
#else
    if (current->need_resched) {
      schedule();
    }
#endif
  }

  TS_REPORT_STAGE(MOD_POLL, "Kernel poll thread exiting");
}
#endif

static void _tsKernelPollSleepThread(
                                void *arg
                                ) {
#ifdef W2K_OS // Vipul
  LARGE_INTEGER   sleep_time;
  NTSTATUS        status = STATUS_SUCCESS;

  TS_REPORT_STAGE(MOD_POLL, "Kernel poll thread started (using sleep)");

  while (status == STATUS_SUCCESS) {
    _tsKernelPollIteration();

    /* Sleep the equivalent of 1 jiffy (1/100 of a second). */
    sleep_time.QuadPart = -100000;
    status = KeDelayExecutionThread(KernelMode, FALSE, &sleep_time);
  }

#else
  TS_REPORT_STAGE(MOD_POLL, "Kernel poll thread started (using sleep)");


  while (!signal_pending(current)) {
    _tsKernelPollIteration();

    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(sleep);
  }
#endif

  TS_REPORT_STAGE(MOD_POLL, "Kernel poll thread exiting");
}

#ifdef W2K_OS // Vipul
static void *_tsKernelPollSeqStart(
                                    struct seq_file *file,
                                    loff_t *pos,
				                    void *usr_arg)
#else
static void *_tsKernelPollSeqStart(
                                    struct seq_file *file,
                                    loff_t *pos
                                    )
#endif
{
  struct list_head *p = &poll_list;
  loff_t n = *pos;

  while (n--) {
    p = p->next;
    if (p == &poll_list) {
      return NULL;
    }
  }

  return p;
}

#ifdef W2K_OS // Vipul
static void *_tsKernelPollSeqNext(
                                   struct seq_file *file,
                                   void *arg,
                                   loff_t *pos,
				                   void *usr_arg)
#else
static void *_tsKernelPollSeqNext(
                                   struct seq_file *file,
                                   void *arg,
                                   loff_t *pos
                                   )
#endif
{
  struct list_head *p = arg;

  (*pos)++;
  return (p->next != &poll_list) ? p->next : NULL;
}

#ifdef W2K_OS // Vipul
static void _tsKernelPollSeqStop(
                                 struct seq_file *file,
                                 void *arg,
				                 void *usr_arg
								 )
#else
static void _tsKernelPollSeqStop(
                                 struct seq_file *file,
                                 void *arg
                                 )
#endif
{
  /* nothing for now */
}

#ifdef W2K_OS // Vipul
static int _tsKernelPollSeqShow(
                                 struct seq_file *file,
                                 void *arg,
				                 void *usr_arg
								 )
#else
static int _tsKernelPollSeqShow(
                                 struct seq_file *file,
                                 void *arg
                                 )
#endif
{
  struct list_head *p = arg;

  if (p == &poll_list) {
    seq_printf(file,
               "function              count\n");
  } else {
    tTS_KERNEL_POLL_HANDLE h = list_entry(p, tTS_KERNEL_POLL_HANDLE_STRUCT, list);

    seq_printf(file,
               "%-16s %10d\n",
               h->name, h->total);
  }

  return 0;
}

#ifdef W2K_OS
static struct seq_operations poll_seq_operations = {
  _tsKernelPollSeqStart,
  _tsKernelPollSeqStop,
  _tsKernelPollSeqNext,
  _tsKernelPollSeqShow
};
#else
static struct seq_operations poll_seq_operations = {
  .start = _tsKernelPollSeqStart,
  .next  = _tsKernelPollSeqNext,
  .stop  = _tsKernelPollSeqStop,
  .show  = _tsKernelPollSeqShow
};

static int _tsKernelPollProcOpen(
                                  struct inode *inode,
                                  struct file *file
                                  ) {
  return seq_open(file, &poll_seq_operations);
}

static struct file_operations poll_proc_operations = {
  .open    = _tsKernelPollProcOpen,
  .read    = seq_read,
  .llseek  = seq_lseek,
  .release = seq_release
};
#endif

#ifdef W2K_OS // Vipul - NT specific Entry routines
NTSTATUS
DriverCreate(IN PDEVICE_OBJECT pdo, IN PIRP pIrp)
{
  PIO_STACK_LOCATION     irpStack;
  PFILE_OBJECT           pFileObject;

  irpStack = IoGetCurrentIrpStackLocation(pIrp);
  pFileObject = irpStack->FileObject;

  seq_open(&pFileObject->FsContext, &poll_seq_operations, 0);

  pIrp->IoStatus.Status = STATUS_SUCCESS;
  pIrp->IoStatus.Information = 0;
  IoCompleteRequest(pIrp, IO_NO_INCREMENT);

  return STATUS_SUCCESS;
}

NTSTATUS
DriverClose(IN PDEVICE_OBJECT pdo, IN PIRP pIrp)
{
  PIO_STACK_LOCATION     irpStack;
  PFILE_OBJECT           pFileObject;

  irpStack = IoGetCurrentIrpStackLocation(pIrp);
  pFileObject = irpStack->FileObject;

  seq_release(pFileObject->FsContext);

  pIrp->IoStatus.Status = STATUS_SUCCESS;
  pIrp->IoStatus.Information = 0;
  IoCompleteRequest(pIrp, IO_NO_INCREMENT);

  return STATUS_SUCCESS;
}

NTSTATUS
DriverRead(IN PDEVICE_OBJECT pdo, IN PIRP pIrp)
{
  PIO_STACK_LOCATION     irpStack;
  PFILE_OBJECT           pFileObject;
  PVOID                  userBuf;
  NTSTATUS               status;
  ssize_t                count;

  irpStack = IoGetCurrentIrpStackLocation(pIrp);
  pFileObject = irpStack->FileObject;
  userBuf = pIrp->AssociatedIrp.SystemBuffer;
  count = irpStack->Parameters.Read.Length;

  count = seq_read(pFileObject->FsContext, userBuf, count);
  status = count>=0? STATUS_SUCCESS: STATUS_INSUFFICIENT_RESOURCES;

  pIrp->IoStatus.Status = status;
  pIrp->IoStatus.Information = count;
  IoCompleteRequest(pIrp, IO_NO_INCREMENT);

  return status;
}

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING UniRegistryPath)
{
  NTSTATUS              status;
  PDEVICE_OBJECT        pDevObj;
  UNICODE_STRING        NtName;
  UNICODE_STRING        Win32Name;
  int                   ret;

  RtlInitUnicodeString(&NtName, NT_DEV_NAME);
  status = IoCreateDevice(DriverObject,
                          0,
                          &NtName,
                          FILE_DEVICE_UNKNOWN,
                          0,
                          FALSE,
                          &pDevObj);
  if (!NT_SUCCESS(status)) {
    return status;
  }
  pDevObj->Flags |= DO_BUFFERED_IO;

  RtlInitUnicodeString(&Win32Name, DOS_DEV_NAME);
  status = IoCreateSymbolicLink(&Win32Name, &NtName);
  if (!NT_SUCCESS(status)) {
      IoDeleteDevice(pDevObj);
      return status;
  }

  DriverObject->DriverUnload = DriverUnload;
  DriverObject->MajorFunction [IRP_MJ_CREATE] = DriverCreate;
  DriverObject->MajorFunction [IRP_MJ_CLOSE] = DriverClose;
  DriverObject->MajorFunction [IRP_MJ_READ] = DriverRead;

  TS_REPORT_INIT(MOD_KERNEL_IB,
                 "Initializing IB kernel poll layer");

  tsKernelTimerTableInit();

  ret = tsKernelThreadStart("ts_poll",
          _tsKernelPollSleepThread,
          NULL,
          &poll_thread);

  if (ret) {
      tsKernelTimerTableCleanup();
  }

  TS_REPORT_INIT(MOD_KERNEL_IB,
        "IB kernel poll layer initialized");
  return STATUS_SUCCESS;
}

VOID
DriverUnload(IN PDRIVER_OBJECT DriverObject)
{
  UNICODE_STRING      Win32Name;
  struct list_head    *cur;
  struct list_head    *next;

  RtlInitUnicodeString(&Win32Name, DOS_DEV_NAME);
  IoDeleteSymbolicLink(&Win32Name);
  IoDeleteDevice(DriverObject->DeviceObject);

  tsKernelThreadStop(poll_thread);
  tsKernelTimerTableCleanup();

  for (cur = poll_list.next; cur != &poll_list; cur = next) {
    tTS_KERNEL_POLL_HANDLE handle;

    handle = list_entry(cur, tTS_KERNEL_POLL_HANDLE_STRUCT, list);
    next   = cur->next;

    kfree(handle);
  }
}

#else // Vipul - Linux specific stuff
static int __init _tsKernelPollInitModule(
                                          void
                                          ) {
  int ret;

  proc_entry = create_proc_entry(proc_entry_name,
                                 S_IRUGO,
                                 tsKernelProcDirGet());
  if (!proc_entry) {
    return -ENOMEM;
  }

  proc_entry->proc_fops = &poll_proc_operations;

  tsKernelTimerTableInit();

  ret = tsKernelThreadStart("ts_poll",
                            !sleep ? _tsKernelPollThread : _tsKernelPollSleepThread,
                            NULL,
                            &poll_thread);

  if (ret) {
    tsKernelTimerTableCleanup();
    remove_proc_entry(proc_entry_name, tsKernelProcDirGet());
  }

  return ret;
}

static void __exit _tsKernelPollCleanupModule(
                                              void
                                              ) {
  tsKernelThreadStop(poll_thread);
  tsKernelTimerTableCleanup();
  remove_proc_entry(proc_entry_name, tsKernelProcDirGet());

  {
    struct list_head *cur;
    struct list_head *next;

    for (cur = poll_list.next; cur != &poll_list; cur = next) {
      tTS_KERNEL_POLL_HANDLE handle;

      handle = list_entry(cur, tTS_KERNEL_POLL_HANDLE_STRUCT, list);
      next   = cur->next;

      kfree(handle);
    }
  }
}

module_init(_tsKernelPollInitModule);
module_exit(_tsKernelPollCleanupModule);

#endif
