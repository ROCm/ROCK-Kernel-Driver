/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_COMMON_H
#define AMDKCL_COMMON_H

#include <linux/kernel.h>
#include <linux/version.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "amdkcl: " fmt

void *amdkcl_fp_setup(const char *symbol, void *dummy);

#endif
