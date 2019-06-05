dnl #
dnl # commit 9705bea5f833f
dnl # mm: convert zone->managed_pages to atomic variable
dnl #
AC_DEFUN([AC_AMDGPU_MANAGED_PAGES_IN_STRUCT_ZONE],
	[AC_MSG_CHECKING([whether zone->managed_pages is atomic_long_t])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmzone.h>
	], [
		struct zone *z = NULL;
		atomic_long_read(&z->managed_pages);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ATOMIC_MANAGED_PAGES_IN_STRUCT_ZONE, 1, [zone->managed_pages is atomic_long_t])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether zone->managed_pages is defined])
dnl #
dnl # commit 9feedc9d831e
dnl # mm: introduce new field "managed_pages" to struct zone
dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mmzone.h>
		], [
			struct zone *z = NULL;
			z->managed_pages = 0;
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_MANAGED_PAGES_IN_STRUCT_ZONE, 1, [zone->managed_pages is defined])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
