dnl #
dnl # commit 6ab9cea16075ea707022753395f340b67f64304c
dnl # sysfs: add support for binary attributes in groups
dnl #
AC_DEFUN([AC_AMDGPU_BIN_ATTRS_IN_ATTRIBUTE_GROUP],
	[AC_MSG_CHECKING([for bin_attrs field within attribute_group structure])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/sysfs.h>
	], [
		struct attribute_group ag;
		ag.bin_attrs = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIN_ATTRS_IN_ATTRIBUTE_GROUP, 1, [attribute_group structure contains bin_attrs field])
	], [
		AC_MSG_RESULT(no)
	])
])
