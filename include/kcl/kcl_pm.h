/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_PM_H
#define KCL_KCL_PM_H

#include <linux/pm.h>

/*
 * v5.7-rc2-7-ge07515563d01
 * PM: sleep: core: Rename DPM_FLAG_NEVER_SKIP
 */
#ifndef DPM_FLAG_NO_DIRECT_COMPLETE
#define DPM_FLAG_NO_DIRECT_COMPLETE DPM_FLAG_NEVER_SKIP
#endif

#endif
