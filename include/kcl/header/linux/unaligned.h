/* SPDX-License-Identifier: MIT */
#ifndef _KCL_HEADER_LINUX_UNALIGNED_H
#define _KCL_HEADER_LINUX_UNALIGNED_H

#ifdef HAVE_LINUX_UNALIGNED_H
#include_next <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

#endif

