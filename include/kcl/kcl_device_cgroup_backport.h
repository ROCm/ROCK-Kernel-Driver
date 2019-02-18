#ifndef AMDKCL_DEVICE_CGROUP_BACKPORT_H
#define AMDKCL_DEVICE_CGROUP_BACKPORT_H

#include <kcl/kcl_device_cgroup.h>

#if defined(CONFIG_CGROUP_DEVICE)
#ifndef HAVE_DEVCGROUP_CHECK_PERMISSION
#define devcgroup_check_permission _kcl_devcgroup_check_permission
#endif
#endif
#endif
