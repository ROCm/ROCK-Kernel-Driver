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

  $Id: ts_kernel_services.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_SERVICES_H
#define _TS_KERNEL_SERVICES_H

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,services_export.ver)
#endif

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include "ts_kcompat.h"
#else
#include <linux/seq_file.h>
#endif

/**
   Return a handle to the "/proc/infiniband/" directory.
*/
struct proc_dir_entry *tsKernelProcDirGet(
                                          void
                                          );

void mcount(void);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
typedef struct rb_root rb_root_t;
typedef struct rb_node rb_node_t;
#endif

#endif /* _TS_KERNEL_SERVICES_H */
