#ifndef AMDKCL_DEVICE_CGROUP_H
#define AMDKCL_DEVICE_CGROUP_H

#include <linux/version.h>
#include <linux/types.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
#define DEVCG_DEV_CHAR  2
#define DEVCG_ACC_READ  2
#define DEVCG_ACC_WRITE 4
#endif

extern int (*__kcl_devcgroup_check_permission)(short type, u32 major, u32 minor,
				short access);

#if defined(BUILD_AS_DKMS) && defined(CONFIG_CGROUP_DEVICE) && \
                 (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
static inline int devcgroup_check_permission(short type, u32 major, u32 minor,
					short access)
{
	return __kcl_devcgroup_check_permission(type, major, minor, access);
}
#elif !defined(CONFIG_CGROUP_DEVICE) && !defined(BUILD_AS_DKMS)
static inline int devcgroup_check_permission(short type, u32 major, u32 minor,
					short access)
{
	return 0;
}

#endif

#endif /*AMDKCL_DEVICE_CGROUP_H*/
