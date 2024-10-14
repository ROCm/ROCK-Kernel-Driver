/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  pm.h - Power management interface
 *
 *  Copyright (C) 2000 Andrew Henroid
 */
#ifndef KCL_KCL_PM_RUNTIME_H
#define KCL_KCL_PM_RUNTIME_H

#include <linux/pm_runtime.h>

#ifndef HAVE_PM_RUNTIME_RESUME_AND_GET
static inline int pm_runtime_resume_and_get(struct device *dev)
{
	int ret;

	ret = __pm_runtime_resume(dev, RPM_GET_PUT);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	return 0;
}
#endif

#endif
