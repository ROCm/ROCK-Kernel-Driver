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

  $Id: services_main.c 32 2004-04-09 03:57:42Z roland $
*/

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
