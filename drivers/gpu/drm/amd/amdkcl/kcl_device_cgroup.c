#include <kcl/kcl_device_cgroup.h>
#include "kcl_common.h"

int (*__kcl_devcgroup_check_permission)(short type, u32 major, u32 minor,
					short access);
EXPORT_SYMBOL(__kcl_devcgroup_check_permission);

void amdkcl_dev_cgroup_init(void)
{
	__kcl_devcgroup_check_permission = amdkcl_fp_setup("__devcgroup_check_permission", NULL);
}
