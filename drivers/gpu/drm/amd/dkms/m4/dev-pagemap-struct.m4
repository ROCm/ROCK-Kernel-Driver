dnl #
dnl # commit 9476df7d80dfc425b37bfecf1d89edf8ec81fcb6
dnl # Author: Dan Williams <dan.j.williams@intel.com>
dnl # Date:   Fri Jan 15 16:56:19 2016 -0800
dnl # mm: introduce find_dev_pagemap()
dnl #
AC_DEFUN([AC_AMDGPU_DEV_PAGEMAP_STRUCT],
	[AC_MSG_CHECKING([whether struct dev_pagemap is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/memremap.h>
	], [
		struct dev_pagemap pgmap;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEV_PAGEMAP_STRUCT, 1, [struct dev_pagemap is defined])
	], [
		AC_MSG_RESULT(no)
	])
])
