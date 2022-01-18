/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_FDTABLE_H
#define _KCL_FDTABLE_H

#ifndef HAVE_KERNEL_CLOSE_FD
#include <linux/syscalls.h>
#define close_fd ksys_close
#endif

#endif
