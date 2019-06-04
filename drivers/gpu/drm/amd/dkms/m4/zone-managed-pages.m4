dnl #
dnl # commit 9705bea5f833f4fc21d5bef5fce7348427f76ea4
dnl # Author: Arun KS <arunks@codeaurora.org>
dnl # Date:   Fri Dec 28 00:34:24 2018 -0800
dnl # mm: convert zone->managed_pages to atomic variable
dnl #
AC_DEFUN([AC_AMDGPU_ZONE_MANAGED_PAGES],
	[AC_MSG_CHECKING([whether zone_managed_pages() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmzone.h>
	],[
		zone_managed_pages(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_MANAGED_PAGES, 1, [whether zone_managed_pages() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
