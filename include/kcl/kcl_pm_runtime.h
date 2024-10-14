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

#ifdef CONFIG_PM
#if defined(HAVE_PM_RUNTIME_GET_IF_ACTIVE_2ARGS)
static inline int _kcl_pm_runtime_get_if_active(struct device *dev)
{
	return pm_runtime_get_if_active(dev, true);
}
#define pm_runtime_get_if_active _kcl_pm_runtime_get_if_active
#elif !defined(HAVE_PM_RUNTIME_GET_IF_ACTIVE_1ARGS)
static inline int _kcl_pm_runtime_get_if_active(struct device *dev)
{
	return pm_runtime_get_if_in_use(dev);
}
#define pm_runtime_get_if_active _kcl_pm_runtime_get_if_active
#endif /* HAVE_PM_RUNTIME_GET_IF_ACTIVE_2ARGS */
#endif /* CONFIG_PM */

#endif /* KCL_KCL_PM_RUNTIME_H */