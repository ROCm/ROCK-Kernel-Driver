dnl e8d5134833006a46fcbefc5f4a84d0b62bd520e7
dnl # memremap: change devm_memremap_pages interface to use struct dev_pagemap
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_DEVM_MEMREMAP_PAGES],
	[AC_MSG_CHECKING([whether devm_memremap_pages() wants 2 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/memremap.h>
		struct device *dev = NULL;
		struct dev_pagemap *pgmap = NULL;
	], [
		devm_memremap_pages(dev, pgmap);
	], [devm_memremap_pages], [kernel/memremap.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_DEVM_MEMREMAP_PAGES, 1, [devm_memremap_pages() wants 2 args])
	], [
		AC_MSG_RESULT(no)
	])
])
