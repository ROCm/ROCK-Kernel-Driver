#ifndef AMDKCL_DEVICE_CGROUP_H
#define AMDKCL_DEVICE_CGROUP_H

#include <linux/device_cgroup.h>

#ifndef DEVCG_DEV_CHAR
#define DEVCG_DEV_CHAR  2
#endif
#ifndef DEVCG_ACC_READ
#define DEVCG_ACC_READ  2
#endif
#ifndef DEVCG_ACC_WRITE
#define DEVCG_ACC_WRITE 4
#endif

#if defined(CONFIG_CGROUP_DEVICE)
#ifndef HAVE_DEVCGROUP_CHECK_PERMISSION
extern int (*__kcl_devcgroup_check_permission)(short type, u32 major, u32 minor,
				short access);
#endif

static inline int kcl_devcgroup_check_permission(short type, u32 major, u32 minor,
					short access)
{
#ifndef HAVE_DEVCGROUP_CHECK_PERMISSION
#ifdef BPF_CGROUP_RUN_PROG_DEVICE_CGROUP
	int rc = BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(type, major, minor, access);

	if (rc)
		return -EPERM;
#endif

	return __kcl_devcgroup_check_permission(type, major, minor, access);
#else
	return devcgroup_check_permission(type, major, minor, access);
#endif
}
#endif
#endif /*AMDKCL_DEVICE_CGROUP_H*/
