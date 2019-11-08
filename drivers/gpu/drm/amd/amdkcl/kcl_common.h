/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_COMMON_H
#define AMDKCL_COMMON_H

#include <linux/kernel.h>
#include <linux/version.h>

void *amdkcl_fp_setup(const char *symbol, void *dummy);

#endif
