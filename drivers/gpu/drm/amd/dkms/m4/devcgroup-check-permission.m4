dnl #
dnl # commit ecf8fecb7828
dnl # device_cgroup: prepare code for bpf-based device controller
dnl # commit 237d9c962b357
dnl # device_cgroup: Export __devcgroup_check_permission
dnl #
AC_DEFUN([AC_AMDGPU___DEVCGROUP_CHECK_PERMISSION],
	[AC_MSG_CHECKING([whether __devcgroup_check_permission() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/device_cgroup.h>
	], [
		__devcgroup_check_permission(0, 0, 0, 0);
	], [__devcgroup_check_permission], [security/device_cgroup.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE___DEVCGROUP_CHECK_PERMISSION, 1, [__devcgroup_check_permission() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
