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

  $Id: services_main.c,v 1.9 2004/02/25 01:35:35 roland Exp $
*/

#ifdef W2K_OS
#include <ntddk.h>
#include "trace.h"

#define NT_DEV_NAME        L"\\Device\\ts_trace"
#define DOS_DEV_NAME       L"\\DosDevices\\ts_trace"

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING UniRegistryPath);
VOID DriverUnload(IN PDRIVER_OBJECT DriverObject);
extern NTSTATUS DriverCreate(IN PDEVICE_OBJECT pdo, IN PIRP pIrp);
extern NTSTATUS DriverClose(IN PDEVICE_OBJECT pdo, IN PIRP pIrp);
extern NTSTATUS DriverRead(IN PDEVICE_OBJECT pdo, IN PIRP pIrp);
extern NTSTATUS DriverWrite(IN PDEVICE_OBJECT pdo, IN PIRP pIrp);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING UniRegistryPath)
{
    NTSTATUS           status;
    PDEVICE_OBJECT     pDevObj;
    UNICODE_STRING     NtName;
    UNICODE_STRING     Win32Name;

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
    DriverObject->MajorFunction [IRP_MJ_WRITE] = DriverWrite;

    return tsKernelTraceInit();
}

VOID
DriverUnload(IN PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING Win32Name;

    RtlInitUnicodeString(&Win32Name, DOS_DEV_NAME);
    IoDeleteSymbolicLink(&Win32Name);
    IoDeleteDevice(DriverObject->DeviceObject);

    tsKernelTraceCleanup();
}

#else //W2K_OS



#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include "ts_kernel_services.h"
#include "trace.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("basic kernel services");
MODULE_LICENSE("Dual BSD/GPL");

static const char ib_proc_dir_name[] = "infiniband";
static struct proc_dir_entry *ib_proc_dir;

/*
  Provide an mcount() function.  The "-pg" option to gcc causes it to
  insert profiling code, which amounts to a call to mcount() at every
  function entry.  We hijack this to check the kernel stack and print
  a warning if it might be overflowing.
*/

void mcount(void) {
#if defined(i386)
  static int overflowed;

  unsigned long sp;
  unsigned long left;

  asm("movl %%esp, %0;" : "=r"(sp) : );

  left = sp - (unsigned long) current - sizeof (struct task_struct);
  if (left < 2048) {
    if (++overflowed < 5) {
      printk(KERN_ERR "STACK WARNING (%1d): 0x%08lx left in [<%p>]\n",
             overflowed, left, __builtin_return_address(0));
    }
  } else {
    overflowed = 0;
  }
#endif /* i386 */
}


struct proc_dir_entry *tsKernelProcDirGet(
                                          void
                                          ) {
  return ib_proc_dir;
}

static int __init tsKernelServicesInitModule(
                                             void
                                             ) {
  int ret;

  ib_proc_dir = proc_mkdir(ib_proc_dir_name, NULL);
  if (!ib_proc_dir) {
    printk(KERN_ERR "Can't create /proc/%s\n",
           ib_proc_dir_name);
    return -ENOMEM;
  }

  ib_proc_dir->owner = THIS_MODULE;

#if !defined(NO_TOPSPIN_LEGACY_PROC)
  proc_symlink("topspin", NULL, ib_proc_dir_name);
#endif

  ret = tsKernelTraceInit();
  if (ret) {
    remove_proc_entry(ib_proc_dir_name, NULL);
    return ret;
  }

  return 0;
}


static void __exit tsKernelServicesCleanupModule(
                                                 void
                                                 ) {
  tsKernelTraceCleanup();

  if (ib_proc_dir) {
    remove_proc_entry(ib_proc_dir_name, NULL);
  }
}

module_init(tsKernelServicesInitModule);
module_exit(tsKernelServicesCleanupModule);

#endif //W2K_OS
