/* SPDX-License-Identifier: MIT */
#ifndef _KCL_HEADER_LINUX_PGTABLE_H_H_
#define _KCL_HEADER_LINUX_PGTABLE_H_H_

#ifdef HAVE_LINUX_PGTABLE_H
#include_next <linux/pgtable.h>
#else
#include <asm-generic/pgtable.h>
#endif

#endif
