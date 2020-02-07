dnl #
dnl # commit v3.11-rc1-5-g6ab9cea16075
dnl # sysfs: add support for binary attributes in groups
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_ATTRIBUTE_GROUP], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/sysfs.h>
		], [
			struct attribute_group *ag;
			ag->bin_attrs = NULL;
		], [
			AC_DEFINE(HAVE_ATTRIBUTE_GROUP_BIN_ATTRS, 1,
				[attribute_group->bin_attrs is available])
		])
	])
])
