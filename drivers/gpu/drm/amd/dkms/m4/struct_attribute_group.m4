dnl #
dnl # commit v4.3-rc4-9-g7f5028cf6190
dnl # sysfs: Support is_visible() on binary attributes
dnl #
AC_DEFUN([AC_AMDGPU_ATTRIBUTE_GROUP_IS_BIN_VISIBLE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/sysfs.h>
		],[
			struct attribute_group *amdgpu_attr_group = NULL;
			amdgpu_attr_group->is_bin_visible = NULL;
		],[
			AC_DEFINE(HAVE_ATTRIBUTE_GROUP_IS_BIN_VISIBLE, 1,
				[amdgpu_attr_group->is_bin_visible is available])
		])
	])
])