/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dynamic loading of modules into the kernel.
 *
 * Rewritten by Richard Henderson <rth@tamu.edu> Dec 1996
 * Rewritten again by Rusty Russell, 2002
 */
#ifndef _KCL_KCL_LINUX_MODULE_H_H
#define _KCL_KCL_LINUX_MODULE_H_H

#include <linux/module.h>

/* Copied from v5.3-11739-g3e4d890a26d5 include/linux/module.h */
#ifndef MODULE_IMPORT_NS
#define MODULE_IMPORT_NS(ns) MODULE_INFO(import_ns, #ns)
#endif

#endif
