dnl #
dnl # commit 9feedc9d831e18ae6d0d15aa562e5e46ba53647b
dnl # mm: introduce new field "managed_pages" to struct zone
dnl #
AC_DEFUN([AC_AMDGPU_MANAGED_PAGES_IN_STRUCT_ZONE],
	[AC_MSG_CHECKING([for managed_pages field within zone structure])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmzone.h>
	], [
		struct zone z;
		z.managed_pages = 0;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MANAGED_PAGES_IN_STRUCT_ZONE, 1, [zone structure contains managed_pages field])
	], [
		AC_MSG_RESULT(no)
	])
])
